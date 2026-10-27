// pump FOLD engine. Framework-free.
//
// Per voice, at four times the sample rate: the modulation oscillator (sine, triangle, saw or square, at RATIO times
// the note plus DETUNE Hz) frequency modulates the principal one (linear and through zero: the principal's rate is
// f (1 + 6 FM m), so it can run backwards), amplitude modulates it ((1 + AM m) / (1 + AM)) and adds to the folding.
// The principal is a sine, a triangle or a saw (SHAPE morphs between them). The folder: u = x g + SYMMETRY, with g
// from 0.4 to where STAGES folds just fit (FOLD, moved by the function, the random source and the modulator), limited to +-(2 STAGES + 1) so the
// number of folds is bounded, folded back into +-1 by a triangle whose corners EDGE leaves sharp or rounds off.
// The low-pass gate: its control (the function generator times the velocity, plus the offset while the key is held)
// drives a light-dependent resistor model, which follows a rise in about a millisecond and lets go slowly, and the
// darker it is the slower it lets go (that is the gate's ring); it sets a two-pole low-pass (30 Hz to 20 kHz) and the
// level together (COMBO), only the filter (LOW-PASS) or only the level (VCA). Voices are summed and brought down to
// the sample rate by two halfband stages.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "dsp/Oversampler.hpp"
#include "FoldParams.hpp"
#include <array>
#include <atomic>

namespace pump { namespace fold {

struct Settings {
    int modWave = kModSine, stages = 4, gateMode = kGateCombo, funcMode = kFuncTrig, voices = 8, voiceMode = 0, octave = 0;
    double shape = 0.0, modRatio = 2.0, modDetune = 0.0, fm = 0.0, am = 0.0, modFold = 0.0;
    double fold = 0.3, symmetry = 0.0, edge = 0.3, gateOpen = 0.0, vactrol = 0.4, gateReso = 0.0;
    double rise = 0.001, fall = 0.15, funcGate = 1.0, funcFold = 0.0, funcFm = 0.0, funcPitch = 0.0;
    double randRate = 2.0, randSmooth = 0.0, randFold = 0.0, randFm = 0.0, randPitch = 0.0;
    double velocity = 0.6, spread = 0.4, glide = 0.0, fine = 0.0, volumeDb = 0.0;
};

// the folding cell: u folded back into +-1 by a triangle whose corners are rounded (a parabola over the last
// `w` before each fold: smooth, and straight until then); EDGE 1 leaves the corners sharp
inline double foldCell(double u, double edge, int stages)
{
    const double lim = 2.0 * stages + 1.0;
    u = clampv(u, -lim, lim);
    double t = u + 1.0;                     // the triangle: period 4, +-1, equal to u for |u| <= 1
    t -= 4.0 * std::floor(t * 0.25);        // 0..4
    t = t < 2.0 ? t - 1.0 : 3.0 - t;
    const double w = 0.55 * (1.0 - clampv(edge, 0.0, 1.0)), a = 1.0 - std::fabs(t);   // a: how far from a corner
    if (a < w) { const double y = 1.0 - 0.5 * (a * a / w + w); return t < 0.0 ? -y : y; }
    return t;
}

// the folder's input gain for an amount (0..1.4) and a number of stages: 0.4 (straight) up to the gain at which
// STAGES folds just fit (2 STAGES + 1.5) at 100 %
inline double foldGain(double amt, int stages) { return 0.4 + (2.0 * clampv(stages, 1, 6) + 1.1) * clampv(amt, 0.0, 1.4); }

class Engine {
public:
    static constexpr int kMaxVoices = 8, kOs = 4, kCtl = 16;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate; osr = sr * kOs;
        for (int i = 0; i < kMaxVoices; ++i) { voices[(size_t)i] = Voice(); voices[(size_t)i].rng.seed(0xF01Du + 977u * (uint32_t)i); voices[(size_t)i].dc.set(6.0, osr); }
        for (int c = 0; c < 2; ++c)
        {
            hb2[c].setup(tables::kHB2, (int)(sizeof(tables::kHB2) / sizeof(double)));
            hb1[c].setup(tables::kHB1, (int)(sizeof(tables::kHB1) / sizeof(double)));
        }
        configure();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        ctl = 0;
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.voiceMode != set.voiceMode || s.voices != set.voices;
        set = s;
        if (re) configure();
    }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    ScopeRing foldRing { 1 << 12 };      // the newest voice, after the folder (before the gate), at the sample rate
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }
    float gateNow() const { return gateFeed.load(std::memory_order_relaxed); }       // the newest voice's gate (0..1)
    float funcNow() const { return funcFeed.load(std::memory_order_relaxed); }
    float foldNow() const { return foldFeed.load(std::memory_order_relaxed); }       // its fold amount now (0..1.2)

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if ((ctl++ % kCtl) == 0) control();
            float os[2][kOs] = {};
            double vis = 0.0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[(size_t)vi];
                if (!v.active) continue;
                for (int k = 0; k < kOs; ++k)
                {
                    double pre = 0.0;
                    const double y = sample(v, pre);
                    os[0][k] += (float)(y * v.gl); os[1][k] += (float)(y * v.gr);
                    if (vi == newest && k == 0) vis = pre;
                }
                if (v.done()) { v.active = false; if (vi < vm.voices()) vm.voiceFinished(vi); }
            }
            const double vol = volS.next();
            for (int c = 0; c < 2; ++c)
            {
                const float a = hb2[c].down(os[c][0], os[c][1]), b = hb2[c].down(os[c][2], os[c][3]);
                (c ? R : L)[i] = (float)(hb1[c].down(a, b) * vol);
            }
            foldRing.push((float)vis);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk))
        {
            for (auto& v : voices) { v.active = false; v.lp.reset(); }
            for (int c = 0; c < 2; ++c) { hb1[c].reset(); hb2[c].reset(); }
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        int act = 0; uint64_t lit[2] = { 0, 0 };
        for (const Voice& v : voices) { if (!v.active) continue; ++act; if (v.gate) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63); }
        activeFeed.store(act); litFeed[0].store(lit[0]); litFeed[1].store(lit[1]);
        const Voice& nv = voices[(size_t)newest];
        gateFeed.store(nv.active ? (float)nv.lpr : 0.0f); funcFeed.store(nv.active ? (float)nv.fn : 0.0f); foldFeed.store(nv.active ? (float)nv.foldAmt : (float)set.fold);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiNote(uint8_t nn, uint8_t vel) { synth::Midi m { 0, (uint8_t)(vel ? 0x90 : 0x80), nn, vel }; handle(m); }

private:
    struct Voice {
        bool active = false, gate = false;
        int note = 60, stage = 0;   // function: 0 idle, 1 rising, 2 holding (sustain), 3 falling
        uint64_t age = 0;
        double vel = 1.0, pitch = 60.0, target = 60.0, strength = 1.0;
        double p = 0.0, q = 0.0, fp = 261.6, fq = 523.2;
        double fn = 0.0, off = 0.0, lpr = 0.0;     // function, offset envelope, the light-dependent resistor
        double rnd = 0.0, rndT = 0.0, rndPh = 1.0;
        double foldAmt = 0.3, fmAmt = 0.0, gl = 0.7071, gr = 0.7071, fc = 1000.0, gain = 0.0;
        fx::Svf lp;
        fx::DcBlocker dc;
        fx::Rng rng { 1u };
        bool done() const { return !gate && stage == 0 && lpr < 3e-3 && off < 1e-3; }
    };

    void configure()
    {
        if (set.voiceMode == 0) vm.setup(clampv(set.voices, 1, kMaxVoices), synth::kPoly);
        else vm.setup(1, set.voiceMode == 2 ? synth::kLegato : synth::kMono);
        vm.reset();
        for (auto& v : voices) v.active = false;
    }
    void handle(const synth::Midi& m)
    {
        vm.midi(m.status, m.d1, m.d2, [&](const synth::VoiceCmd& c) {
            const int vi = clampv(c.voice, 0, kMaxVoices - 1);
            Voice& v = voices[(size_t)vi];
            switch (c.kind)
            {
            case synth::VoiceCmd::Start:
            {
                const bool was = v.active;
                if (!was) { v.lp.reset(); v.dc.reset(); v.lpr = 0.0; v.off = 0.0; v.fn = 0.0; v.p = 0.0; v.q = 0.0; v.rndPh = 1.0; }
                v.active = true; v.gate = true; v.note = c.note; v.target = c.note; v.age = ++counter; newest = vi;
                if (c.velocity >= 0.0f) v.vel = c.velocity;
                if (!(set.glide > 0.0 && was)) v.pitch = c.note;
                v.strength = (1.0 - set.velocity) + set.velocity * v.vel;
                if (c.velocity >= 0.0f || !was) v.stage = 1;   // (legato: the function carries on)
                const double pan = ((vi & 1) ? 1.0 : -1.0) * set.spread * (0.3 + 0.1 * (vi >> 1));
                v.gl = std::sqrt(0.5 * (1.0 - pan)); v.gr = std::sqrt(0.5 * (1.0 + pan));
                tune(v);
                break;
            }
            case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; break;
            case synth::VoiceCmd::Release: v.gate = false; if (v.stage == 1 || v.stage == 2) v.stage = set.funcMode == kFuncTrig ? v.stage : 3; break;
            case synth::VoiceCmd::Kill: v.active = false; v.gate = false; break;
            }
        });
    }

    void tune(Voice& v)
    {
        const Settings& s = set;
        const double semis = v.pitch + 12.0 * s.octave + s.fine * 0.01 + vm.pitchBend() * 2.0 + s.funcPitch * v.fn + s.randPitch * v.rnd;
        const double f = synth::midiToHz(semis);
        v.fp = std::min(f, 0.2 * osr);
        v.fq = clampv(f * s.modRatio + s.modDetune, -0.2 * osr, 0.2 * osr);
        v.foldAmt = clampv(s.fold + s.funcFold * v.fn + 0.5 * s.randFold * v.rnd, 0.0, 1.2);
        v.fmAmt = clampv(s.fm + s.funcFm * v.fn + 0.5 * s.randFm * v.rnd, 0.0, 1.5);
    }
    void control()
    {
        const double dt = kCtl / sr;
        const double gk = set.glide > 0.0 ? 1.0 - std::exp(-dt * 3000.0 / set.glide) : 1.0;
        for (auto& v : voices)
        {
            if (!v.active) continue;
            v.pitch += (v.target - v.pitch) * gk;
            // the random source: a new value each tick of its clock, stepped or slewed toward
            v.rndPh += set.randRate * dt;
            if (v.rndPh >= 1.0) { v.rndPh -= std::floor(v.rndPh); v.rndT = v.rng.bi(); if (set.randSmooth <= 0.0) v.rnd = v.rndT; }
            if (set.randSmooth > 0.0) v.rnd += (v.rndT - v.rnd) * (1.0 - std::exp(-dt * set.randRate * 4.0 / (0.05 + 1.5 * set.randSmooth)));
            tune(v);
            // the gate's filter (its cutoff follows the resistor)
            v.fc = 30.0 * std::exp2(9.4 * std::pow(clampv(v.lpr, 0.0, 1.0), 0.8));
            if (set.gateMode == kGateVca) v.fc = 20000.0;
            v.lp.set(std::min(v.fc, 0.45 * osr), 0.6 + 3.4 * set.gateReso, osr);
        }
    }

    // the function generator and the gate (at the oversampled rate)
    inline void envelopes(Voice& v)
    {
        const Settings& s = set;
        const double ri = 1.0 / (std::max(0.0002, s.rise) * osr), fi = 1.0 / (std::max(0.001, s.fall) * osr);
        switch (v.stage)
        {
        case 1: v.fn += ri; if (v.fn >= 1.0) { v.fn = 1.0; v.stage = (s.funcMode == kFuncSustain && v.gate) ? 2 : 3; } break;
        case 2: v.fn = 1.0; if (!v.gate || s.funcMode != kFuncSustain) v.stage = 3; break;
        case 3: v.fn -= fi; if (v.fn <= 0.0) { v.fn = 0.0; v.stage = (s.funcMode == kFuncCycle && v.gate) ? 1 : 0; } break;
        default: break;
        }
        // the offset holds the gate open while the key is down
        const double offT = v.gate ? s.gateOpen : 0.0;
        v.off += (offT - v.off) * (offT > v.off ? 0.002 : fi * 3.0);
        const double ctlv = clampv(s.funcGate * v.fn * v.strength + v.off, 0.0, 1.0);
        // the light-dependent resistor: quick to follow a rise, slow to let go, slower still as it gets dark
        const double r = clampv(s.vactrol, 0.0, 1.0);
        const double atk = 1.0 / ((0.0006 + 0.004 * r) * osr), rel = 1.0 / ((0.02 + 0.5 * r * r + 0.03 * r) * osr);
        if (ctlv > v.lpr) v.lpr += (ctlv - v.lpr) * std::min(1.0, atk * 3.0);
        else v.lpr += (ctlv - v.lpr) * rel * (0.4 + 1.5 * v.lpr);
    }

    inline double sample(Voice& v, double& pre)
    {
        const Settings& s = set;
        envelopes(v);
        // the modulation oscillator
        v.q += v.fq / osr; v.q -= std::floor(v.q);
        double m;
        switch (s.modWave)
        {
        case kModTri: m = 1.0 - 4.0 * std::fabs(v.q - 0.5); break;
        case kModSaw: m = synth::blepSaw(v.q, std::fabs(v.fq) / osr); break;
        case kModSquare: m = synth::blepPulse(v.q, std::fabs(v.fq) / osr, 0.5); break;
        default: m = std::sin(kTwoPi * v.q); break;
        }
        // the principal, frequency modulated through zero
        v.p += v.fp * (1.0 + 6.0 * v.fmAmt * m) / osr; v.p -= std::floor(v.p);
        const double sh = clampv(s.shape, 0.0, 1.0);
        const double sn = std::sin(kTwoPi * v.p), sw = 2.0 * v.p - 1.0;
        const double tri = v.p < 0.25 ? 4.0 * v.p : (v.p < 0.75 ? 2.0 - 4.0 * v.p : 4.0 * v.p - 4.0);   // in phase with the sine
        double x = sh < 0.5 ? sn + (tri - sn) * sh * 2.0 : tri + (sw - tri) * (sh - 0.5) * 2.0;
        x *= (1.0 + s.am * m) / (1.0 + s.am);
        // the folder
        const double amt = clampv(v.foldAmt + 0.6 * s.modFold * m, 0.0, 1.4);
        // below a gain of 1 the level is made up, so FOLD 0 is the plain oscillator
        const double g = foldGain(amt, s.stages);
        double y = foldCell(x * g + s.symmetry, clampv(s.edge, 0.0, 1.0), clampv(s.stages, 1, 6)) / std::min(1.0, g);
        y = v.dc.process(y);
        pre = y;
        // the gate
        const double lp = v.lp.lowpass(y);
        const double lv = std::pow(clampv(v.lpr, 0.0, 1.0), 1.4);
        switch (s.gateMode)
        {
        case kGateLp: return lp * 0.35;
        case kGateVca: return y * lv * 0.35;
        default: return lp * lv * 0.35;
        }
    }

    Settings set;
    double sr = 48000.0, osr = 192000.0;
    std::array<Voice, kMaxVoices> voices {};
    synth::VoiceManager vm;
    HalfbandStage hb1[2], hb2[2];
    uint64_t ctl = 0, counter = 0;
    int newest = 0;
    LinearSmoother volS;
    std::atomic<int> activeFeed { 0 };
    std::atomic<float> gateFeed { 0.0f }, funcFeed { 0.0f }, foldFeed { 0.0f };
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
};

}} // namespace pump::fold
