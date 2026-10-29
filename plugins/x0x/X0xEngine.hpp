// pump X0X engine: a rhythm machine of eleven instruments, each modelled on any of five classic machines' circuits,
// with a sixteen-step sequencer and an accent row. Framework-free.
//
// The instrument families (the machines' numbers are in the tables below):
//   bass drum  a sine (Nine and Seven: shaped from a triangle) swept down from a pitch envelope, decaying; an attack
//              click (a short pulse and high-passed noise). Eight rings long and deep with a small overshoot at the
//              hit; Nine sweeps far and fast (TONE = how far); Six is short and clicky; Ten a low-passed thud
//   snare      two tuned heads (Nine's swept a little) and noise through a high- and a low-pass with its own decay
//              (SNAPPY); TONE: the balance of the heads (Nine: the noise's brightness)
//   clap       noise through a band-pass in quick bursts (2 to 4, machine by machine) and a tail (TAIL, DECAY)
//   hats       six square oscillators at the machine's inharmonic set, or noise (Ten), mixed by NOISE, through a band-
//   cymbal     pass and a high-pass (the cymbal: two bands, the lower one longer); closed chokes open
//   toms       a sine swept down (SWEEP), noise (NOISE) through a low-pass
//   rim        two short resonances and a click, clipped
//   cowbell    two square oscillators through a narrow band-pass, a short loud stage then the decay (ATTACK)
// Nine's hats and cymbal are six-bit samples (they were); Seven is wholly eight-bit at a 25 kHz rate.
// A new hit on an instrument fades its previous voice (4 ms); the closed hat chokes the open one.
//
// Accent: hits on the accent row's steps (or MIDI at or above ACC VEL) play louder by ACCENT and brighter. The
// sequencer: SONG locks to the song while the host plays, FREE always plays (at the host tempo), the window's PLAY
// runs it now; SWING delays every second step.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Tempo.hpp"
#include "X0xParams.hpp"
#include <array>
#include <climits>

namespace pump { namespace x0x {

struct InstrSettings { int machine = kEight; double tune = 0, decay = 0.5, tone = 0.5, snap = 0.5, levelDb = 0, pan = 0; };
struct Settings {
    InstrSettings in[kInstr];
    double accent = 0.6, drive = 0.0, volumeDb = 0.0, velocity = 0.4;
    int accentVel = 110, seqMode = kSeqSong, length = kSteps, rate = 2;
    double swing = 0.0;
    Pattern pattern = defaultPattern();
};

// which instrument a note plays: the pads' white keys from C-4, and the General MIDI drum notes below
inline int instrForNote(int note)
{
    for (int i = 0; i < kInstr; ++i) if (noteForInstr(i) == note) return i;
    if (note >= kPadBaseNote) return -1;
    switch (note)
    {
    case 35: case 36: return kBD;
    case 37: return kRS;
    case 38: case 40: return kSD;
    case 39: return kCP;
    case 42: case 44: return kCH;
    case 46: return kOH;
    case 41: case 43: return kLT;
    case 45: case 47: return kMT;
    default: return -1;
    }
}

// ---- the machines' numbers ------------------------------------------------------------------------------------------
struct KickSpec { double f0, sweep, sweepTau, decLo, decHi, click; bool shaped, toneSweeps, toneLowpass; };
static const KickSpec kKick[kNumMachines] = {
    { 49.0, 3.0, 0.005, 0.15, 2.2, 0.6, false, false, false },   // Eight: deep, long, a small overshoot
    { 50.0, 4.0, 0.020, 0.12, 1.0, 1.0, true, true, false },     // Nine: swept far (TONE), shaped
    { 62.0, 5.0, 0.004, 0.06, 0.45, 1.2, false, false, false },  // Six: short, clicky
    { 56.0, 7.0, 0.008, 0.05, 0.35, 0.4, false, false, true },   // Ten: a low-passed thud
    { 52.0, 6.0, 0.015, 0.10, 0.80, 1.0, true, true, false },    // Seven
};
struct SnareSpec { double f0, f1, sweep, bodyLo, bodyHi, nHp, nLp, nLo, nHi; bool toneBright; };
static const SnareSpec kSnare[kNumMachines] = {
    { 238.0, 476.0, 0.0, 0.08, 0.20, 1800.0, 9000.0, 0.05, 0.40, false },
    { 180.0, 330.0, 4.0, 0.06, 0.14, 500.0, 6000.0, 0.06, 0.45, true },
    { 210.0, 380.0, 0.0, 0.05, 0.10, 2500.0, 12000.0, 0.04, 0.30, false },
    { 190.0, 290.0, 0.0, 0.04, 0.08, 900.0, 5000.0, 0.04, 0.25, false },
    { 200.0, 340.0, 3.0, 0.05, 0.12, 700.0, 9000.0, 0.05, 0.35, true },
};
struct ClapSpec { int bursts; double gap, fc, q, hp, tailLo, tailHi; };
static const ClapSpec kClap[kNumMachines] = {
    { 3, 0.010, 1000.0, 2.0, 400.0, 0.08, 0.60 },
    { 4, 0.008, 1200.0, 1.6, 700.0, 0.07, 0.50 },
    { 2, 0.006, 1800.0, 1.2, 900.0, 0.04, 0.25 },
    { 2, 0.012, 1400.0, 1.0, 500.0, 0.05, 0.35 },
    { 4, 0.009, 1100.0, 1.8, 600.0, 0.06, 0.45 },
};
struct MetalSpec { double f[6]; double band, q, hp, noise; };
static const MetalSpec kMetal[kNumMachines] = {
    { { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 }, 7100.0, 1.4, 6500.0, 0.05 },
    { { 263.0, 400.0, 421.0, 474.0, 587.0, 845.0 }, 8500.0, 0.8, 6000.0, 0.35 },
    { { 245.0, 306.0, 365.0, 415.0, 437.0, 619.0 }, 8000.0, 1.6, 7000.0, 0.05 },
    { { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 }, 7500.0, 0.7, 6000.0, 0.85 },
    { { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 }, 7800.0, 1.0, 6200.0, 0.40 },
};
// decay ranges (seconds, -60 dB) of the closed hat, the open hat and the cymbal, per machine
static const double kHatDec[kNumMachines][3][2] = {
    { { 0.02, 0.12 }, { 0.15, 1.2 }, { 0.6, 3.5 } },
    { { 0.03, 0.15 }, { 0.15, 1.0 }, { 0.8, 3.0 } },
    { { 0.015, 0.09 }, { 0.10, 0.8 }, { 0.5, 2.5 } },
    { { 0.02, 0.10 }, { 0.10, 0.6 }, { 0.4, 1.8 } },
    { { 0.02, 0.12 }, { 0.12, 0.9 }, { 0.6, 2.6 } },
};
static const double kTomF[kNumMachines][3] = { { 85, 120, 165 }, { 95, 135, 185 }, { 110, 155, 210 }, { 90, 130, 175 }, { 100, 145, 200 } };
static const double kTomSweep[kNumMachines] = { 4.0, 12.0, 5.0, 6.0, 10.0 };
static const double kTomDec[kNumMachines][2] = { { 0.12, 1.0 }, { 0.10, 0.8 }, { 0.08, 0.6 }, { 0.06, 0.4 }, { 0.10, 0.7 } };
static const double kRimF[kNumMachines][2] = { { 1667.0, 455.0 }, { 1700.0, 520.0 }, { 1800.0, 600.0 }, { 1500.0, 500.0 }, { 1650.0, 480.0 } };
static const double kBellF[kNumMachines][2] = { { 540.0, 800.0 }, { 540.0, 800.0 }, { 560.0, 850.0 }, { 600.0, 880.0 }, { 545.0, 815.0 } };

class Engine {
public:
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& v : voices) v = Voice();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        fadeStep = 1.0 / std::max(1.0, 0.004 * sr);
        seqReset();
    }
    void setSettings(const Settings& s) { set = s; }
    void setTransport(const Transport& t) { tr = t; }
    void setPreview(bool on) { preview = on; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    float level(int i) const { return lvFeed[(size_t)clampv(i, 0, kInstr - 1)].load(std::memory_order_relaxed); }
    int stepNow() const { return stepFeed.load(std::memory_order_relaxed); }
    uint32_t lastHit() const { return lastHitFeed.load(std::memory_order_relaxed); }   // instrument | count << 8

    // play an instrument now (the window's pads), velocity 0..1
    void hit(int instr, double vel, bool accent)
    {
        if (instr < 0 || instr >= kInstr) return;
        lastHitFeed.store((uint32_t)instr | (((lastHitFeed.load(std::memory_order_relaxed) >> 8) + 1u) << 8), std::memory_order_relaxed);
        if (instr == kCH) for (auto& v : voices) if (v.active && v.instr == kOH) v.fadeOut = true;   // the closed hat chokes the open one
        Voice* slot = nullptr;
        for (int k = 0; k < 2; ++k) { Voice& v = voices[(size_t)(instr * 2 + k)]; if (v.active) v.fadeOut = true; else if (!slot) slot = &v; }
        if (!slot) { Voice& a = voices[(size_t)instr * 2], &b = voices[(size_t)instr * 2 + 1]; slot = a.fade < b.fade ? &a : &b; }
        start(*slot, instr, clampv(vel, 0.0, 1.0), accent);
    }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        const int mode = preview ? kSeqFree : set.seqMode;
        if (mode != lastMode) { seqReset(); lastMode = mode; }
        const bool locked = tr.playing && tr.valid && std::isfinite(tr.ppq) && std::fabs(tr.ppq) < 1e9;
        const double stepsPerSample = clampv(tr.bpm, 20.0, 999.0) / 60.0 / sr * stepsPerQuarter(set.rate);
        if (mode == kSeqSong && !locked && running) seqReset();
        const double kd = set.drive > 0.001 ? 1.0 + set.drive * set.drive * 8.0 : 0.0, kdn = kd > 0.0 ? 1.0 / std::tanh(kd) : 1.0;
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if (mode == kSeqSong && locked) clock(tr.ppq * stepsPerQuarter(set.rate) + i * stepsPerSample);
            else if (mode == kSeqFree) { if (locked) clock(tr.ppq * stepsPerQuarter(set.rate) + i * stepsPerSample); else { clock(freePos); freePos += stepsPerSample; } }
            double l = 0.0, r = 0.0;
            for (auto& v : voices)
            {
                if (!v.active) continue;
                double y = voiceSample(v);
                if (v.fadeOut) { v.fade -= fadeStep; if (v.fade <= 0.0) { v.active = false; continue; } y *= v.fade; }
                l += y * v.gl; r += y * v.gr;
            }
            l *= 0.22; r *= 0.22;   // headroom: a full pattern adds up (VOLUME 0 dB: the default kit and pattern near -18 dBFS RMS)
            if (kd > 0.0) { l = std::tanh(l * kd) * kdn; r = std::tanh(r * kd) * kdn; }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) v.active = false; sanitizeBlock(L, n); sanitizeBlock(R, n); }
        float lv[kInstr] = {};
        for (const auto& v : voices) if (v.active) lv[v.instr] = std::max(lv[v.instr], (float)(v.show * v.fade));
        for (int k = 0; k < kInstr; ++k) lvFeed[(size_t)k].store(lv[k], std::memory_order_relaxed);
        stepFeed.store(running ? curStep : -1, std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    int voicesNow() const { int c = 0; for (const auto& v : voices) c += v.active ? 1 : 0; return c; }

private:
    struct Voice {
        bool active = false, fadeOut = false;
        int instr = 0, machine = 0;
        InstrSettings p;
        double fade = 1.0, gain = 1.0, gl = 1.0, gr = 1.0, t = 0.0, show = 0.0, bright = 1.0;
        double ph[6] = {}, amp = 0.0, ampK = 0.999, amp2 = 0.0, amp2K = 0.999, nz = 0.0, nzK = 0.999, pe = 1.0, peK = 0.99;
        double click = 0.0, clickK = 0.99, atk = 0.0, atkInc = 1.0;
        double f0 = 100.0, f1 = 200.0, sweep = 0.0, mix = 0.5;
        int bursts = 3; double gap = 0.01;
        fx::Svf bp1, bp2;
        fx::OnePole lp, hp, hp2, clickHp;
        fx::Crusher crush;
        bool crushed = false;
        fx::Rng rng { 1u };
    };

    // ---- MIDI ----
    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        if (st == 0x90 && m.d2 > 0)
        {
            const int in = instrForNote(m.d1);
            if (in >= 0) hit(in, m.d2 / 127.0, m.d2 >= set.accentVel);
        }
        else if (st == 0xB0 && (m.d1 == 120 || m.d1 == 123)) for (auto& v : voices) if (v.active) v.fadeOut = true;
    }

    // ---- the sequencer ----
    void seqReset() { running = false; curStep = -1; lastIdx = LLONG_MIN; freePos = 0.0; }
    void clock(double x)
    {
        const int L = clampv(set.length, 1, kSteps);
        const double odd = 1.0 + clampv(set.swing, 0.0, 0.75);
        const double pair = std::floor(x * 0.5 + 1e-9);
        const double w = x - 2.0 * pair;
        const long long idx = (long long)pair * 2 + (w >= odd - 1e-9 ? 1 : 0);
        if (idx == lastIdx) return;
        lastIdx = idx;
        running = true;
        curStep = (int)(((idx % L) + L) % L);
        const Pattern& p = set.pattern;
        const bool acc = (p.rows[kAccentRow] >> curStep) & 1u;
        for (int in = 0; in < kInstr; ++in) if ((p.rows[in] >> curStep) & 1u) hit(in, 0.8, acc);
    }

    // ---- a hit ----
    double coef(double seconds) const { return std::exp(std::log(1e-3) / std::max(1.0, seconds * sr)); }   // -60 dB in `seconds`
    double tauK(double seconds) const { return std::exp(-1.0 / std::max(1.0, seconds * sr)); }
    static double range(const double* lohi, double t) { return lohi[0] * std::pow(lohi[1] / lohi[0], clampv(t, 0.0, 1.0)); }
    static double rangeOf(double lo, double hi, double t) { return lo * std::pow(hi / lo, clampv(t, 0.0, 1.0)); }

    void start(Voice& v, int instr, double vel, bool accent)
    {
        const InstrSettings& p = set.in[instr];
        v = Voice();
        v.active = true; v.instr = instr; v.p = p; v.machine = clampv(p.machine, 0, kNumMachines - 1);
        v.rng = fx::Rng((uint32_t)(0x5EEDu + 7919u * (uint32_t)(++hits)));
        const int M = v.machine;
        const double acc = accent ? set.accent : 0.0;
        const double velGain = 1.0 - set.velocity * (1.0 - vel * vel);
        v.gain = dbToGain(p.levelDb) * velGain * (1.0 + 1.1 * acc);
        v.bright = 1.0 + 0.8 * acc;
        const double pan = clampv(p.pan, -1.0, 1.0);
        v.gl = std::cos((pan + 1.0) * kPi * 0.25) * 1.41421356; v.gr = std::sin((pan + 1.0) * kPi * 0.25) * 1.41421356;
        v.atk = 0.0; v.atkInc = 1.0 / std::max(1.0, 0.0003 * sr);
        const double tr = std::exp2(p.tune / 12.0);
        v.amp = 1.0;
        v.clickHp.set(1200.0, sr);
        switch (instr)
        {
        case kBD:
        {
            const KickSpec& k = kKick[M];
            v.f0 = k.f0 * tr;
            v.sweep = k.toneSweeps ? 4.0 + p.tone * 28.0 : k.sweep;
            v.peK = tauK(k.sweepTau);
            v.ampK = coef(rangeOf(k.decLo, k.decHi, p.decay));
            v.click = k.click * (0.2 + 1.2 * p.snap) * v.bright; v.clickK = tauK(0.0018);
            v.lp.set(k.toneLowpass ? 120.0 * std::exp2(p.tone * 4.3) : 300.0 * std::exp2(p.tone * 5.0), sr);
            break;
        }
        case kSD:
        {
            const SnareSpec& s = kSnare[M];
            v.f0 = s.f0 * tr; v.f1 = s.f1 * tr; v.sweep = s.sweep; v.peK = tauK(0.012);
            v.ampK = coef(rangeOf(s.bodyLo, s.bodyHi, p.decay));
            v.nz = 1.0; v.nzK = coef(rangeOf(s.nLo, s.nHi, p.decay));
            v.hp.set(s.nHp, sr);
            v.lp.set(s.toneBright ? 2000.0 * std::exp2(p.tone * 3.0) : s.nLp, sr);
            v.mix = s.toneBright ? 0.35 : p.tone;   // the second head's share
            v.click = 0.4 * v.bright; v.clickK = tauK(0.0012);
            break;
        }
        case kCP:
        {
            const ClapSpec& c = kClap[M];
            v.bursts = c.bursts; v.gap = c.gap;
            v.bp1.set(c.fc * std::exp2((p.tone - 0.5) * 2.0) * tr, c.q, sr);
            v.hp.set(c.hp, sr);
            v.ampK = coef(rangeOf(c.tailLo, c.tailHi, p.decay));
            break;
        }
        case kCH: case kOH: case kCY:
        {
            const MetalSpec& m = kMetal[M];
            const int which = instr == kCH ? 0 : (instr == kOH ? 1 : 2);
            v.ampK = coef(range(kHatDec[M][which], p.decay));
            const double band = m.band * std::exp2((p.tone - 0.5) * 1.4);
            if (which < 2) { v.bp1.set(band, m.q, sr); v.hp.set(m.hp * std::exp2((p.tone - 0.5) * 1.0), sr); }
            else
            {
                // the cymbal: a lower band that rings longer, the upper one shorter
                v.bp1.set(band * 0.48, 1.2, sr); v.bp2.set(band, m.q, sr); v.hp.set(2500.0, sr);
                v.amp2 = 1.0; v.amp2K = coef(range(kHatDec[M][2], p.decay) * 0.45);
            }
            v.mix = clampv(m.noise + p.snap * (1.0 - m.noise), 0.0, 1.0);
            v.f0 = tr;   // a multiplier for the oscillator set
            for (int k = 0; k < 6; ++k) v.ph[k] = v.rng.uni();
            if (M == kNine) { v.crush.set(1.0, 6.0); v.crushed = true; }
            break;
        }
        case kLT: case kMT: case kHT:
        {
            v.f0 = kTomF[M][instr - kLT] * tr;
            v.sweep = kTomSweep[M] * (0.2 + 1.6 * p.snap);
            v.peK = tauK(M == kNine ? 0.05 : 0.035);
            v.ampK = coef(rangeOf(kTomDec[M][0], kTomDec[M][1], p.decay));
            v.nz = 1.0; v.nzK = coef(0.08 + 0.15 * p.decay);
            v.lp.set(M == kNine ? 3000.0 : 1600.0, sr);
            v.mix = p.tone;
            break;
        }
        case kRS:
        {
            v.f0 = kRimF[M][0] * tr; v.f1 = kRimF[M][1] * tr;
            v.ampK = coef(rangeOf(0.006, 0.06, p.decay));
            v.amp2 = 1.0; v.amp2K = coef(rangeOf(0.004, 0.03, p.decay));
            v.click = 0.3 + 1.2 * p.snap; v.clickK = tauK(0.0008);
            v.mix = p.tone;
            v.hp.set(M == kNine ? 400.0 : 200.0, sr);
            break;
        }
        case kCB:
        {
            v.f0 = kBellF[M][0] * tr; v.f1 = kBellF[M][1] * tr;
            v.bp1.set(2640.0 * std::exp2((p.tone - 0.5) * 1.2) * tr, 3.5, sr);
            v.amp2 = 0.4 + 1.2 * p.snap; v.amp2K = tauK(0.012);   // the short loud stage
            v.ampK = coef(rangeOf(0.05, 0.6, p.decay));
            v.hp.set(M == kTen ? 900.0 : 400.0, sr);
            if (M == kNine) { v.crush.set(1.0, 6.0); v.crushed = true; }
            break;
        }
        default: break;
        }
        if (M == kSeven) { v.crush.set(25000.0 / sr, 8.0); v.crushed = true; }
        v.show = 1.0;
    }

    static double sine(double ph) { return std::sin(kTwoPi * ph); }
    double squares(Voice& v, const double* f)
    {
        double s = 0.0;
        for (int k = 0; k < 6; ++k)
        {
            const double inc = std::min(0.45, f[k] * v.f0 / sr);
            s += synth::blepPulse(v.ph[k], inc, 0.5);
            v.ph[k] += inc; if (v.ph[k] >= 1.0) v.ph[k] -= 1.0;
        }
        return s * (1.0 / 6.0);
    }

    double voiceSample(Voice& v)
    {
        const InstrSettings& p = v.p;
        const int M = v.machine;
        const double wn = v.rng.bi();
        double y = 0.0;
        bool done = false;
        switch (v.instr)
        {
        case kBD:
        {
            const KickSpec& k = kKick[M];
            const double f = v.f0 * std::exp2(v.sweep * v.pe / 12.0);
            double s = sine(v.ph[0]);
            if (k.shaped) s = std::tanh(1.7 * s) / std::tanh(1.7);
            v.ph[0] += f / sr; if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
            const double tp = v.t * 1500.0, pulse = tp < 1.0 ? std::sin(kPi * tp) : 0.0;
            const double cl = v.click * (0.6 * pulse + 0.5 * v.clickHp.hp(wn));
            y = k.toneLowpass ? v.lp.lp(s * v.amp + cl) : s * v.amp + v.lp.lp(cl);
            y = std::tanh(y * 1.3) / std::tanh(1.3) * 1.5;
            v.click *= v.clickK; v.pe *= v.peK; v.amp *= v.ampK;
            done = v.amp < 1e-4 && v.click < 1e-4;
            v.show = v.amp;
            break;
        }
        case kSD:
        {
            const double pm = std::exp2(v.sweep * v.pe / 12.0);
            const double body = (1.0 - v.mix) * sine(v.ph[0]) + v.mix * 0.8 * sine(v.ph[1]);
            v.ph[0] += v.f0 * pm / sr; v.ph[1] += v.f1 * pm / sr;
            for (int k = 0; k < 2; ++k) if (v.ph[k] >= 1.0) v.ph[k] -= 1.0;
            const double nz = v.lp.lp(v.hp.hp(wn));
            const double snappy = (0.2 + 1.3 * p.snap) * v.bright;
            y = (body * v.amp * (1.0 - 0.3 * p.snap) + nz * v.nz * snappy * 1.4 + v.click * v.clickHp.hp(wn)) * 0.75;
            v.amp *= v.ampK; v.nz *= v.nzK; v.pe *= v.peK; v.click *= v.clickK;
            done = v.amp < 1e-4 && v.nz < 1e-4;
            v.show = std::max(v.amp, v.nz);
            break;
        }
        case kCP:
        {
            double env = 0.0;
            for (int k = 0; k < v.bursts; ++k) { const double dt = v.t - k * v.gap; if (dt >= 0.0) env = std::max(env, std::exp(-dt / 0.0028)); }
            const double tailOn = v.t >= (v.bursts - 1) * v.gap ? 1.0 : 0.0;
            const double x = v.hp.hp(v.bp1.bandpass(wn));
            y = x * (env * 1.3 + tailOn * v.amp * (0.25 + 0.9 * p.snap)) * 1.65 * std::sqrt(kClap[M].q) * v.bright;
            if (tailOn > 0.0) v.amp *= v.ampK;
            done = tailOn > 0.0 && v.amp < 1e-4 && env < 1e-4;
            v.show = std::max(env, tailOn * v.amp);
            break;
        }
        case kCH: case kOH: case kCY:
        {
            const MetalSpec& m = kMetal[M];
            const double sq = squares(v, m.f);
            const double x = (sq * (1.0 - v.mix) + wn * v.mix * 0.5) * 1.3;
            if (v.instr == kCY)
            {
                const double lo = v.bp1.bandpass(x) * v.amp, hi = v.bp2.bandpass(x) * v.amp2;
                y = v.hp.hp(lo * 0.9 + hi * 1.1) * 1.1;
                v.amp2 *= v.amp2K;
            }
            else y = v.hp.hp(v.bp1.bandpass(x)) * v.amp * 3.5;
            y *= v.bright;
            v.amp *= v.ampK;
            done = v.amp < 1e-4;
            v.show = v.amp;
            break;
        }
        case kLT: case kMT: case kHT:
        {
            const double f = v.f0 * std::exp2(v.sweep * v.pe / 12.0);
            const double s = sine(v.ph[0]);
            v.ph[0] += f / sr; if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
            y = s * v.amp + v.lp.lp(wn) * v.nz * v.mix * 0.9;
            v.amp *= v.ampK; v.nz *= v.nzK; v.pe *= v.peK;
            done = v.amp < 1e-4 && v.nz < 1e-4;
            v.show = v.amp;
            break;
        }
        case kRS:
        {
            const double a = sine(v.ph[0]) * v.amp, b = sine(v.ph[1]) * v.amp2;
            v.ph[0] += v.f0 / sr; v.ph[1] += v.f1 / sr;
            for (int k = 0; k < 2; ++k) if (v.ph[k] >= 1.0) v.ph[k] -= 1.0;
            const double cl = v.click * v.clickHp.hp(wn);
            y = std::tanh(3.0 * (a * (0.5 + 0.5 * v.mix) + b * (1.0 - 0.5 * v.mix) + cl)) * 0.8;
            y = v.hp.hp(y);
            v.amp *= v.ampK; v.amp2 *= v.amp2K; v.click *= v.clickK;
            done = v.amp < 1e-4 && v.amp2 < 1e-4;
            v.show = std::max(v.amp, v.amp2);
            break;
        }
        case kCB:
        {
            const double i0 = std::min(0.45, v.f0 / sr), i1 = std::min(0.45, v.f1 / sr);
            const double x = synth::blepPulse(v.ph[0], i0, 0.5) + synth::blepPulse(v.ph[1], i1, 0.5);
            v.ph[0] += i0; v.ph[1] += i1;
            for (int k = 0; k < 2; ++k) if (v.ph[k] >= 1.0) v.ph[k] -= 1.0;
            const double env = v.amp * 0.55 + v.amp2;
            y = v.hp.hp(v.bp1.bandpass(x + (M == kTen ? 0.3 * wn : 0.0))) * env * 0.7 * v.bright;
            v.amp *= v.ampK; v.amp2 *= v.amp2K;
            done = v.amp < 1e-4;
            v.show = std::min(1.0, env);
            break;
        }
        default: done = true; break;
        }
        if (v.crushed) y = v.crush.process(y);
        if (v.atk < 1.0) { v.atk = std::min(1.0, v.atk + v.atkInc); y *= v.atk; }
        v.t += 1.0 / sr;
        if (done || v.t > 8.0) v.active = false;
        return y * v.gain;
    }

    Settings set;
    Transport tr;
    std::array<Voice, kInstr * 2> voices {};
    double sr = 48000.0, fadeStep = 0.005;
    uint32_t hits = 0;
    LinearSmoother volS;
    bool preview = false, running = false;
    int curStep = -1, lastMode = -1;
    long long lastIdx = LLONG_MIN;
    double freePos = 0.0;
    std::array<std::atomic<float>, kInstr> lvFeed {};
    std::atomic<int> stepFeed { -1 };
    std::atomic<uint32_t> lastHitFeed { 0u };
};

}} // namespace pump::x0x
