// pump POLY engine: a vintage-style analog polysynth. Framework-free.
//
// Per voice: oscillator A (saw and / or pulse) and B (saw, triangle and / or pulse; LOW = a few hertz, off the
// keyboard, for modulation), A can be synced to B; a sub square an octave under A; noise. POLY-MOD: the filter
// envelope and oscillator B (at audio rate) push A's frequency, A's pulse width and / or the cutoff, the classic
// source of bells, growls and sync sweeps. Then a high-pass and one of four low-pass voicings (Five 24, Eight 24,
// Eight 12, OB 12), the filter and amp envelopes. Each voice has its own small, fixed offsets in pitch and cutoff
// (SLOP: no two voices quite alike). One LFO for all (triangle, saw, square, S&H) with a per-note DELAY before it
// fades in; the mod wheel adds to its amount. Modes: POLY (up to 16 voices), UNISON (every voice on one note,
// detuned and spread in stereo), MONO. A bucket-brigade-style ensemble chorus (I, II, I+II) at the end.
#pragma once

#include "dsp/Synth.hpp"
#include "PolyParams.hpp"

namespace pump { namespace poly {

struct Settings {
    bool aSaw = true, aPulse = false, sync = false, bSaw = true, bTri = false, bPulse = false, bLow = false;
    double aFreq = 0.0, aPw = 0.5, bFreq = 0.0, bFine = 7.0, bPw = 0.5;
    double levelA = 0.8, levelB = 0.7, noise = 0.0, sub = 0.0;
    int filterType = kFive24;
    double cutoff = 2200.0, res = 0.15, filterEnv = 0.35, keyTrack = 0.5, hpf = kHpfOff, velFilter = 0.3;
    double fA = 0.005, fD = 0.7, fS = 0.3, fR = 0.5, aA = 0.005, aD = 0.8, aS = 0.8, aR = 0.6, velAmp = 0.4;
    double pmEnv = 0.0, pmOscB = 0.0;
    bool pmFreqA = true, pmPwA = false, pmFilter = false;
    int lfoShape = kLfoTri;
    double lfoRate = 5.0, lfoDelay = 0.0, lfoPitch = 0.0, lfoPw = 0.0, lfoFilter = 0.0, wheelMod = 0.4;
    int voiceMode = kModePoly, voices = 8;
    double unisonDetune = 0.25, glide = 0.0, slop = 0.25, bendRange = 2.0;
    int chorus = kChorusOff;
    double volumeDb = -8.0;
};

class Engine {
public:
    static constexpr int kMaxVoices = 16, kCtl = 16;
    static constexpr double kIn = 0.12, kOut = 1.0 / 0.12;   // the filters see the mix at about -18 dB

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        synth::BandWaves::get();   // build the shared tables now (not on the audio thread)
        fx::Rng r(0x5A5Au);
        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[i];
            v = Voice();
            v.fEnv.setSampleRate(sr); v.aEnv.setSampleRate(sr);
            v.slopPitch = r.bi(); v.slopCut = r.bi();
        }
        configure();
        lfoPh = 0.0; lfoHeld = 0.0;
        for (int ch = 0; ch < 2; ++ch) { chLine[ch].setMax((int)(0.02 * sr) + 8); chLine[ch].clear(); }
        chPh = 0.0;
        chMix.setRamp(std::max(1, (int)(0.03 * sr))); chMix.reset(set.chorus != kChorusOff ? 1.0 : 0.0);
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.voiceMode != set.voiceMode || s.voices != set.voices;
        set = s;
        if (re) { for (auto& v : voices) { v.active = false; v.aEnv.kill(); v.fEnv.kill(); } configure(); }
    }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    float cutoffNow() const { return cutFeed.load(std::memory_order_relaxed); }
    float ampNow() const { return ampFeed.load(std::memory_order_relaxed); }
    float filterEnvNow() const { return fenvFeed.load(std::memory_order_relaxed); }
    float lfoNow() const { return lfoFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }
    // which voices sound (bit i), for the voice lamps
    uint32_t voiceMask() const { return maskFeed.load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        chMix.setTarget(set.chorus != kChorusOff ? 1.0 : 0.0);
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if ((ctl++ % kCtl) == 0) control();
            double l = 0.0, r = 0.0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[vi];
                if (!v.active) continue;
                const double y = voiceSample(v);
                l += y * v.panL; r += y * v.panR;
                if (!v.aEnv.active()) { v.active = false; if (vi < vm.voices()) vm.voiceFinished(vi); }
            }
            double out[2] = { l * 0.3, r * 0.3 };
            chorus(out);
            const double vol = volS.next();
            L[i] = (float)(out[0] * vol); R[i] = (float)(out[1] * vol);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) v.resetFilters(); for (auto& c : chLine) c.clear(); sanitizeBlock(L, n); sanitizeBlock(R, n); }
        int act = 0; float a = 0.0f, f = 0.0f; uint32_t mask = 0; uint64_t lit[2] = { 0, 0 };
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const Voice& v = voices[i];
            if (!v.active) continue;
            ++act; mask |= 1u << i;
            a = std::max(a, (float)v.aEnv.level()); f = std::max(f, (float)v.fEnv.level());
            if (v.aEnv.state() != synth::Adsr::Release) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63);
        }
        activeFeed.store(act); ampFeed.store(a); fenvFeed.store(f); maskFeed.store(mask);
        litFeed[0].store(lit[0]); litFeed[1].store(lit[1]);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiNote(uint8_t n, uint8_t v) { synth::Midi m { 0, (uint8_t)(v ? 0x90 : 0x80), n, v }; handle(m); }

private:
    struct Voice {
        bool active = false;
        int note = 60;
        double vel = 1.0, pitch = 60.0, target = 60.0, age = 0.0;
        double phA = 0.0, phB = 0.0, sub = 1.0, detune = 0.0, panL = 0.7071, panR = 0.7071;
        double slopPitch = 0.0, slopCut = 0.0;
        double incA = 0.0, incB = 0.0, pwA = 0.5, pwB = 0.5, cutBase = 1000.0, lfoAmt = 0.0, envNow = 0.0;
        synth::Adsr fEnv, aEnv;
        synth::Ladder ladder;
        fx::Svf svf;
        fx::OnePole hp1, hp2;
        void resetFilters() { ladder.reset(); svf.reset(); hp1.reset(); hp2.reset(); }
    };

    int unisonCount() const { return clampv(set.voices, 1, kMaxVoices); }
    void configure()
    {
        if (set.voiceMode == kModePoly) vm.setup(clampv(set.voices, 1, kMaxVoices), synth::kPoly);
        else vm.setup(1, synth::kLegato);
        vm.reset();
        // unison: the voices spread in pitch (-1 .. 1 of the detune) and across the stereo field
        const int u = set.voiceMode == kModeUnison ? unisonCount() : 1;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[i];
            const double x = u > 1 ? 2.0 * (i % u) / (u - 1) - 1.0 : 0.0;
            v.detune = set.voiceMode == kModeUnison ? x : 0.0;
            const double pan = set.voiceMode == kModeUnison ? 0.7 * x : 0.0;
            v.panL = std::cos(0.25 * kPi * (pan + 1.0)); v.panR = std::sin(0.25 * kPi * (pan + 1.0));
        }
    }

    void handle(const synth::Midi& m)
    {
        vm.midi(m.status, m.d1, m.d2, [&](const synth::VoiceCmd& c) {
            // unison: the one managed voice drives all of them
            const int first = set.voiceMode == kModeUnison ? 0 : c.voice;
            const int last = set.voiceMode == kModeUnison ? unisonCount() - 1 : c.voice;
            for (int vi = first; vi <= last; ++vi)
            {
                Voice& v = voices[vi];
                switch (c.kind)
                {
                case synth::VoiceCmd::Start:
                {
                    const bool was = v.active;
                    v.active = true; v.note = c.note; v.target = c.note;
                    if (c.velocity >= 0.0f) v.vel = c.velocity;
                    if (!(set.glide > 0.0 && was)) v.pitch = c.note;
                    if (c.velocity >= 0.0f) { v.age = 0.0; if (!was) { v.resetFilters(); v.phA = set.voiceMode == kModeUnison ? rng.uni() : 0.0; v.phB = v.phA; } }
                    v.fEnv.gate(true); v.aEnv.gate(true);
                    break;
                }
                case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; break;
                case synth::VoiceCmd::Release: v.fEnv.gate(false); v.aEnv.gate(false); break;
                case synth::VoiceCmd::Kill: v.fEnv.kill(); v.aEnv.kill(); v.active = false; break;
                }
            }
        });
    }

    void control()
    {
        const Settings& s = set;
        // the LFO (global)
        lfoPh += s.lfoRate * kCtl / sr;
        if (lfoPh >= 1.0) { lfoPh -= std::floor(lfoPh); lfoHeld = rng.bi(); }
        switch (s.lfoShape)
        {
        case kLfoSaw: lfo = 2.0 * lfoPh - 1.0; break;
        case kLfoSquare: lfo = lfoPh < 0.5 ? 1.0 : -1.0; break;
        case kLfoSh: lfo = lfoHeld; break;
        default: lfo = 1.0 - 4.0 * std::fabs(lfoPh - 0.5); break;
        }
        lfoFeed.store((float)lfo, std::memory_order_relaxed);
        const double wheelAdd = vm.wheel() * s.wheelMod;
        const double bend = vm.pitchBend() * s.bendRange;
        const double glideK = s.glide > 0.0 ? 1.0 - std::exp(-(double)kCtl / (s.glide * 0.001 * sr * 0.4)) : 1.0;
        double cutShow = 0.0;
        for (auto& v : voices)
        {
            if (!v.active) continue;
            v.fEnv.set(s.fA, s.fD, s.fS, s.fR);
            v.aEnv.set(s.aA, s.aD, s.aS, s.aR);
            v.pitch += (v.target - v.pitch) * glideK;
            v.age += kCtl / sr;
            // the LFO fades in over DELAY after the note starts; the wheel adds on top
            const double fade = s.lfoDelay > 0.0 ? clampv(v.age / (s.lfoDelay * 0.001), 0.0, 1.0) : 1.0;
            v.lfoAmt = fade + wheelAdd;
            const double slopC = s.slop * 0.08 * v.slopPitch;   // up to +-8 cents
            const double base = v.pitch + bend + slopC + v.detune * s.unisonDetune * 0.5 + lfo * v.lfoAmt * s.lfoPitch * s.lfoPitch * 7.0;
            v.incA = synth::midiToHz(clampv(base + s.aFreq, -24.0, 140.0)) / sr;
            v.incB = s.bLow ? 2.0 * std::exp2(s.bFreq / 12.0) / sr : synth::midiToHz(clampv(base + s.bFreq + s.bFine * 0.01, -24.0, 140.0)) / sr;
            v.pwA = clampv(s.aPw + lfo * v.lfoAmt * s.lfoPw * 0.4, 0.05, 0.95);
            v.pwB = clampv(s.bPw + lfo * v.lfoAmt * s.lfoPw * 0.4, 0.05, 0.95);
            v.envNow = v.fEnv.level();
            const double oct = std::log2(s.cutoff) + 7.0 * s.filterEnv * v.envNow * (1.0 - s.velFilter + s.velFilter * v.vel)
                             + s.keyTrack * (v.pitch - 60.0) / 12.0 + 4.0 * s.lfoFilter * lfo * v.lfoAmt + s.slop * 0.15 * v.slopCut;
            v.cutBase = std::exp2(oct);
            setFilter(v, v.cutBase);
            if (hpfActive(s.hpf)) { v.hp1.set(s.hpf, sr); v.hp2.set(s.hpf, sr); }
            cutShow = v.cutBase;
        }
        if (cutShow > 0.0) cutFeed.store((float)cutShow, std::memory_order_relaxed);
        // chorus rates
        static const double rates[4] = { 0.0, 0.5, 0.83, 8.0 };
        chInc = rates[clampv(s.chorus, 0, 3)] / sr;
    }
    inline void setFilter(Voice& v, double fc)
    {
        fc = clampv(fc, 15.0, 0.45 * sr);
        if (set.filterType == kOb12) v.svf.set(fc, 0.5 + 4.5 * set.res * set.res, sr);
        else v.ladder.set(fc, set.res * (set.filterType == kFive24 ? 0.97 : 1.0), sr);
    }

    inline double voiceSample(Voice& v)
    {
        const Settings& s = set;
        const synth::BandWaves& bw = synth::BandWaves::get();
        // oscillator B first (it may sync A and poly-modulate)
        double b = 0.0;
        const int mb = synth::BandWaves::mipFor(v.incB);
        if (s.bSaw) b += bw.saw(v.phB, mb);
        if (s.bTri) b += bw.tri(v.phB, mb);
        if (s.bPulse) b += bw.pulse(v.phB, v.pwB, mb);
        const double bMod = s.bSaw || s.bTri || s.bPulse ? b : 0.0;
        // poly-mod amount now
        const double pm = s.pmEnv * v.envNow + s.pmOscB * bMod;
        double incA = v.incA;
        if (s.pmFreqA && pm != 0.0) incA *= std::exp2(pm * 3.0);   // up to +-3 octaves
        incA = clampv(incA, 0.0, 0.45);
        const double pwA = s.pmPwA ? clampv(v.pwA + pm * 0.45, 0.03, 0.97) : v.pwA;
        double a = 0.0;
        const int ma = synth::BandWaves::mipFor(incA);
        if (s.aSaw) a += bw.saw(v.phA, ma);
        if (s.aPulse) a += bw.pulse(v.phA, pwA, ma);
        // advance
        const double prevA = v.phA;
        v.phA += incA;
        if (v.phA >= 1.0) { v.phA -= std::floor(v.phA); v.sub = -v.sub; }
        v.phB += v.incB;
        if (v.phB >= 1.0)
        {
            v.phB -= std::floor(v.phB);
            if (s.sync) v.phA = v.phB * incA / std::max(1e-9, v.incB);   // A restarts with B
        }
        (void)prevA;
        double mix = s.levelA * a + s.levelB * b + s.sub * v.sub * 0.8 + s.noise * rng.bi();
        // high-pass
        if (hpfActive(s.hpf)) mix = v.hp2.hp(v.hp1.hp(mix));
        // poly-mod to the filter, at audio rate
        if (s.pmFilter && pm != 0.0) setFilter(v, v.cutBase * std::exp2(pm * 4.0));
        double f;
        switch (s.filterType)
        {
        case kFive24: f = v.ladder.process(kIn * mix, 0, 1.0) * kOut * (1.0 + 0.45 * s.res); break;
        case kEight24: f = v.ladder.process(kIn * mix, 0, 1.0) * kOut; break;
        case kEight12: f = v.ladder.process(kIn * mix, 1, 1.0) * kOut; break;
        default: f = v.svf.lowpass(std::tanh(kIn * mix)) * kOut; break;
        }
        const double amp = v.aEnv.next() * (1.0 - s.velAmp + s.velAmp * v.vel);
        v.fEnv.next();
        return f * amp;
    }

    // ensemble chorus: two bucket-brigade-like lines, swept in opposition by one triangle
    void chorus(double* x)
    {
        const double m = chMix.next();
        if (m <= 0.0 && set.chorus == kChorusOff) { chLine[0].push((float)x[0]); chLine[1].push((float)x[1]); return; }
        chPh += chInc; if (chPh >= 1.0) chPh -= 1.0;
        const double tri = 1.0 - 4.0 * std::fabs(chPh - 0.5);
        const bool fast = set.chorus == kChorusBoth;
        const double base = (fast ? 3.2 : 3.5) * 0.001 * sr, depth = (fast ? 0.3 : 1.85) * 0.001 * sr;
        const double mono = 0.5 * (x[0] + x[1]);
        chLine[0].push((float)mono);
        chLine[1].push((float)mono);
        const double wl = chLine[0].read(base + depth * tri), wr = chLine[1].read(base - depth * tri);
        x[0] = x[0] + m * (wl - 0.5 * x[0]) * 0.9;
        x[1] = x[1] + m * (wr - 0.5 * x[1]) * 0.9;
    }

    double sr = 48000.0;
    Settings set;
    synth::VoiceManager vm;
    Voice voices[kMaxVoices];
    double lfoPh = 0.0, lfoHeld = 0.0, lfo = 0.0;
    fx::FracDelay chLine[2];
    double chPh = 0.0, chInc = 0.0;
    LinearSmoother chMix, volS;
    fx::Rng rng { 0x9017u };
    unsigned ctl = 0;
    std::atomic<int> activeFeed { 0 };
    std::atomic<float> cutFeed { 2200.0f }, ampFeed { 0.0f }, fenvFeed { 0.0f }, lfoFeed { 0.0f };
    std::atomic<uint32_t> maskFeed { 0 };
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
};

}} // namespace pump::poly
