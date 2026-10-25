// pump TABLE engine: a polyphonic wavetable synth. Framework-free.
//
// Per voice: two wavetable oscillators (A, B), each with up to 7 unison copies spread in pitch (DETUNE, up to
// +-50 cents) and across the stereo field (SPREAD), a sine sub an octave under A, and noise; a filter per side
// (ladder low-pass 24 / 12 dB with drive, or state-variable band / high-pass) moved by the mod envelope (ENV) and
// the key (KEY); an amp envelope; LFO 1 (restarts with each note). LFO 2 is shared by all voices (free, or locked
// to the host's song position). Four mod slots route LFO 1 / 2, the mod envelope, velocity, wheel, aftertouch, key
// or a per-note random value to wavetable positions, pitch, cutoff, resonance, levels, pan, detune or LFO 1's rate.
// Modulation and filter coefficients update every 16 samples; oscillators read band-limited mips chosen by pitch
// (no aliasing). Voice allocation (poly up to 16 with stealing, mono, legato with glide) is synth::VoiceManager.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Tempo.hpp"
#include "TableParams.hpp"
#include "Wavetables.hpp"

namespace pump { namespace table {

struct OscSettings { int table = 0, voices = 1, oct = 0, semi = 0; double pos = 0.4, detune = 0.2, spread = 0.6, fine = 0.0, level = 0.8, pan = 0.0; };
struct ModSlot { int src = kSrcOff, dst = 0; double amt = 0.0; };
struct Settings {
    OscSettings osc[2];
    double sub = 0.0, noise = 0.0;
    int filterType = kLp24;
    double cutoff = 6000.0, res = 0.15, driveDb = 0.0, filterEnv = 0.25, keyTrack = 0.5;
    double ampA = 0.003, ampD = 0.4, ampS = 0.8, ampR = 0.3, velAmp = 0.6;
    double modA = 0.005, modD = 0.6, modS = 0.2, modR = 0.4;
    int lfo1Shape = kSine, lfo2Shape = kTri;
    double lfo1Rate = 2.0, lfo2Rate = 0.3;
    bool lfo2Sync = false;
    int lfo2Div = kNote1;
    ModSlot mod[4];
    int voiceMode = synth::kPoly, polyphony = 8;
    double glide = 0.0, bendRange = 2.0, volumeDb = -6.0;
};

// LFO shape at phase p (0..1); held is the S&H / drift state
inline double lfoShape(int shape, double p, double held)
{
    switch (shape)
    {
    case kTri: return 1.0 - 4.0 * std::fabs(p - 0.5);
    case kSawUp: return 2.0 * p - 1.0;
    case kSawDown: return 1.0 - 2.0 * p;
    case kSquare: return p < 0.5 ? 1.0 : -1.0;
    case kSampleHold: case kSmoothRandom: return held;
    default: return std::sin(kTwoPi * p);
    }
}

class Engine {
public:
    static constexpr int kMaxVoices = 16, kMaxUnison = 7, kCtl = 16;
    static constexpr double kIn = 0.1, kOut = 10.0;   // the filter sees the oscillators at -20 dB (see render)

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& v : voices) { v = Voice(); v.amp.setSampleRate(sr); v.mod.setSampleRate(sr); }
        vm.setup(set.polyphony, set.voiceMode);
        vm.reset();
        lfo2Phase = 0.0; lfo2Held = 0.0; lfo2Target = 0.0; lfo2Prev = 0.0;
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        factory();   // build the shared tables now (not on the audio thread)
    }
    void setSettings(const Settings& s)
    {
        const bool modeChange = s.voiceMode != set.voiceMode || s.polyphony != set.polyphony;
        set = s;
        if (modeChange)
        {
            for (auto& v : voices) { v.active = false; v.amp.kill(); v.mod.kill(); }
            vm.setup(clampv(set.polyphony, 1, kMaxVoices), set.voiceMode);
            vm.reset();
        }
    }
    void setTransport(const Transport& t) { tr = t; }
    // a user table for oscillator o (kept alive by the caller; nullptr = none)
    void setUserTable(int o, const Table* t) { user[o & 1].store(t, std::memory_order_release); }

    // ---- feeds (UI) ----
    float posNow(int o) const { return posFeed[o & 1].load(std::memory_order_relaxed); }
    float cutoffNow() const { return cutFeed.load(std::memory_order_relaxed); }
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    float ampEnvNow() const { return ampFeed.load(std::memory_order_relaxed); }
    float modEnvNow() const { return modFeed.load(std::memory_order_relaxed); }
    ScopeRing outRing { 1 << 13 };
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }

    // events must be sorted by offset (offset < n)
    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        std::memset(L, 0, sizeof(float) * (size_t)n);
        std::memset(R, 0, sizeof(float) * (size_t)n);
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0, pos = 0;
        while (pos < n)
        {
            while (e < nev && (int)ev[e].offset <= pos) { handle(ev[e]); ++e; }
            int end = n;
            if (e < nev) end = std::min(n, std::max(pos + 1, (int)ev[e].offset));
            render(L + pos, R + pos, end - pos);
            pos = end;
        }
        while (e < nev) handle(ev[e++]);
        int act = 0; float a = 0.0f, m = 0.0f;
        uint64_t lit[2] = { 0, 0 };
        for (auto& v : voices) if (v.active) { ++act; a = std::max(a, (float)v.amp.level()); m = std::max(m, (float)v.mod.level()); if (v.amp.state() != synth::Adsr::Release) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63); }
        litFeed[0].store(lit[0], std::memory_order_relaxed); litFeed[1].store(lit[1], std::memory_order_relaxed);
        activeFeed.store(act, std::memory_order_relaxed);
        ampFeed.store(a, std::memory_order_relaxed);
        modFeed.store(m, std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    // a note from the window's keyboard (audio thread, before process)
    void uiNote(uint8_t note, uint8_t vel) { synth::Midi m { 0, (uint8_t)(vel ? 0x90 : 0x80), note, vel }; handle(m); }

private:
    struct Voice {
        bool active = false;
        int note = 60;
        double vel = 1.0, pitch = 60.0, pitchTarget = 60.0, rnd = 0.0;
        double ph[2][kMaxUnison] = {}, subPh = 0.0;
        synth::Adsr amp, mod;
        double lfo1Ph = 0.0, lfo1Held = 0.0, lfo1Target = 0.0;
        synth::Ladder ladder[2];
        fx::Svf svf[2];
        // control-rate state
        double inc[2][kMaxUnison] = {}, gainL[2][kMaxUnison] = {}, gainR[2][kMaxUnison] = {}, pos[2] = { 0, 0 }, subInc = 0.0;
        int mip[2] = { 0, 0 }, nUni[2] = { 1, 1 };
        double lvl[2] = { 1, 1 }, panG[2] = { 1, 1 }, drive = 1.0;
        int ctl = 0;
        fx::Rng rng;
    };

    const Table* tableFor(int o) const
    {
        const int t = set.osc[o].table;
        if (t == kUserTable) { const Table* u = user[o].load(std::memory_order_acquire); return u ? u : factory()[0].get(); }
        return factory()[(size_t)clampv(t, 0, kNumFactory - 1)].get();
    }

    void handle(const synth::Midi& m)
    {
        vm.midi(m.status, m.d1, m.d2, [&](const synth::VoiceCmd& c) {
            if (c.voice < 0 || c.voice >= kMaxVoices) return;
            Voice& v = voices[c.voice];
            switch (c.kind)
            {
            case synth::VoiceCmd::Start:
            {
                const bool wasActive = v.active;
                v.active = true;
                v.note = c.note;
                if (c.velocity >= 0.0f) v.vel = c.velocity;
                v.pitchTarget = c.note;
                // glide from the pitch the voice is sounding (an overlapping mono / legato note, or a stolen
                // voice still ringing), else start on the note
                if (!(set.glide > 0.0 && wasActive)) v.pitch = c.note;
                if (c.velocity >= 0.0f)
                {
                    v.rnd = rng.bi();
                    v.rng.seed(rng.next());
                    for (int o = 0; o < 2; ++o)
                        for (int k = 0; k < kMaxUnison; ++k) v.ph[o][k] = set.osc[o].voices > 1 ? v.rng.uni() : 0.0;
                    v.subPh = 0.0;
                    v.lfo1Ph = 0.0; v.lfo1Held = v.rng.bi(); v.lfo1Target = v.rng.bi();
                    if (!wasActive) { for (auto& l : v.ladder) l.reset(); for (auto& f : v.svf) f.reset(); }
                }
                v.amp.gate(true); v.mod.gate(true);
                v.ctl = 0;
                break;
            }
            case synth::VoiceCmd::Glide:
                v.pitchTarget = c.note; v.note = c.note;
                break;
            case synth::VoiceCmd::Release:
                v.amp.gate(false); v.mod.gate(false);
                break;
            case synth::VoiceCmd::Kill:
                v.amp.kill(); v.mod.kill(); v.active = false; vm.voiceFinished(c.voice);
                break;
            }
        });
    }

    // the mod sources for a voice now (LFO 2 is global)
    double source(const Voice& v, int s, double lfo1, double lfo2) const
    {
        switch (s)
        {
        case kSrcLfo1: return lfo1;
        case kSrcLfo2: return lfo2;
        case kSrcModEnv: return v.mod.level();
        case kSrcVelocity: return v.vel;
        case kSrcWheel: return vm.wheel();
        case kSrcAftertouch: return vm.aftertouch();
        case kSrcKey: return (v.note - 60) / 60.0;
        case kSrcRandom: return v.rnd;
        default: return 0.0;
        }
    }

    // LFO 2: free, or locked to the song position
    double lfo2Advance(int n)
    {
        const double bpm = clampv(tr.bpm, 20.0, 999.0);
        double inc;
        if (set.lfo2Sync)
        {
            const double q = noteQuarters(set.lfo2Div);
            inc = bpm / 60.0 / q / sr;
            if (tr.playing && tr.valid && std::isfinite(tr.ppq) && std::fabs(tr.ppq) < 1e9 && n == 0)
            {
                const double want = tr.ppq / q - std::floor(tr.ppq / q);
                double d = want - lfo2Phase; d -= std::round(d);
                lfo2Phase += std::fabs(d) > 0.05 ? d : 0.1 * d;
            }
        }
        else inc = set.lfo2Rate / sr;
        const double before = lfo2Phase;
        lfo2Phase += inc * kCtl;
        if (lfo2Phase >= 1.0)
        {
            lfo2Phase -= std::floor(lfo2Phase);
            lfo2Prev = lfo2Target; lfo2Target = rng.bi();
            if (set.lfo2Shape == kSampleHold) lfo2Held = lfo2Target;
        }
        if (set.lfo2Shape == kSmoothRandom) { const double c = 0.5 - 0.5 * std::cos(kPi * lfo2Phase); lfo2Held = lfo2Prev + (lfo2Target - lfo2Prev) * c; }
        (void)before;
        return lfoShape(set.lfo2Shape, lfo2Phase, lfo2Held);
    }

    void control(Voice& v, double lfo2)
    {
        const Settings& s = set;
        v.amp.set(s.ampA, s.ampD, s.ampS, s.ampR);
        v.mod.set(s.modA, s.modD, s.modS, s.modR);
        // glide (a constant time per semitone-ish: exponential approach)
        if (s.glide > 0.0) { const double k = 1.0 - std::exp(-(double)kCtl / (s.glide * 0.001 * sr * 0.4)); v.pitch += (v.pitchTarget - v.pitch) * k; }
        else v.pitch = v.pitchTarget;
        // LFO 1 (rate may be modulated)
        double lfo1Rate = s.lfo1Rate;
        double modv[kNumDests] = {};
        const double lfo1Now = lfoShape(s.lfo1Shape, v.lfo1Ph, v.lfo1Held);
        for (const ModSlot& m : s.mod)
            if (m.src != kSrcOff && m.amt != 0.0) modv[clampv(m.dst, 0, kNumDests - 1)] += m.amt * source(v, m.src, lfo1Now, lfo2);
        lfo1Rate *= std::exp2(3.0 * modv[kDstLfo1Rate]);
        v.lfo1Ph += lfo1Rate * kCtl / sr;
        if (v.lfo1Ph >= 1.0)
        {
            v.lfo1Ph -= std::floor(v.lfo1Ph);
            const double prev = v.lfo1Target; v.lfo1Target = v.rng.bi();
            if (s.lfo1Shape == kSampleHold) v.lfo1Held = v.lfo1Target;
            else if (s.lfo1Shape == kSmoothRandom) v.lfo1Held = prev;
        }
        if (s.lfo1Shape == kSmoothRandom) { /* glide toward the target over the cycle */ v.lfo1Held += (v.lfo1Target - v.lfo1Held) * std::min(1.0, 2.0 * lfo1Rate * kCtl / sr * 3.0); }

        const double bend = vm.pitchBend() * s.bendRange;
        for (int o = 0; o < 2; ++o)
        {
            const OscSettings& os = s.osc[o];
            const Table* t = tableFor(o);
            const double basePitch = v.pitch + os.oct * 12 + os.semi + os.fine * 0.01 + bend + 24.0 * modv[kDstPitch];
            const int nu = clampv(os.voices, 1, kMaxUnison);
            v.nUni[o] = nu;
            const double det = clampv(os.detune + modv[kDstDetune], 0.0, 1.0) * 0.5;   // semitones at the edge
            const double pan = clampv(os.pan + modv[kDstPan], -1.0, 1.0);
            const double g = 1.0 / std::sqrt((double)nu);
            double maxInc = 0.0;
            for (int k = 0; k < nu; ++k)
            {
                const double x = nu == 1 ? 0.0 : (2.0 * k / (nu - 1) - 1.0);       // -1 .. 1 across the copies
                const double cents = det * x * (0.6 + 0.4 * std::fabs(x));
                v.inc[o][k] = midiToHzSafe(basePitch + cents) / sr;
                maxInc = std::max(maxInc, v.inc[o][k]);
                const double pp = clampv(pan + x * os.spread, -1.0, 1.0);
                v.gainL[o][k] = g * std::cos(0.25 * kPi * (pp + 1.0));
                v.gainR[o][k] = g * std::sin(0.25 * kPi * (pp + 1.0));
            }
            v.mip[o] = Table::mipFor(maxInc);
            v.pos[o] = clampv(os.pos + (o == 0 ? modv[kDstPosA] : modv[kDstPosB]), 0.0, 1.0);
            v.lvl[o] = os.level * clampv(1.0 + (o == 0 ? modv[kDstLevelA] : modv[kDstLevelB]), 0.0, 2.0);
            (void)t;
        }
        v.subInc = midiToHzSafe(v.pitch + s.osc[0].oct * 12 + s.osc[0].semi - 12.0 + bend + 24.0 * modv[kDstPitch]) / sr;
        // filter
        const double cutOct = std::log2(s.cutoff) + 6.0 * s.filterEnv * v.mod.level() + s.keyTrack * (v.pitch - 60.0) / 12.0 + 6.0 * modv[kDstCutoff];
        const double fc = clampv(std::exp2(cutOct), 20.0, 0.45 * sr);
        const double res = clampv(s.res + modv[kDstRes], 0.0, 1.0);
        v.drive = dbToGain(s.driveDb);
        if (s.filterType <= kLp12) for (auto& l : v.ladder) l.set(fc, res, sr);
        else for (auto& f : v.svf) f.set(fc, 0.5 + 12.0 * res * res, sr);
        if (&v == lastVoice) { posFeed[0].store((float)v.pos[0], std::memory_order_relaxed); posFeed[1].store((float)v.pos[1], std::memory_order_relaxed); cutFeed.store((float)fc, std::memory_order_relaxed); }
    }
    static double midiToHzSafe(double n) { return synth::midiToHz(clampv(n, -24.0, 150.0)); }

    void render(float* L, float* R, int n)
    {
        // the most recent active voice feeds the window
        lastVoice = nullptr;
        for (auto& v : voices) if (v.active) lastVoice = &v;
        const Table* tb[2] = { tableFor(0), tableFor(1) };
        for (int i = 0; i < n; ++i)
        {
            if ((ctlCount++ % kCtl) == 0)
            {
                lfo2Now = lfo2Advance(i);
                for (auto& v : voices) if (v.active) control(v, lfo2Now);
            }
            const double vol = volS.next() * 0.35;
            double outL = 0.0, outR = 0.0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[vi];
                if (!v.active) continue;
                double l = 0.0, r = 0.0;
                for (int o = 0; o < 2; ++o)
                {
                    if (v.lvl[o] <= 0.0) { for (int k = 0; k < v.nUni[o]; ++k) { v.ph[o][k] += v.inc[o][k]; v.ph[o][k] -= std::floor(v.ph[o][k]); } continue; }
                    double sl = 0.0, sr2 = 0.0;
                    for (int k = 0; k < v.nUni[o]; ++k)
                    {
                        const double x = tb[o]->read(v.ph[o][k], v.pos[o], v.mip[o]);
                        sl += x * v.gainL[o][k]; sr2 += x * v.gainR[o][k];
                        v.ph[o][k] += v.inc[o][k];
                        if (v.ph[o][k] >= 1.0) v.ph[o][k] -= 1.0;
                    }
                    l += sl * v.lvl[o]; r += sr2 * v.lvl[o];
                }
                if (set.sub > 0.0) { const double s = set.sub * std::sin(kTwoPi * v.subPh); l += s * 0.7071; r += s * 0.7071; }
                v.subPh += v.subInc; if (v.subPh >= 1.0) v.subPh -= 1.0;
                if (set.noise > 0.0) { l += set.noise * 0.5 * v.rng.bi(); r += set.noise * 0.5 * v.rng.bi(); }
                // filter
                switch (set.filterType)
                {
                // into the filter at a lower level (its saturation only bites when DRIVE pushes it), made up after
                case kLp24: l = v.ladder[0].process(kIn * l, 0, v.drive) * kOut; r = v.ladder[1].process(kIn * r, 0, v.drive) * kOut; break;
                case kLp12: l = v.ladder[0].process(kIn * l, 1, v.drive) * kOut; r = v.ladder[1].process(kIn * r, 1, v.drive) * kOut; break;
                case kBp: l = v.svf[0].bandpass(std::tanh(v.drive * kIn * l)) * kOut; r = v.svf[1].bandpass(std::tanh(v.drive * kIn * r)) * kOut; break;
                default: l = v.svf[0].highpass(std::tanh(v.drive * kIn * l)) * kOut; r = v.svf[1].highpass(std::tanh(v.drive * kIn * r)) * kOut; break;
                }
                const double a = v.amp.next() * (1.0 - set.velAmp + set.velAmp * v.vel);
                v.mod.next();
                outL += l * a; outR += r * a;
                if (!v.amp.active()) { v.active = false; vm.voiceFinished(vi); }
            }
            L[i] += (float)(outL * vol);
            R[i] += (float)(outR * vol);
        }
        // runaway filter / NaN guard
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk))
        {
            for (auto& v : voices) { for (auto& l : v.ladder) l.reset(); for (auto& f : v.svf) f.reset(); }
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
    }

    double sr = 48000.0;
    Settings set;
    Transport tr;
    synth::VoiceManager vm;
    Voice voices[kMaxVoices];
    Voice* lastVoice = nullptr;
    std::atomic<const Table*> user[2] { { nullptr }, { nullptr } };
    double lfo2Phase = 0.0, lfo2Held = 0.0, lfo2Target = 0.0, lfo2Prev = 0.0, lfo2Now = 0.0;
    unsigned ctlCount = 0;
    fx::Rng rng { 0x7AB1Eu };
    LinearSmoother volS;
    std::atomic<float> posFeed[2] { { 0.4f }, { 0.3f } }, cutFeed { 6000.0f }, ampFeed { 0.0f }, modFeed { 0.0f };
    std::atomic<int> activeFeed { 0 };
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
};

}} // namespace pump::table
