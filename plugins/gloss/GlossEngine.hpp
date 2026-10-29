// pump GLOSS engine: a polyphonic synth of seven machines for hyperpop sounds. Framework-free.
//
// Voices (1 to 8, or mono / legato with glide) run at twice the host rate: the machines (FM, hard sync, folding,
// phase distortion, chip waves) are full of harmonics. Each voice: its machine (see GlossParams.hpp), a pitch
// envelope (PITCH ENV semitones decaying in P.TIME: the boings, drops and squeaks), a state-variable filter (low-,
// band- or high-pass) with its own decaying envelope and key tracking, an ADSR. The voices are summed and brought
// down by a half-band filter; then the kit of the genre: DRIVE (soft, hard clip or wavefold), CRUSH (bits and a
// sample-and-hold rate) and WIDTH (two slowly moving short delays of the middle, added as side). One LFO goes to the
// pitch, the cutoff, control A or B, or the level.
//
// The machines' insides:
//   Rubber  a sine carrier phase-modulated by a sine modulator with feedback; the ratio snaps to whole and half
//           ratios within 8 %; BOUNCE multiplies the index by the pitch envelope, so the timbre falls with the pitch
//   Chip    a 24-bit phase accumulator read as 12-bit waves the way the chip does: the saw is the top bits, the
//           triangle folds them, the pulse compares them, the noise is a 23-bit shift register clocked by bit 19;
//           the combined waves are the bitwise AND of the parts; SYNC runs the accumulator up to eight times the note
//           and restarts it every period of the note; RING multiplies by a triangle a fifth up
//   Voice   a band-limited saw tilted down into four formant band-passes (a e i o u, interpolated), shifted by SIZE;
//           BREATH mixes in noise; MOVE bends the vowel with the pitch envelope
//   Super   seven band-limited saws spread over DETUNE (uneven offsets), the side voices alternately left and right
//   Digi    sixteen single-cycle spectra built once as band-limited tables (an octave each), morphed by WAVE; then
//           the wave's resolution (BITS), a folder and phase distortion (WARP)
//   Pluck   a Karplus-Strong string: a delay line a period long (the loop filters' delays taken out, read with a
//           cubic), a one-pole loss filter and gain set by DAMP, up to four allpasses in the loop for stiffness
//           (STRETCH), excited by a noise burst low-passed by BRIGHT; BODY a second string 3..15 cents apart
//   Bubble  drops: each a sine whose pitch rises by CHIRP octaves as it dies over TIME; WET a band of noise at its
//           pitch; FIZZ spawns more drops at random around the note while the key is held
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Oversampler.hpp"
#include "GlossParams.hpp"
#include <array>
#include <complex>

namespace pump { namespace gloss {

struct Settings {
    int machine = kRubber;
    double a = 0.25, b = 0.4, c = 0.15, d = 0.6;
    double tune = 0.0, pitchEnv = 12.0, pitchTime = 0.06, glide = 0.0;
    int mode = kPoly, filterType = kLp, driveType = kHard, lfoShape = kLfoSine, lfoDest = kToPitch;
    double cutoff = 6000.0, res = 0.2, filterEnv = 0.3, filterDecay = 0.25, keyTrack = 0.5;
    double attack = 0.001, decay = 0.4, sustain = 0.4, release = 0.12, velAmp = 0.5;
    double drive = 0.15, crushBits = 16.0, crushRate = 48000.0, width = 0.3;
    double lfoRate = 5.0, lfoDepth = 0.0, bendRange = 2.0, volumeDb = 0.0;
};

// ---- Digi's waves ---------------------------------------------------------------------------------------------------
class DigiWaves {
public:
    static constexpr int kWaves = 16, kMips = 10, kN = 2048;
    static const DigiWaves& get() { static const DigiWaves w; return w; }
    static int mipFor(double inc)
    {
        // mip m keeps harmonics up to 512 >> m
        const double maxH = 0.5 / std::max(1e-9, inc);
        int m = 0;
        while (m < kMips - 1 && (512 >> m) > maxH) ++m;
        return m;
    }
    inline double read(int w, int m, double p) const
    {
        const float* t = tab[(size_t)(w * kMips + m)].data();
        const double x = p * kN;
        const int i = (int)x;
        const double f = x - i;
        return t[i] + (t[i + 1] - t[i]) * f;
    }
private:
    std::vector<float> tab[kWaves * kMips];
    static double amp(int w, int h, fx::Rng& r)
    {
        switch (w)
        {
        case 0: return h == 1 ? 1.0 : 0.0;                                         // sine
        case 1: return h == 1 ? 1.0 : (h == 2 ? 0.5 : (h == 4 ? 0.35 : 0.0));       // sine and octaves
        case 2: return (h % 2) ? 1.0 / h : 0.0;                                     // square
        case 3: return 1.0 / h;                                                     // saw
        case 4: return (h == 1 || h == 2 || h == 3 || h == 4 || h == 6 || h == 8) ? 0.8 / std::sqrt((double)h) : 0.0;   // organ
        case 5: return std::exp(-0.5 * std::pow((h - 7.0) / 2.2, 2.0)) + 0.4 / h;   // a formant around the 7th
        case 6: return (h == 1 ? 1.0 : (h == 5 || h == 9 || h == 13 ? 0.6 : 0.0)); // glassy odd partials
        case 7: return (h % 2) ? 1.0 / (h * h) : 0.0;                               // hollow
        case 8: return std::fabs(std::sin(kPi * h * 0.25)) / h;                     // pulse 25 %
        case 9: return std::fabs(std::sin(kPi * h * 0.125)) / h;                    // pulse 12.5 %
        case 10: case 11: return r.uni() / std::sqrt((double)h) * (h < 40 ? 1.0 : 0.0);   // fixed random spectra
        case 12: return std::fabs(std::sin(h * 0.9)) / std::sqrt((double)h);        // sync-like ripple
        case 13: return (h == 4 || h == 5 || h == 6) ? 0.8 : (h == 1 ? 0.5 : 0.0);  // a major triad of partials
        case 14: return h <= 40 ? 0.15 : 0.0;                                       // buzz
        default: return (h >= 8 && h <= 16) ? 0.5 / (1.0 + std::fabs(h - 12.0)) : (h == 1 ? 0.3 : 0.0);   // squeak
        }
    }
    DigiWaves()
    {
        for (int w = 0; w < kWaves; ++w)
        {
            fx::Rng r((uint32_t)(77 + w * 1013));
            double a[513] = {};
            for (int h = 1; h <= 512; ++h) a[h] = amp(w, h, r);
            for (int m = 0; m < kMips; ++m)
            {
                const int H = 512 >> m;
                std::vector<double> x((size_t)kN, 0.0);
                for (int h = 1; h <= H; ++h)
                {
                    if (a[h] == 0.0) continue;
                    const double ph = (w >= 10 && w <= 12) ? h * 1.7 : 0.0;   // the random ones get mixed phases
                    for (int i = 0; i < kN; ++i) x[(size_t)i] += a[h] * std::sin(kTwoPi * h * i / kN + ph);
                }
                double pk = 1e-9; for (double v : x) pk = std::max(pk, std::fabs(v));
                std::vector<float>& t = tab[(size_t)(w * kMips + m)];
                t.resize((size_t)kN + 1);
                // normalise each wave by its fullest table, so the mips keep their relative level
                if (m == 0) norm[w] = 0.9 / pk;
                for (int i = 0; i < kN; ++i) t[(size_t)i] = (float)(x[(size_t)i] * norm[w]);
                t[(size_t)kN] = t[0];
            }
        }
    }
    double norm[kWaves] = {};
};

// formants (Hz) and levels of a e i o u
static const double kFormantF[5][4] = { { 800, 1150, 2900, 3900 }, { 400, 2000, 2600, 3300 }, { 270, 2290, 3010, 3500 }, { 450, 800, 2830, 3800 }, { 325, 700, 2530, 3500 } };
static const double kFormantBw[4] = { 80, 90, 120, 130 };
static const double kFormantG[4] = { 1.0, 0.5, 0.28, 0.16 };

class Engine {
public:
    static constexpr int kMaxVoices = 8, kCtl = 16, kMaxDrops = 6;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate; osr = 2.0 * sr;
        (void)DigiWaves::get(); (void)synth::BandWaves::get();
        hbL.setup(tables::kHB1, (int)(sizeof(tables::kHB1) / sizeof(double)));
        hbR.setup(tables::kHB1, (int)(sizeof(tables::kHB1) / sizeof(double)));
        // the strings: a period of the lowest note (about 16 Hz) at the oversampled rate, twice
        const int maxLine = (int)(osr / 15.0) + 8;
        for (auto& v : voices)
        {
            v = Voice();
            v.line[0].assign((size_t)maxLine, 0.0f); v.line[1].assign((size_t)maxLine, 0.0f);
            v.amp.setSampleRate(osr);
        }
        mgr.reset();
        mgr.setup(set.mode == kPoly ? kMaxVoices : 1, set.mode == kPoly ? synth::kPoly : (set.mode == kMono ? synth::kMono : synth::kLegato));
        modeNow = set.mode;
        const int dl = (int)(0.03 * sr) + 4;
        wdl.assign((size_t)dl, 0.0f); wdw = 0;
        crushL.reset(); crushR.reset();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        lfoPh = 0.0; ctl = 0; wph = 0.0;
        dcL.set(10.0, sr); dcR.set(10.0, sr); dcL.reset(); dcR.reset();
    }
    void setSettings(const Settings& s) { set = s; }
    int latency() const { return (hbL.delay() + 1) / 2; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return voicesFeed.load(std::memory_order_relaxed); }
    float pitchEnvNow() const { return penvFeed.load(std::memory_order_relaxed); }
    float ampNow() const { return ampFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && lit[(size_t)n].load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        if (set.mode != modeNow)
        {
            for (auto& v : voices) { v.on = false; v.amp.kill(); }
            mgr.reset();
            mgr.setup(set.mode == kPoly ? kMaxVoices : 1, set.mode == kPoly ? synth::kPoly : (set.mode == kMono ? synth::kMono : synth::kLegato));
            modeNow = set.mode;
        }
        volS.setTarget(dbToGain(set.volumeDb));
        crushL.set(set.crushRate >= 47999.0 ? 1.0 : set.crushRate / sr, set.crushBits >= 15.5 ? 0.0 : set.crushBits);
        crushR.set(set.crushRate >= 47999.0 ? 1.0 : set.crushRate / sr, set.crushBits >= 15.5 ? 0.0 : set.crushBits);
        const double dg = 1.0 + set.drive * set.drive * 30.0, dn = 1.0 / std::sqrt(dg);
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if ((ctl++ % kCtl) == 0) control();
            float yl[2], yr[2];
            for (int k = 0; k < 2; ++k)
            {
                double l = 0.0, r = 0.0;
                for (auto& v : voices) if (v.on) voiceSample(v, l, r);
                // drive at the high rate (its harmonics fold back less)
                if (set.drive > 0.001) { l = shape(l * dg) * dn * 1.4; r = shape(r * dg) * dn * 1.4; }
                yl[k] = (float)l; yr[k] = (float)r;
            }
            double l = hbL.down(yl[0], yl[1]), r = hbR.down(yr[0], yr[1]);
            // crush
            l = crushL.process(l); r = crushR.process(r);
            // width: the middle through two slowly moving short delays, added as side
            const double m = 0.5 * (l + r);
            wdl[(size_t)wdw] = (float)m;
            if (set.width > 0.001)
            {
                wph += 0.13 / sr; if (wph >= 1.0) wph -= 1.0;
                const double d1 = (0.007 + 0.0015 * std::sin(kTwoPi * wph)) * sr, d2 = (0.011 + 0.0015 * std::cos(kTwoPi * wph * 1.3)) * sr;
                const double s = 0.45 * set.width * (readW(d1) - readW(d2));
                l += s; r -= s;
            }
            wdw = (wdw + 1) % (int)wdl.size();
            const double vol = volS.next();
            l = dcL.process(l); r = dcR.process(r);
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
            outRing.push((float)(0.5 * (L[i] + R[i])));
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk))
        {
            for (auto& v : voices) { v.on = false; v.amp.kill(); v.svfL.reset(); v.svfR.reset(); }
            hbL.reset(); hbR.reset(); dcL.reset(); dcR.reset(); std::fill(wdl.begin(), wdl.end(), 0.0f);
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        int c = 0; double pe = 0.0, am = 0.0;
        bool l2[128] = {};
        for (const auto& v : voices) if (v.on) { ++c; pe = std::max(pe, v.penv); am = std::max(am, v.amp.level()); if (v.gate && v.note >= 0 && v.note < 128) l2[v.note] = true; }
        voicesFeed.store(c, std::memory_order_relaxed);
        penvFeed.store((float)pe, std::memory_order_relaxed);
        ampFeed.store((float)am, std::memory_order_relaxed);
        for (int k = 0; k < 128; ++k) lit[(size_t)k].store(l2[k], std::memory_order_relaxed);
    }
    void uiNote(uint8_t n, uint8_t v) { synth::Midi m { 0, (uint8_t)(v ? 0x90 : 0x80), n, v }; handle(m); }

private:
    struct Drop { bool on = false; double ph = 0.0, f = 440.0, env = 1.0, inc = 0.0, a = 0.0; unsigned n = 0; fx::Svf bp; };
    struct Voice {
        bool on = false, gate = false;
        int note = 60;
        double vel = 1.0, pitch = 60.0, target = 60.0, penv = 0.0, fenv = 0.0;
        synth::Adsr amp;
        double ph[9] = {}, mph = 0.0, fbm = 0.0, fbm2 = 0.0, pan[9] = {};
        // chip
        uint32_t acc = 0, master = 0, lfsr = 0x7FFFF8u, ringAcc = 0; uint32_t noiseOut = 0; bool bit19 = false;
        // voice
        fx::Svf form[4]; fx::OnePole tilt;
        // pluck
        std::vector<float> line[2];
        int w[2] = { 0, 0 };
        double lp[2] = { 0, 0 }, ap[2][4] = {}, apIn[2][4] = {}, excite = 0.0, exLp = 0.0;
        double loopG = 0.99, lossP = 0.3, apA = 0.0, loopD = 1.0;
        int nAp = 0;
        unsigned pluckCtl = 0;
        // bubble
        Drop drops[kMaxDrops];
        double fizzT = 0.0;
        // filter
        fx::Svf svfL, svfR;
        fx::Rng rng { 1u };
        // control-rate values
        double inc = 0.001, aN = 0, bN = 0, cN = 0, dN = 0, gain = 1.0;
    };

    // ---- notes ----
    void handle(const synth::Midi& m)
    {
        mgr.midi(m.status, m.d1, m.d2, [&](const synth::VoiceCmd& c) {
            if (c.voice < 0 || c.voice >= kMaxVoices) return;
            Voice& v = voices[(size_t)c.voice];
            switch (c.kind)
            {
            case synth::VoiceCmd::Start: startVoice(v, c.note, c.velocity < 0.0f ? v.vel : (double)c.velocity, c.velocity < 0.0f); break;
            case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; if (c.velocity >= 0.0f) v.vel = c.velocity; break;
            case synth::VoiceCmd::Release: v.gate = false; v.amp.gate(false); break;
            case synth::VoiceCmd::Kill: v.on = false; v.gate = false; v.amp.kill(); mgr.voiceFinished(c.voice); break;
            }
        });
    }
    void startVoice(Voice& v, int note, double vel, bool backToHeld)
    {
        const bool wasOn = v.on && v.amp.active();
        v.note = note; v.target = note; v.vel = vel;
        // glide from where the voice was (mono / legato), else start on the note
        if (!(set.glide > 0.0 && wasOn && set.mode != kPoly)) v.pitch = note;
        if (backToHeld && wasOn) return;   // mono: back to a held key, no new attack
        v.on = true; v.gate = true;
        v.penv = 1.0; v.fenv = 1.0;
        v.amp.set(set.attack, set.decay, set.sustain, set.release);
        v.amp.gate(true);
        v.rng = fx::Rng((uint32_t)(0x6105u + 977u * (uint32_t)(++starts)));
        // machines that start from a fresh state
        if (!wasOn)
        {
            v.svfL.reset(); v.svfR.reset();
            for (int k = 0; k < 9; ++k) { v.ph[k] = v.rng.uni(); v.pan[k] = 0.0; }
            v.mph = 0.0; v.fbm = v.fbm2 = 0.0;
            for (auto& f : v.form) f.reset();
        }
        if (set.machine == kRubber) { v.ph[0] = 0.0; v.mph = 0.0; }
        if (set.machine == kPluck) pluck(v);
        if (set.machine == kBubble) { for (auto& d : v.drops) d.on = false; spawnDrop(v, 0.0, 1.0); v.fizzT = 0.0; }
        gainFor(v);
    }
    void gainFor(Voice& v) { v.gain = 1.0 - set.velAmp * (1.0 - v.vel); }

    // ---- control rate ----
    void control()
    {
        const Settings& s = set;
        const double dt = (double)kCtl / sr;
        // LFO
        lfoPh += s.lfoRate * dt;
        if (lfoPh >= 1.0) { lfoPh -= std::floor(lfoPh); lfoHeld = rng.bi(); }
        switch (s.lfoShape)
        {
        case kLfoTri: lfo = 1.0 - 4.0 * std::fabs(lfoPh - 0.5); break;
        case kLfoSquare: lfo = lfoPh < 0.5 ? 1.0 : -1.0; break;
        case kLfoRamp: lfo = 2.0 * lfoPh - 1.0; break;
        case kLfoSh: lfo = lfoHeld; break;
        default: lfo = std::sin(kTwoPi * lfoPh); break;
        }
        const double ld = s.lfoDepth * lfo;
        const double lfoPitch = s.lfoDest == kToPitch ? ld * 12.0 : 0.0;
        const double lfoCut = s.lfoDest == kToCutoff ? ld * 4.0 : 0.0;
        lfoAmp = s.lfoDest == kToAmp ? 1.0 - s.lfoDepth * (0.5 - 0.5 * lfo) : 1.0;
        const double pK = std::exp(-dt / std::max(0.0005, s.pitchTime / 3.0));   // the pitch envelope: ~95 % gone in P.TIME
        const double fK = std::exp(-dt / std::max(0.0005, s.filterDecay / 3.0));
        const double gK = s.glide > 0.0 ? 1.0 - std::exp(-dt * 3.0 / (s.glide * 0.001)) : 1.0;
        for (auto& v : voices)
        {
            if (!v.on) continue;
            v.pitch += (v.target - v.pitch) * gK;
            const double semis = v.pitch + s.tune + mgr.pitchBend() * s.bendRange + s.pitchEnv * v.penv + lfoPitch;
            v.inc = synth::midiToHz(clampv(semis, -24.0, 150.0)) / osr;
            v.aN = clampv(s.a + (s.lfoDest == kToA ? 0.5 * ld : 0.0), 0.0, 1.0);
            v.bN = clampv(s.b + (s.lfoDest == kToB ? 0.5 * ld : 0.0), 0.0, 1.0);
            v.cN = s.c; v.dN = s.d;
            double cutOct = std::log2(s.cutoff) + s.filterEnv * 7.0 * v.fenv + s.keyTrack * (v.pitch - 60.0) / 12.0 + lfoCut;
            const double fc = clampv(std::exp2(cutOct), 20.0, 0.45 * osr);
            const double q = 0.5 + 19.5 * s.res * s.res;
            v.svfL.set(fc, q, osr); v.svfR.set(fc, q, osr);
            machineControl(v);
            v.penv *= pK; v.fenv *= fK;
            v.amp.set(s.attack, s.decay, s.sustain, s.release);
        }
    }
    void machineControl(Voice& v)
    {
        const double f0 = v.inc * osr;
        if (set.machine == kVoice)
        {
            const double pos = clampv(v.aN * 4.0 + v.dN * 3.0 * (v.penv - 0.5) * (set.pitchEnv >= 0 ? 1.0 : -1.0), 0.0, 4.0);
            const int i0 = std::min(3, (int)pos); const double t = pos - i0;
            const double size = std::exp2((v.bN - 0.35) * 1.6);
            for (int k = 0; k < 4; ++k)
            {
                const double f = (kFormantF[i0][k] * (1.0 - t) + kFormantF[i0 + 1][k] * t) * size;
                v.form[k].set(clampv(f, 60.0, 0.42 * osr), clampv(f / (kFormantBw[k] * size), 1.0, 40.0), osr);
            }
            v.tilt.set(clampv(f0 * 3.0, 100.0, 8000.0), osr);
        }
    }

    // ---- the machines (one sample at the oversampled rate) ----
    void pluck(Voice& v)
    {
        // DAMP: the decay (T60) from 8 s to 0.15 s, and the loss filter's darkness; STRETCH: allpasses in the loop
        const double fN = synth::midiToHz(clampv((double)v.note + set.tune, -24.0, 150.0));
        const double damp = set.a;
        const double t60 = 8.0 * std::pow(0.15 / 8.0, damp);
        v.lossP = 0.05 + 0.6 * damp;
        v.loopG = std::pow(10.0, -3.0 / (t60 * fN));
        v.nAp = set.c > 0.02 ? 1 + (int)(set.c * 3.0) : 0;
        v.apA = -0.15 - 0.6 * set.c;
        for (int s = 0; s < 2; ++s) { std::fill(v.line[s].begin(), v.line[s].end(), 0.0f); v.lp[s] = 0.0; for (int k = 0; k < 4; ++k) v.ap[s][k] = v.apIn[s][k] = 0.0; v.w[s] = 0; }
        v.excite = 1.0; v.exLp = 0.0; v.loopD = 1.0; v.pluckCtl = 0;
        v.ph[8] = 0.0;
    }
    // the loop's own delay at f (the loss filter and the allpasses), in samples
    double loopDelay(const Voice& v, double f) const
    {
        const double w = kTwoPi * f / osr;
        const std::complex<double> z1 = std::polar(1.0, -w);
        // one-pole loss: y = (1 - p) x + p y1
        const std::complex<double> hl = (1.0 - v.lossP) / (1.0 - v.lossP * z1);
        double ph = -std::arg(hl);
        for (int k = 0; k < v.nAp; ++k) ph += -std::arg((v.apA + z1) / (1.0 + v.apA * z1));
        return ph / std::max(1e-9, w);
    }
    inline double readLine(const std::vector<float>& b, int w, double d) const
    {
        const int n = (int)b.size();
        const int di = (int)d; const double f = d - di;
        auto at = [&](int k) { int i = w - k; while (i < 0) i += n; return (double)b[(size_t)i]; };
        const double y0 = at(di - 1), y1 = at(di), y2 = at(di + 1), y3 = at(di + 2);
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    double stringSample(Voice& v, int s, double f, double ex)
    {
        std::vector<float>& b = v.line[s];
        const int n = (int)b.size();
        const double d = clampv(osr / std::max(10.0, f) - v.loopD, 2.0, (double)n - 4.0);
        double y = readLine(b, v.w[s] - 1 < 0 ? n - 1 : v.w[s] - 1, d - 1.0);
        y = (1.0 - v.lossP) * y + v.lossP * v.lp[s]; v.lp[s] = y;
        for (int k = 0; k < v.nAp; ++k) { const double o = v.apA * y + v.apIn[s][k] - v.apA * v.ap[s][k]; v.apIn[s][k] = y; v.ap[s][k] = o; y = o; }
        y *= v.loopG;
        const double in = y + ex;
        b[(size_t)v.w[s]] = (float)clampv(in, -4.0, 4.0);
        v.w[s] = (v.w[s] + 1) % n;
        return in;
    }
    void spawnDrop(Voice& v, double semis, double level)
    {
        for (auto& d : v.drops)
            if (!d.on)
            {
                d.on = true; d.ph = 0.0; d.env = 1.0; d.a = level; d.n = 0;
                d.f = synth::midiToHz((double)v.note + semis);
                d.bp.reset();
                return;
            }
    }
    static inline double fold(double x) { x = x * 0.25 + 0.25; x -= std::floor(x); return 4.0 * std::fabs(x - 0.5) - 1.0; }   // a triangle fold, period 4
    inline double shape(double x) const
    {
        switch (set.driveType)
        {
        case kSoft: return std::tanh(x);
        case kFold: return std::sin(x * 1.2);   // smooth wavefolding
        default: return clampv(x, -1.0, 1.0);
        }
    }

    // (each machine's output gain puts a held C4 at the defaults near -20 dBFS RMS)
    inline void voiceSample(Voice& v, double& outL, double& outR)
    {
        const synth::BandWaves& bw = synth::BandWaves::get();
        const double inc = v.inc;
        double y = 0.0, yl = 0.0, yr = 0.0;
        bool stereo = false;
        switch (set.machine)
        {
        case kRubber:
        {
            double r = 0.5 * std::exp2(v.aN * 4.0);
            const double rr = std::round(r * 2.0) * 0.5;
            if (std::fabs(r - rr) < 0.08 * rr) r = rr;
            const double index = v.bN * v.bN * 10.0 * (1.0 + v.dN * 3.0 * v.penv);
            const double fb = v.cN * 1.4;
            const double m = std::sin(kTwoPi * v.mph + fb * 0.5 * (v.fbm + v.fbm2));
            v.fbm2 = v.fbm; v.fbm = m;
            y = std::sin(kTwoPi * v.ph[0] + index * m);
            v.mph += inc * r; v.mph -= std::floor(v.mph);
            v.ph[0] += inc; if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
            y *= 0.27;
            break;
        }
        case kChip:
        {
            const uint32_t mask = 0xFFFFFFu;
            const uint32_t step = (uint32_t)(inc * 16777216.0);
            const uint32_t slave = (uint32_t)(inc * (1.0 + 7.0 * v.cN) * 16777216.0);
            const uint32_t prevMaster = v.master;
            v.master = (v.master + step) & mask;
            const bool wrap = v.master < prevMaster;
            const uint32_t prevAcc = v.acc;
            v.acc = wrap && v.cN > 0.001 ? 0u : ((v.acc + slave) & mask);
            // the noise: clocked by bit 19 rising
            const bool b19 = (v.acc >> 19) & 1u;
            if (b19 && !v.bit19)
            {
                const uint32_t bit = ((v.lfsr >> 22) ^ (v.lfsr >> 17)) & 1u;
                v.lfsr = ((v.lfsr << 1) | bit) & 0x7FFFFFu;
                // the eight output taps, as the chip picks them
                const uint32_t l = v.lfsr;
                v.noiseOut = (((l >> 22) & 1u) << 11) | (((l >> 20) & 1u) << 10) | (((l >> 16) & 1u) << 9) | (((l >> 13) & 1u) << 8)
                           | (((l >> 11) & 1u) << 7) | (((l >> 7) & 1u) << 6) | (((l >> 4) & 1u) << 5) | (((l >> 2) & 1u) << 4);
            }
            v.bit19 = b19;
            (void)prevAcc;
            const uint32_t a12 = v.acc >> 12;
            const uint32_t saw = a12;
            const uint32_t tri = ((a12 & 0x800u) ? (~a12 & 0x7FFu) : (a12 & 0x7FFu)) << 1;
            const uint32_t pwv = (uint32_t)(clampv(0.03 + 0.94 * v.bN, 0.0, 1.0) * 4095.0);
            const uint32_t pulse = a12 >= pwv ? 0xFFFu : 0u;
            uint32_t val;
            switch (clampv((int)(v.aN * 7.999), 0, 7))
            {
            case 0: val = tri; break;
            case 1: val = saw; break;
            case 2: val = pulse; break;
            case 3: val = v.noiseOut; break;
            case 4: val = tri & saw; break;
            case 5: val = tri & pulse; break;
            case 6: val = saw & pulse; break;
            default: val = tri & saw & pulse; break;
            }
            y = (double)val / 2048.0 - 1.0;
            if (v.dN > 0.001)
            {
                v.ringAcc = (v.ringAcc + (uint32_t)(inc * 1.5 * 16777216.0)) & mask;
                const double rp = v.ringAcc / 16777216.0, rt = 1.0 - 4.0 * std::fabs(rp - 0.5);
                y = y * (1.0 - v.dN) + y * rt * v.dN;
            }
            y *= 0.45;
            break;
        }
        case kVoice:
        {
            const int mip = synth::BandWaves::mipFor(inc);
            const double src = v.tilt.lp(-bw.saw(v.ph[0], mip)) * 2.0;
            v.ph[0] += inc; if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
            const double x = src * (1.0 - v.cN * 0.8) + v.rng.bi() * v.cN * 0.6;
            for (int k = 0; k < 4; ++k) y += v.form[k].bandpass(x) * kFormantG[k];
            y *= 0.7;
            break;
        }
        case kSuper:
        {
            static const double off[7] = { 0.0, -0.11, 0.12, -0.37, 0.33, -0.66, 0.71 };   // uneven, as the classic one
            const double det = std::pow(v.aN, 1.5) * 0.06;   // up to 6 % (about a semitone) at the outside
            const int mip = synth::BandWaves::mipFor(inc * (1.0 + det));
            stereo = true;
            for (int k = 0; k < 7; ++k)
            {
                const double i2 = inc * (1.0 + det * off[k]);
                const double s = -bw.saw(v.ph[k], mip) * (1.0 - v.cN) + bw.pulse(v.ph[k], 0.5, mip) * v.cN;
                v.ph[k] += i2; if (v.ph[k] >= 1.0) v.ph[k] -= 1.0;
                const double g = k == 0 ? 1.0 - 0.6 * v.bN : 0.25 + 0.55 * v.bN;
                if (k == 0) { yl += s * g; yr += s * g; }
                else if (k % 2) { yl += s * g; yr += s * g * 0.35; }
                else { yr += s * g; yl += s * g * 0.35; }
            }
            if (v.dN > 0.001)
            {
                const int m2 = synth::BandWaves::mipFor(inc * 2.0);
                const double s = -bw.saw(v.ph[7], m2) * v.dN * 0.6;
                v.ph[7] += inc * 2.0; if (v.ph[7] >= 1.0) v.ph[7] -= 1.0;
                yl += s; yr += s;
            }
            yl *= 0.16; yr *= 0.16;
            break;
        }
        case kDigi:
        {
            const DigiWaves& dw = DigiWaves::get();
            const double pos = v.aN * (DigiWaves::kWaves - 1);
            const int w0 = std::min(DigiWaves::kWaves - 2, (int)pos); const double t = pos - w0;
            const int mip = DigiWaves::mipFor(inc * (1.0 + 3.0 * v.dN));
            // phase distortion: the first half of the cycle squeezed into less (WARP)
            const double knee = 0.5 - 0.45 * v.dN, p = v.ph[0];
            const double pd = p < knee ? p * 0.5 / knee : 0.5 + (p - knee) * 0.5 / (1.0 - knee);
            y = dw.read(w0, mip, pd) * (1.0 - t) + dw.read(w0 + 1, mip, pd) * t;
            v.ph[0] += inc; if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
            const double bits = 2.0 + v.bN * 14.0;
            if (bits < 15.9) { const double q = std::exp2(bits - 1.0); y = std::round(y * q) / q; }
            if (v.cN > 0.001) y = fold(y * (1.0 + v.cN * 6.0));
            y *= 0.25;
            break;
        }
        case kPluck:
        {
            // the excitation: one period of noise, low-passed by BRIGHT
            double ex = 0.0;
            const double f = inc * osr;
            if (v.excite > 0.0)
            {
                const double k = 1.0 - std::exp(-kTwoPi * 300.0 * std::exp2(v.bN * 5.5) / osr);
                v.exLp += (v.rng.bi() - v.exLp) * k;
                ex = v.exLp * (0.6 + 0.6 * v.bN);
                v.ph[8] += inc;
                if (v.ph[8] >= 1.0) v.excite = 0.0;
            }
            if ((v.pluckCtl++ & 63) == 0) v.loopD = loopDelay(v, f);   // the loop filters' own delay taken out of the line
            y = stringSample(v, 0, f, ex);
            if (v.dN > 0.001) y = y * (1.0 - 0.4 * v.dN) + stringSample(v, 1, f * std::exp2((3.0 + 12.0 * v.dN) / 1200.0), ex) * 0.7 * v.dN;
            y *= 5.4;
            break;
        }
        case kBubble:
        {
            // FIZZ: more drops while the key is held
            if (v.gate && v.dN > 0.001)
            {
                v.fizzT += 1.0 / osr;
                if (v.rng.uni() < v.dN * v.dN * 40.0 / osr) spawnDrop(v, v.rng.bi() * 7.0, 0.3 + 0.5 * v.rng.uni());
            }
            const double tau = 0.005 + 0.2 * v.bN * v.bN;
            const double chirp = v.aN * 3.0;
            const double pm = inc * osr / synth::midiToHz((double)v.note);   // where the voice's pitch is now (tune, glide, envelope, bend, LFO)
            const double envK = std::exp(-1.0 / (tau * osr));
            for (auto& d : v.drops)
            {
                if (!d.on) continue;
                // the drop's pitch (and the noise band) every 16 samples; its level every sample
                if ((d.n & 15) == 0)
                {
                    d.inc = d.f * std::exp2(chirp * (1.0 - d.env)) * pm / osr;
                    if (v.cN > 0.001 && (d.n & 31) == 0) d.bp.set(clampv(d.inc * osr, 40.0, 0.4 * osr), 8.0, osr);
                }
                ++d.n;
                d.ph += d.inc; if (d.ph >= 1.0) d.ph -= 1.0;
                double s = std::sin(kTwoPi * d.ph);
                if (v.cN > 0.001) s += d.bp.bandpass(v.rng.bi()) * v.cN * 1.5;
                y += s * d.env * d.a;
                d.env *= envK;
                if (d.env < 1e-4) d.on = false;
            }
            y *= 0.72;
            break;
        }
        default: break;
        }
        if (!stereo) { yl = y; yr = y; }
        // the filter
        if (set.filterType != kFilterOff)
        {
            double l, b, h;
            v.svfL.process(yl, l, b, h);
            const double ol = set.filterType == kLp ? l : (set.filterType == kBp ? b * v.svfL.k : h);
            if (stereo) { v.svfR.process(yr, l, b, h); yr = set.filterType == kLp ? l : (set.filterType == kBp ? b * v.svfR.k : h); }
            else yr = ol;
            yl = ol;
        }
        const double a = v.amp.next() * v.gain * lfoAmp;
        outL += yl * a; outR += yr * a;
        if (!v.amp.active())
        {
            v.on = false;
            mgr.voiceFinished((int)(&v - voices.data()));
        }
    }
    double readW(double d) const
    {
        const int n = (int)wdl.size();
        const int di = (int)d; const double f = d - di;
        int i0 = wdw - di; while (i0 < 0) i0 += n;
        int i1 = i0 - 1; if (i1 < 0) i1 += n;
        return wdl[(size_t)i0] * (1.0 - f) + wdl[(size_t)i1] * f;
    }

    double sr = 48000.0, osr = 96000.0;
    Settings set;
    synth::VoiceManager mgr;
    std::array<Voice, kMaxVoices> voices;
    HalfbandStage hbL, hbR;
    fx::Crusher crushL, crushR;
    fx::DcBlocker dcL, dcR;
    std::vector<float> wdl;
    int wdw = 0, modeNow = -1;
    double wph = 0.0;
    LinearSmoother volS;
    fx::Rng rng { 0x6105u };
    double lfoPh = 0.0, lfoHeld = 0.0, lfo = 0.0, lfoAmp = 1.0;
    unsigned ctl = 0;
    uint32_t starts = 0;
    std::atomic<int> voicesFeed { 0 };
    std::atomic<float> penvFeed { 0.0f }, ampFeed { 0.0f };
    std::array<std::atomic<bool>, 128> lit {};
};

}} // namespace pump::gloss
