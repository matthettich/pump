// pump WOB engine: an additive mono bass. Framework-free.
//
// Up to 256 sine partials are summed per sample. Their levels are set every 32 samples by a chain of spectral
// shapers, so nothing here can alias (partials near Nyquist fade out and above it are dropped):
//   source   the spectrum morphs saw (1/k) -> square (odd 1/k) -> triangle (odd 1/k^2); PARTIALS keeps the first N;
//            TILT leans it brighter or darker; ODD/EVEN takes out the even (+) or the odd (-) partials; STRETCH
//            pushes the upper partials sharp, for bars, bells and metal
//   filter   a 24 dB low-pass, a resonant band-pass or a 24 dB high-pass, as a gain per partial; a decaying
//            envelope and key tracking move the cutoff
//   formant  two vowel resonances (a e i o u, morphed)
//   comb     a cosine comb across the spectrum (the hollow, phasey colour)
//   morph    a second source spectrum (its own SHAPE B and TILT B) to morph to, by MORPH and the wobble
//   motion   INHARMONIC bends the partial series sharp by a power law (bells, gongs, glass); SPRAY detunes every
//            partial by its own fixed amount (a dense, chorused body); DRIFT moves each one's pitch and level by its
//            own slow random walk (at DRIFT RATE): the spectrum lives
// A REESE second set of partials, detuned, crosses the first in stereo; the upper partials alternate left / right
// (WIDTH). A sine SUB one or two octaves down. The WOBBLE (sine, triangle, saws, square, random steps) runs at a
// note division of the host tempo or free in hertz; it pushes the cutoff, vowel, comb, level and pitch. With
// RETRIG it restarts with each note (at START PHASE); without it, a synced wobble follows the song position. A
// DELAY and FADE-IN hold it back after each note; the mod wheel can add depth. Mono, with glide and legato.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Tempo.hpp"
#include "WobParams.hpp"
#include "dsp/Fx.hpp"
#include <array>

namespace pump { namespace wob {

struct Settings {
    int octave = -1;
    double tune = 0.0, shape = 0.0;
    int partials = 48;
    double tilt = 0.0, oddEven = 0.0, stretch = 0.0;
    int filterMode = kLowPass;
    double cutoff = 400.0, res = 0.35, filterEnv = 2.0, filterDecay = 0.25, keyTrack = 0.5;
    double vowel = 0.0, formant = 0.0, combFreq = 0.5, comb = 0.0;
    double detune = 0.0, width = 0.3, sub = 0.5;
    int subOct = 0;
    bool wobSync = true, wobRetrig = true;
    int wobDiv = kNote8, wobShape = kSine;
    double wobRate = 4.0, wobPhase = 0.0, wobSmooth = 0.15, wobDelay = 0.0, wobFade = 0.0, wobDepth = 1.0, wheel = 0.0;
    double wobCut = 0.0, wobVowel = 0.0, wobComb = 0.0, wobAmp = 0.0, wobPitch = 0.0;
    double attack = 0.002, decay = 0.3, sustain = 1.0, release = 0.08, glide = 0.0;
    bool legato = false;
    double drive = 0.0, volumeDb = -6.0;
    double shapeB = 1.0, tiltB = 0.4, morph = 0.0, wobMorph = 0.0, inharm = 0.0, spray = 0.0, drift = 0.0, driftRate = 0.6;
};

class Engine {
public:
    static constexpr int N = kMaxPartials, kBlk = 32, kTab = 4096;

    Engine()
    {
        for (int i = 0; i <= kTab; ++i) sinT[(size_t)i] = (float)std::sin(kTwoPi * i / kTab);
        for (int k = 1; k <= N; ++k) phB[k] = std::fmod(k * 0.618034, 1.0);
        fx::Rng r0(0x5EEDu);
        for (int k = 1; k <= N; ++k) { sprayR[k] = r0.bi(); dPh[k] = r0.uni(); dT[k] = r0.bi(); dA[k] = r0.bi(); dRate[k] = 0.7 + 0.6 * r0.uni(); }
        rebuild();
    }
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        env.setSampleRate(sr);
        env.kill();
        vm.setup(1, set.legato ? synth::kLegato : synth::kMono); vm.reset();
        aA.fill(0.0); aT.fill(0.0); phA.fill(0.0);
        subPh = 0.0; fe = 0.0; lf = 0.0; incPrev = 0.0; sounding = false; held = false; note = 48; blkPos = 0;
        lph = 0.0; lsm = 0.0; sh = 0.5; wt = 0.0; ampPrev = 1.0;
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
    }
    void setSettings(const Settings& s)
    {
        const bool mono = s.legato != set.legato;
        set = s;
        set.partials = clampv(set.partials, 1, N);
        if (mono) vm.setup(1, set.legato ? synth::kLegato : synth::kMono);
        env.set(set.attack, set.decay, set.sustain, set.release);
        rebuild();
    }
    void setTransport(const Transport& t) { tr = t; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    // the source spectrum and what the shapers leave of it, partial k at [k - 1], relative to the loudest source one
    float sourceLevel(int k) const { return srcFeed[(size_t)clampv(k, 1, N) - 1].load(std::memory_order_relaxed); }
    float partialLevel(int k) const { return outFeed[(size_t)clampv(k, 1, N) - 1].load(std::memory_order_relaxed); }
    float fundamentalNow() const { return f0Feed.load(std::memory_order_relaxed); }
    float cutoffNow() const { return cutFeed.load(std::memory_order_relaxed); }
    float ampNow() const { return ampFeed.load(std::memory_order_relaxed); }
    float wobbleNow() const { return wobFeed.load(std::memory_order_relaxed); }      // 0..1, after depth
    float wobblePhase() const { return wobPhFeed.load(std::memory_order_relaxed); }
    bool soundingNow() const { return sounding; }
    bool noteLit(int n) const { return n >= 0 && n < 128 && litFeed.load(std::memory_order_relaxed) == n; }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if (blkPos == 0) control();
            const double t = (blkPos + 1) * (1.0 / kBlk);
            double l = 0.0, r = 0.0;
            if (sounding)
            {
                const double inc = incPrev + (incNow - incPrev) * t;
                synthSample(inc, t, l, r);
                const double a = env.next();
                l *= a; r *= a;
                if (!env.active()) { sounding = false; vm.voiceFinished(0); }
                if (driveK > 0.0) { l = std::tanh(l * driveK) * driveN; r = std::tanh(r * driveK) * driveN; }
            }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
            if (++blkPos == kBlk) { blkPos = 0; for (int k = 1; k <= N; ++k) aA[k] = aT[k]; incPrev = incNow; }
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { aA.fill(0.0); aT.fill(0.0); phA.fill(0.0); sanitizeBlock(L, n); sanitizeBlock(R, n); }
        ampFeed.store((float)env.level());
        litFeed.store(sounding && env.state() != synth::Adsr::Release ? note : -1);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiNote(uint8_t nn, uint8_t v) { synth::Midi m { 0, (uint8_t)(v ? 0x90 : 0x80), nn, v }; handle(m); }

    // the spectral shapers' gain for a partial at fk hertz (also drawn by the UI)
    static double filterGain(int mode, double fk, double fc, double res)
    {
        const double Q = 0.6 + res * res * 11.0, x = fk / fc, x2 = x * x, d1 = 1.0 - x2;
        const double two = std::sqrt(d1 * d1 + x2 / (Q * Q)), but = std::sqrt(d1 * d1 + 2.0 * x2);
        switch (mode)
        {
        case kHighPass: return (x2 / two) * (x2 / but);
        case kBandPass: { const double b = (x / Q) / two; return b * b; }
        default: return 1.0 / two / but;
        }
    }
    static double formantGain(double fk, double vowel, double amount)
    {
        if (amount <= 0.0) return 1.0;
        static const double vow[5][2] = { { 730, 1090 }, { 530, 1840 }, { 270, 2290 }, { 570, 840 }, { 300, 870 } };   // a e i o u
        const double vp = clampv(vowel, 0.0, 1.0) * 4.0;
        const int vi = std::min(3, (int)vp);
        const double vt = vp - vi;
        const double F1 = std::log2(vow[vi][0] * (1 - vt) + vow[vi + 1][0] * vt), F2 = std::log2(vow[vi][1] * (1 - vt) + vow[vi + 1][1] * vt);
        const double lk = std::log2(std::max(1.0, fk)), g1 = (lk - F1) / 0.32, g2 = (lk - F2) / 0.32;
        return 1.0 - amount + amount * (0.1 + 2.6 * (std::exp(-g1 * g1) + 0.6 * std::exp(-g2 * g2)));
    }
    static double combHz(double c) { return 60.0 * std::exp2(clampv(c, 0.0, 1.0) * 6.0); }

private:
    void handle(const synth::Midi& m)
    {
        vm.midi(m.status, m.d1, m.d2, [&](const synth::VoiceCmd& c) {
            switch (c.kind)
            {
            case synth::VoiceCmd::Start: start(c.note, false); break;
            case synth::VoiceCmd::Glide: start(c.note, true); break;
            case synth::VoiceCmd::Release: held = false; env.gate(false); break;
            case synth::VoiceCmd::Kill: held = false; env.kill(); sounding = false; vm.voiceFinished(0); break;
            }
        });
    }
    void start(int nn, bool legato)
    {
        const bool was = sounding && env.state() != synth::Adsr::Release;
        note = nn;
        glideFrom = sounding && set.glide > 0.5;   // glide only from a note that still sounds
        if (!legato || !was)
        {
            if (!sounding || env.level() < 0.002) { phA.fill(0.0); subPh = 0.0; aA.fill(0.0); }
            env.gate(true);
            fe = 1.0; wt = 0.0;
            if (set.wobRetrig) { lph = set.wobPhase; lsm = shapeAt(lph); sh = rng.uni(); }
        }
        held = true;
        if (!sounding) { sounding = true; glideFrom = false; }
        go = true;
    }
    double shapeAt(double ph) const
    {
        switch (set.wobShape)
        {
        case kTriangle: return ph < 0.5 ? ph * 2.0 : 2.0 - ph * 2.0;
        case kSawDown: return 1.0 - ph;
        case kSawUp: return ph;
        case kSquare: return ph < 0.5 ? 1.0 : 0.0;
        case kSteps: return sh;
        default: return 0.5 - 0.5 * std::cos(kTwoPi * ph);
        }
    }
    // the source spectrum and the stereo placement of each partial
    // a source spectrum: saw -> square -> triangle by shape, leaned by tilt, odd / even, the first N partials
    void spectrum(double shape, double tilt, std::array<double, N + 1>& out, double& e2, double& mx) const
    {
        const double s = clampv(shape, 0.0, 1.0);
        e2 = 0.0; mx = 1e-9;
        for (int k = 1; k <= N; ++k)
        {
            double a = 0.0;
            if (k <= set.partials)
            {
                const bool odd = (k & 1) != 0;
                if (s <= 0.5) a = odd ? 1.0 / k : (1.0 - 2.0 * s) / k;
                else
                {
                    const double t = (s - 0.5) * 2.0;
                    a = odd ? ((1.0 - t) / k + t / ((double)k * k)) * ((t > 0.5 && (((k - 1) / 2) & 1)) ? -1.0 : 1.0) : 0.0;
                }
                a *= std::pow((double)k, tilt);
                if (set.oddEven > 0.0 && !odd) a *= 1.0 - set.oddEven;
                else if (set.oddEven < 0.0 && odd && k > 1) a *= 1.0 + set.oddEven;
            }
            out[k] = a; e2 += a * a; mx = std::max(mx, std::fabs(a));
        }
    }
    // the source spectra and the stereo placement of each partial
    void rebuild()
    {
        double eA, mA, eB, mB;
        spectrum(set.shape, set.tilt, base, eA, mA);
        spectrum(set.shapeB, set.tiltB, baseB, eB, mB);
        for (int k = 1; k <= N; ++k)
        {
            const double pan = ((k & 1) ? -1.0 : 1.0) * set.width * std::min(1.0, (k - 1) / 4.0);
            panL[k] = std::sqrt((1.0 - pan) * 0.5) * 1.4142135623730951; panR[k] = std::sqrt((1.0 + pan) * 0.5) * 1.4142135623730951;
        }
        const double dn = set.detune > 0.5 ? 1.35 : 1.0;
        normA = 0.34 / std::max(0.25, std::sqrt(eA)) / dn; normB = 0.34 / std::max(0.25, std::sqrt(eB)) / dn;
        norm = normA;
        srcMax = mA; srcMaxB = mB;
        for (int k = 1; k <= N; ++k) srcFeed[(size_t)k - 1].store((float)(std::fabs(base[k]) / mA), std::memory_order_relaxed);
        const double dr = set.drive > 0.001 ? 1.0 + set.drive * set.drive * 12.0 : 0.0;
        driveK = dr; driveN = dr > 0.0 ? 0.5 / std::tanh(dr * 0.5) : 1.0;
    }

    // once per 32 samples: pitch, envelopes, wobble, and every partial's level for the end of the block
    void control()
    {
        const double dt = kBlk / sr;
        // wobble
        const double bpm = clampv(tr.bpm, 20.0, 999.0);
        if (set.wobSync)
        {
            const double q = noteQuarters(set.wobDiv);
            const double inc = dt * bpm / 60.0 / q;
            if (!set.wobRetrig && tr.playing && tr.valid && std::isfinite(tr.ppq) && std::fabs(tr.ppq) < 1e9)
            {
                const double want = tr.ppq / q - std::floor(tr.ppq / q);
                double d = want - lph; d -= std::round(d);
                lph += std::fabs(d) > 0.05 ? d : 0.1 * d;
            }
            lph += inc;
        }
        else lph += dt * set.wobRate;
        if (lph >= 1.0 || lph < 0.0) { lph -= std::floor(lph); sh = rng.uni(); }
        const double raw = shapeAt(lph), sm = set.wobSmooth > 0.001 ? 1.0 - std::exp(-dt / (set.wobSmooth * 0.04)) : 1.0;
        lsm += (raw - lsm) * sm;
        wt += dt * 1000.0;
        const double wd = (set.wobDelay > 0.5 || set.wobFade > 0.5) ? clampv((wt - set.wobDelay) / std::max(0.5, set.wobFade), 0.0, 1.0) : 1.0;
        const double depth = clampv(set.wobDepth + vm.wheel() * set.wheel, 0.0, 1.0) * wd;
        const double u = lsm * depth;
        wobFeed.store((float)u, std::memory_order_relaxed); wobPhFeed.store((float)lph, std::memory_order_relaxed);
        if (!sounding) return;

        // pitch, with glide
        const double semis = note + set.octave * 12.0 + set.tune + vm.pitchBend() * 2.0 + set.wobPitch * u;
        const double lt = std::log2(synth::midiToHz(semis));
        if (go) { go = false; if (!glideFrom || lf <= 0.0) lf = lt; }
        if (set.glide > 0.5 && glideFrom) lf += (lt - lf) * (1.0 - std::exp(-dt * 3000.0 / set.glide));   // ~95 % of the way in the glide time else lf = lt;
        if (std::fabs(lf - lt) < 1e-4) glideFrom = set.glide > 0.5;
        const double f = std::exp2(lf);
        incNow = f / sr;
        if (incPrev <= 0.0) incPrev = incNow;
        fe *= std::exp(-dt * 4.6 / std::max(0.001, set.filterDecay));

        // the spectrum
        const double nyq = sr * 0.5;
        const double fc = clampv(set.cutoff * std::exp2(set.filterEnv * fe + set.wobCut * u) * std::pow(f / 65.406, set.keyTrack), 20.0, nyq * 0.98);
        const double vowel = clampv(set.vowel + set.wobVowel * u, 0.0, 1.0);
        const double cHz = combHz(set.combFreq + set.wobComb * u);
        const double st = set.stretch * 0.0015;
        // the morph between the two spectra (each normalised to its own loudness), and the motion of every partial
        const double mo = clampv(set.morph + set.wobMorph * u, 0.0, 1.0);
        const double nA = (1.0 - mo) * normA, nB = mo * normB;
        const double gain = 1.0 - set.wobAmp * depth * (1.0 - lsm);
        const double ih = set.inharm * 0.35, spr = set.spray * 35.0, dr = set.drift;
        if (dr > 0.0)
        {
            const double kk = 1.0 - std::exp(-dt * set.driftRate * 3.0);
            for (int k = 1; k <= N; ++k)
            {
                dPh[k] += dt * set.driftRate * dRate[k];
                if (dPh[k] >= 1.0) { dPh[k] -= std::floor(dPh[k]); dT[k] = rng.bi(); dA[k] = rng.bi(); }
                dCur[k] += (dT[k] - dCur[k]) * kk; dAmp[k] += (dA[k] - dAmp[k]) * kk;
            }
        }
        norm = nA + nB;
        double shown = 0.0;
        for (int k = 1; k <= N; ++k)
        {
            const double b0 = base[k] * nA + baseB[k] * nB;
            double a = 0.0;
            double m = k * (1.0 + st * k * k);
            if (ih > 0.0) m *= std::pow((double)k, ih);
            if (spr > 0.0 || dr > 0.0) m *= std::exp2((spr * sprayR[k] * std::min(1.0, (k - 1) / 3.0) + dr * 25.0 * dCur[k] * std::min(1.0, (k - 1) / 2.0 + 0.2)) / 1200.0);
            mul[k] = m;
            if (b0 != 0.0)
            {
                const double fk = f * m;
                if (fk < nyq * 0.92)
                {
                    double g = filterGain(set.filterMode, fk, fc, set.res) * formantGain(fk, vowel, set.formant);
                    if (set.comb > 0.0) g *= 1.0 - set.comb * (0.5 + 0.5 * std::cos(kTwoPi * fk / cHz));
                    if (fk > nyq * 0.8) g *= (nyq * 0.92 - fk) / (nyq * 0.12);
                    a = b0 * g * gain * (dr > 0.0 ? 1.0 + 0.6 * dr * dAmp[k] : 1.0);
                }
            }
            aT[k] = a;
            const float lv = (float)(std::fabs(a) / std::max(1e-12, normA * srcMax));
            outFeed[(size_t)k - 1].store(lv, std::memory_order_relaxed);
            shown = std::max(shown, (double)lv);
        }
        cutFeed.store((float)fc, std::memory_order_relaxed);
        f0Feed.store((float)f, std::memory_order_relaxed);
        detRatio = set.detune > 0.5 ? std::exp2(set.detune / 1200.0) : 0.0;
        subA = set.sub * 0.45;
    }

    inline double tab(double ph) const
    {
        const double x = ph * kTab;
        const int j = (int)x;
        return sinT[(size_t)j] + (x - j) * (sinT[(size_t)j + 1] - sinT[(size_t)j]);
    }
    void synthSample(double inc, double t, double& l, double& r)
    {
        const bool det = detRatio > 0.0;
        const double half = det ? std::sqrt(detRatio) : 1.0;
        for (int k = 1; k <= N; ++k)
        {
            const double s0 = aA[k], s1 = aT[k];
            if (s0 == 0.0 && s1 == 0.0) continue;
            const double amp = s0 + (s1 - s0) * t;
            const double ik = inc * mul[k];
            if (ik >= 0.46) continue;   // past Nyquist (a pitch or stretch jump inside the block): never made
            if (det)
            {
                const double a = tab(phA[k]), b = tab(phB[k]);
                l += amp * (a * panL[k] + b * panR[k]); r += amp * (a * panR[k] + b * panL[k]);
                phA[k] += ik / half; phB[k] += ik * half;
                if (phB[k] >= 1.0) phB[k] -= std::floor(phB[k]);
            }
            else
            {
                const double a = tab(phA[k]) * amp;
                l += a * panL[k]; r += a * panR[k];
                phA[k] += ik;
            }
            if (phA[k] >= 1.0) phA[k] -= std::floor(phA[k]);
        }
        if (subA > 0.0)
        {
            const double s = tab(subPh) * subA;
            l += s; r += s;
            subPh += inc / (set.subOct ? 4.0 : 2.0);
            if (subPh >= 1.0) subPh -= 1.0;
        }
    }

    Settings set;
    Transport tr;
    synth::VoiceManager vm;
    synth::Adsr env;
    fx::Rng rng { 0xB0B5u };
    std::array<float, kTab + 1> sinT {};
    std::array<double, N + 1> base {}, baseB {}, panL {}, panR {}, aA {}, aT {}, phA {}, phB {}, mul {};
    std::array<double, N + 1> sprayR {}, dPh {}, dT {}, dA {}, dCur {}, dAmp {}, dRate {};
    double normA = 0.3, normB = 0.3, srcMaxB = 1.0;
    double sr = 48000.0, norm = 0.3, srcMax = 1.0, driveK = 0.0, driveN = 1.0;
    double subPh = 0.0, subA = 0.0, fe = 0.0, lf = 0.0, incNow = 0.0, incPrev = 0.0, detRatio = 0.0;
    double lph = 0.0, lsm = 0.0, sh = 0.5, wt = 0.0, ampPrev = 1.0;
    int note = 48, blkPos = 0;
    bool sounding = false, held = false, go = false, glideFrom = false;
    LinearSmoother volS;
    std::array<std::atomic<float>, N> srcFeed {}, outFeed {};
    std::atomic<float> f0Feed { 0.0f }, cutFeed { 400.0f }, ampFeed { 0.0f }, wobFeed { 0.0f }, wobPhFeed { 0.0f };
    std::atomic<int> litFeed { -1 };
};

}} // namespace pump::wob
