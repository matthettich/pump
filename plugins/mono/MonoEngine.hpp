// pump MONO engine: an analog-style monophonic synth. Framework-free.
//
// Everything runs at twice the host rate and is brought down by a steep half-band filter (about 32 samples of
// latency): the oscillators are PolyBLEP (band-limited) and the filters and overdrive are nonlinear, so their
// distortion would otherwise fold back as aliasing.
//   oscillators  three (triangle / saw / square / pulse, 32' .. 2'); 2 can be hard-synced to 1; 3 can frequency-
//                modulate 1 (FM); each drifts a little on its own (DRIFT); noise
//   mixer        the levels sum into OVERDRIVE (a soft clip before the filter, the old trick of driving a filter
//                input hot)
//   filter       Ladder 24 (four TPT one-poles in a tanh feedback loop), SVF 12 (state-variable, MODE sweeps
//                low-pass -> notch -> high-pass, soft-clipped integrators), Diode 18 (three poles with saturating
//                stages and a high-pass in the resonance loop), Sallen 12 (two poles, a clipped resonance loop that
//                screams when pushed); cutoff from the knob, the filter envelope (+-), key tracking, velocity, LFO,
//                drift
//   amp          the amp envelope, velocity
// Notes: one voice, a note stack with LAST / LOW / HIGH priority; LEGATO = overlapping notes don't retrigger the
// envelopes; glide ALWAYS or only between overlapping (LEGATO) notes. The LFO (triangle, saw, square, S&H) goes to
// pitch, filter and pulse width; the mod wheel adds vibrato on top.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Oversampler.hpp"
#include "MonoParams.hpp"

namespace pump { namespace mono {

struct Settings {
    int wave[3] = { kSaw, kSaw, kSquare }, oct[3] = { 1, 1, 0 };
    double pitch[3] = { 0.0, 0.08, -0.06 }, level[3] = { 0.8, 0.7, 0.0 };
    bool sync = false;
    double fm = 0.0, pw = 0.25, noise = 0.0, overdriveDb = 0.0;
    int filterType = kLadder;
    double cutoff = 800.0, res = 0.2, filterEnv = 0.45, keyTrack = 0.33, mode = 0.0, velFilter = 0.3;
    double fA = 0.002, fD = 0.35, fS = 0.2, fR = 0.25, aA = 0.002, aD = 0.6, aS = 0.85, aR = 0.15, velAmp = 0.4;
    int lfoShape = kLfoTri;
    double lfoRate = 5.0, lfoPitch = 0.0, lfoFilter = 0.0, lfoPw = 0.0, wheelVib = 0.3;
    double glide = 0.0;
    int glideMode = 0, priority = kLast;
    bool legato = true;
    double drift = 0.2, bendRange = 2.0, volumeDb = -6.0;
};

// ---- the filter models (at the oversampled rate) ----------------------------------------------------------------
class MonoFilter {
public:
    void reset() { ladder.reset(); svf.reset(); for (auto& d : dz) d = 0.0; fbHp.reset(); s1 = s2 = 0.0; }
    void set(int t, double fc, double res, double mode, double sr)
    {
        type = t; m = mode;
        fc = clampv(fc, 15.0, 0.45 * sr);
        switch (t)
        {
        case kLadder: ladder.set(fc, res, sr); break;
        case kSvf: svf.set(fc, 0.5 + 9.5 * res * res, sr); break;
        case kDiode:
        {
            // three equal poles reach -180 degrees at sqrt(3) x their corner: tune them so the resonance sits at fc
            for (auto& g : dg) { const double w = std::tan(kPi * fc / 1.7320508 / sr); g = w / (1.0 + w); }
            k = 10.5 * clampv(res, 0.0, 1.0);   // oscillation from about 76 %
            fbHp.set(120.0, sr);
            break;
        }
        default:   // Sallen: a TPT SVF with its band state clipped; resonance runs to self-oscillation
        {
            g = std::tan(kPi * fc / sr);
            kd = 2.0 - 2.15 * clampv(res, 0.0, 1.0);   // below zero from 93 %: it oscillates, the clipped state holds it
            break;
        }
        }
    }
    inline double process(double x)
    {
        switch (type)
        {
        case kLadder: return ladder.process(x, 0, 1.0);
        case kSvf:
        {
            double lp, bp, hp;
            svf.process(std::tanh(x), lp, bp, hp);
            svf.ic1 = std::tanh(svf.ic1 * 0.5) * 2.0;   // the integrators saturate softly
            // LP -> notch -> HP
            if (m < 0.5) { const double t = m * 2.0; return lp * (1.0 - t) + (lp + hp) * t; }
            const double t = (m - 0.5) * 2.0;
            return (lp + hp) * (1.0 - t) + hp * t;
        }
        case kDiode:
        {
            // three saturating one-poles; the resonance loop goes through a high-pass (thin bass, rubbery squelch)
            double u = std::tanh(x - k * fbHp.hp(dz[2]));
            for (int i = 0; i < 3; ++i)
            {
                const double v = (u - dz[i]) * dg[i];
                const double y = v + dz[i];
                dz[i] = y + v;
                u = i < 2 ? std::tanh(y * 1.1) : y;
            }
            return u * (1.0 + 0.3 * k / 10.5);
        }
        default:
        {
            // Simper SVF, the band state clipped: a loud, screaming resonance that limits itself
            const double a1 = 1.0 / (1.0 + g * (g + kd)), a2 = g * a1, a3 = g * a2;
            const double v3 = std::tanh(x) - s2;
            const double v1 = a1 * s1 + a2 * v3;
            const double v2 = s2 + a2 * s1 + a3 * v3;
            s1 = std::tanh(2.0 * v1 - s1);
            s2 = 2.0 * v2 - s2;
            return v2;
        }
        }
    }
private:
    int type = kLadder;
    double m = 0.0, k = 0.0, g = 0.1, kd = 1.4, s1 = 0.0, s2 = 0.0;
    double dg[3] = { 0.1, 0.1, 0.1 }, dz[3] = { 0, 0, 0 };
    synth::Ladder ladder;
    fx::Svf svf;
    fx::OnePole fbHp;
};

class Engine {
public:
    static constexpr int kCtl = 16;   // base-rate samples between control updates

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate; osr = 2.0 * sr;
        hb.setup(tables::kHB1, (int)(sizeof(tables::kHB1) / sizeof(double)));
        fEnv.setSampleRate(osr); aEnv.setSampleRate(osr);
        fEnv.kill(); aEnv.kill();
        filter.reset();
        for (auto& p : ph) p = 0.0;
        nStack = 0; gate = false; note = 60; pitch = 60.0; target = 60.0; vel = 1.0;
        for (int i = 0; i < 4; ++i) { drift[i].setup(sr, 0xD1F7u + 77u * i); drift[i].setRate(0.3 + 0.17 * i); }
        lfoPh = 0.0; lfoHeld = 0.0;
        bend = 0.0; wheel = 0.0; sustain = false; sustained = false;
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        ctl = 0;
    }
    void setSettings(const Settings& s) { set = s; }
    // the latency of the half-band decimator, in host samples
    int latency() const { return (hb.delay() + 1) / 2; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    float cutoffNow() const { return cutFeed.load(std::memory_order_relaxed); }
    float pitchNow() const { return pitchFeed.load(std::memory_order_relaxed); }       // the note sounding (with glide)
    bool gateNow() const { return gateFeed.load(std::memory_order_relaxed); }
    float ampNow() const { return ampFeed.load(std::memory_order_relaxed); }
    float filterEnvNow() const { return fenvFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return gateNow() && n == litNote.load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if ((ctl++ % kCtl) == 0) control();
            float y[2];
            for (int k = 0; k < 2; ++k) y[k] = (float)voiceSample();
            const float out = hb.down(y[0], y[1]) * (float)volS.next();
            L[i] = R[i] = out;
            outRing.push(out);
        }
        while (e < nev) handle(ev[e++]);
        if (!std::isfinite(L[0] + L[n - 1])) { filter.reset(); hb.reset(); sanitizeBlock(L, n); sanitizeBlock(R, n); }
        cutFeed.store((float)cutNow, std::memory_order_relaxed);
        pitchFeed.store((float)pitch, std::memory_order_relaxed);
        gateFeed.store(aEnv.active() && aEnv.state() != synth::Adsr::Release, std::memory_order_relaxed);
        litNote.store(note, std::memory_order_relaxed);
        ampFeed.store((float)aEnv.level(), std::memory_order_relaxed);
        fenvFeed.store((float)fEnv.level(), std::memory_order_relaxed);
    }
    void uiNote(uint8_t n, uint8_t v) { synth::Midi m { 0, (uint8_t)(v ? 0x90 : 0x80), n, v }; handle(m); }

private:
    // ---- notes ----
    int chooseNote() const
    {
        if (nStack == 0) return -1;
        if (set.priority == kLow) { int m = 128; for (int i = 0; i < nStack; ++i) m = std::min(m, stack[i]); return m; }
        if (set.priority == kHigh) { int m = -1; for (int i = 0; i < nStack; ++i) m = std::max(m, stack[i]); return m; }
        return stack[nStack - 1];
    }
    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        if (st == 0x90 && m.d2 > 0)
        {
            const bool overlapping = nStack > 0;
            remove(m.d1);
            if (nStack < 64) stack[nStack++] = m.d1;
            const int want = chooseNote();
            if (want != note || !gate) play(want, m.d2 / 127.0, overlapping);
        }
        else if (st == 0x80 || (st == 0x90 && m.d2 == 0))
        {
            remove(m.d1);
            const int want = chooseNote();
            if (want >= 0) { if (want != note) play(want, -1.0, true); }
            else if (sustain) sustained = true;
            else release();
        }
        else if (st == 0xE0) bend = ((m.d2 << 7 | m.d1) - 8192) / 8192.0;
        else if (st == 0xB0)
        {
            if (m.d1 == 1) wheel = m.d2 / 127.0;
            else if (m.d1 == 64) { sustain = m.d2 >= 64; if (!sustain && sustained && nStack == 0) { sustained = false; release(); } }
            else if (m.d1 == 120 || m.d1 == 123) { nStack = 0; release(); if (m.d1 == 120) { aEnv.kill(); fEnv.kill(); } }
        }
    }
    void remove(int n) { int k = 0; for (int i = 0; i < nStack; ++i) if (stack[i] != n) stack[k++] = stack[i]; nStack = k; }
    void play(int n, double velocity, bool overlapping)
    {
        note = n;
        target = n;
        if (velocity >= 0.0) vel = velocity;
        const bool glideNow = set.glide > 0.0 && (set.glideMode == 0 ? (gate || aEnv.active()) : overlapping);
        if (!glideNow) pitch = n;
        // a new note (not tied) retriggers the envelopes; with LEGATO, overlapping notes don't
        if (!(overlapping && set.legato && gate))
        {
            fEnv.gate(true); aEnv.gate(true);
            if (!aEnv.active() || aEnv.level() < 1e-4) for (auto& p : ph) p = 0.0;
        }
        gate = true;
        sustained = false;
    }
    void release() { gate = false; fEnv.gate(false); aEnv.gate(false); }

    // ---- control rate (base rate, every kCtl samples) ----
    void control()
    {
        const Settings& s = set;
        fEnv.set(s.fA, s.fD, s.fS, s.fR);
        aEnv.set(s.aA, s.aD, s.aS, s.aR);
        if (s.glide > 0.0) { const double k = 1.0 - std::exp(-(double)kCtl / (s.glide * 0.001 * sr * 0.4)); pitch += (target - pitch) * k; }
        else pitch = target;
        // LFO
        lfoPh += s.lfoRate * kCtl / sr;
        if (lfoPh >= 1.0) { lfoPh -= std::floor(lfoPh); lfoHeld = rng.bi(); }
        double lfo;
        switch (s.lfoShape)
        {
        case kLfoSaw: lfo = 2.0 * lfoPh - 1.0; break;
        case kLfoSquare: lfo = lfoPh < 0.5 ? 1.0 : -1.0; break;
        case kLfoSh: lfo = lfoHeld; break;
        default: lfo = 1.0 - 4.0 * std::fabs(lfoPh - 0.5); break;
        }
        const double vib = lfo * (s.lfoPitch * s.lfoPitch * 12.0 + wheel * s.wheelVib * 0.6);   // semitones
        const double base = pitch + bend * s.bendRange + vib;
        for (int o = 0; o < 3; ++o)
        {
            const double d = s.drift * 0.12 * drift[o].advance(kCtl);   // up to +-12 cents
            const double semis = base + (s.oct[o] - 2) * 12 + s.pitch[o] + d;
            inc[o] = synth::midiToHz(clampv(semis, -36.0, 150.0)) / osr;
        }
        pwNow = clampv(s.pw + s.lfoPw * 0.45 * lfo, 0.03, 0.97);
        // the filter
        const double env = fEnv.level();
        const double oct = std::log2(s.cutoff) + 7.0 * s.filterEnv * env * (1.0 - s.velFilter + s.velFilter * vel)
                         + s.keyTrack * (pitch - 60.0) / 12.0 + 4.0 * s.lfoFilter * lfo + s.drift * 0.08 * drift[3].advance(kCtl);
        cutNow = clampv(std::exp2(oct), 15.0, 0.45 * osr);
        filter.set(s.filterType, cutNow, s.res, s.mode, osr);
        driveG = dbToGain(s.overdriveDb);
        ampVel = 1.0 - s.velAmp + s.velAmp * vel;
    }

    // one sample at the oversampled rate
    inline double oscValue(int o, double p, double dt) const
    {
        switch (set.wave[o])
        {
        case kTri: return synth::blepTri(p, dt);
        case kSquare: return synth::blepPulse(p, dt, 0.5);
        case kPulse: return synth::blepPulse(p, dt, pwNow);
        default: return synth::blepSaw(p, dt);
        }
    }
    inline double voiceSample()
    {
        if (!aEnv.active()) { aEnv.next(); fEnv.next(); return filter.process(0.0) * 0.0; }
        const Settings& s = set;
        // osc 3 first (it may modulate 1)
        const double y3 = oscValue(2, ph[2], inc[2]);
        const double inc1 = inc[0] * (1.0 + s.fm * s.fm * 4.0 * y3 * 0.5);
        const double i1 = std::max(0.0, inc1);
        const double y1 = oscValue(0, ph[0], std::max(1e-6, i1));
        const double y2 = oscValue(1, ph[1], inc[1]);
        ph[0] += i1; ph[1] += inc[1]; ph[2] += inc[2];
        bool wrapped1 = false;
        if (ph[0] >= 1.0) { ph[0] -= std::floor(ph[0]); wrapped1 = true; }
        if (ph[1] >= 1.0) ph[1] -= std::floor(ph[1]);
        if (ph[2] >= 1.0) ph[2] -= std::floor(ph[2]);
        if (s.sync && wrapped1) ph[1] = ph[0] * inc[1] / std::max(1e-9, i1);   // restart 2 where 1 restarted
        double mix = s.level[0] * y1 + s.level[1] * y2 + s.level[2] * y3 + s.noise * rng.bi();
        mix *= 0.5;
        // overdrive into the filter
        if (driveG > 1.0) mix = std::tanh(mix * driveG) / std::sqrt(driveG);
        // the filters see the mix at -14 dB (they saturate when OVERDRIVE pushes them, not at rest), made up after
        const double f = filter.process(0.2 * mix) * 5.0;
        const double a = aEnv.next();
        fEnv.next();
        return f * a * ampVel * 0.9;
    }

    double sr = 48000.0, osr = 96000.0;
    Settings set;
    HalfbandStage hb;
    MonoFilter filter;
    synth::Adsr fEnv, aEnv;
    double ph[3] = { 0, 0, 0 }, inc[3] = { 0.01, 0.01, 0.01 }, pwNow = 0.25, driveG = 1.0, ampVel = 1.0, cutNow = 800.0;
    int stack[64] = {}, nStack = 0, note = 60;
    bool gate = false, sustain = false, sustained = false;
    double pitch = 60.0, target = 60.0, vel = 1.0, bend = 0.0, wheel = 0.0;
    fx::Wander drift[4];
    double lfoPh = 0.0, lfoHeld = 0.0;
    fx::Rng rng { 0x3051u };
    LinearSmoother volS;
    unsigned ctl = 0;
    std::atomic<float> cutFeed { 800.0f }, pitchFeed { 60.0f }, ampFeed { 0.0f }, fenvFeed { 0.0f };
    std::atomic<bool> gateFeed { false };
    std::atomic<int> litNote { -1 };
};

}} // namespace pump::mono
