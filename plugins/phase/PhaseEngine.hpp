// pump PHASE engine: FM in six engines, and phase distortion. Framework-free.
//
// Operators. Each has a phase, a ratio to the note (or a fixed frequency), and an envelope that works the way the
// chips' did: in attenuation (dB), an exponential attack, linear decay to the sustain level, linear release; the
// operator's LEVEL is a further attenuation (0.75 dB a step, like the chips' total level), as are velocity and the
// LFO's tremolo. A modulator's output (+-1) moves its carrier's phase by up to four cycles, as on the chips. Feedback
// (0-7) feeds the mean of an operator's last two outputs back into its own phase, as the chips did.
//
// CHIP on: the operator is the chips' integer pipeline. Its phase is cut to 10 bits; the quarter sine is a log table
// (256 entries, 1/256-octave steps); the envelope and level add to it in that log domain, cut to the chip's envelope
// step; an exponent table turns it back into a 13-bit signed output. The PC card's eight waveforms are the same
// table read in different ways (half, abs, quarter, alternating, camel, square, log saw). The voices run at the
// chip's own rate (53.3, 55.9, 49.7, 49.1 or 36 kHz), so their aliasing folds where the chip's did, through the
// chip's DAC, and are then resampled to the host's rate (a windowed sinc; TONE is the board's low-pass after it): the console's
// 9-bit converter with its crossover step, the floating-point converters of the arcade and PC chips (10-bit
// mantissa), the studio's 12-bit, phase distortion's 10-bit. CHIP off: the same voice in clean floating point at
// the host's rate.
//
// Phase distortion: a cosine read through a phase that is bent: bent hard toward the middle of the cycle it becomes
// a saw; bent in each half, a square; and so on (eight shapes, the last three a fast cosine inside a window that
// resets with the cycle: the resonant sweeps). DCW (the bend) has its own envelope and follows the key; each line can
// alternate two shapes cycle by cycle; two lines (1, 2, 1+1' detuned, 1+2'), the second ring-modulated or
// noise-modulated. Op 1 and op 2's envelopes are the lines' amplitude (DCA).
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "dsp/Resampler.hpp"
#include "PhaseParams.hpp"
#include <array>
#include <atomic>
#include <cmath>

namespace pump { namespace phase {

struct OpSettings {
    int wave = 0, ratio = 1;
    double fine = 0.0, detune = 0.0, level = 99.0, a = 0.001, d = 1.0, s = 0.7, r = 0.3, vel = 0.3;
    bool am = false, fixed = false;
};
struct Settings {
    int engine = kConsole, algo = 0, feedback = 0;
    bool chip = true;
    double tone = 1.0, lfoRate = 5.0, lfoAm = 0.0, lfoPm = 0.0, wheelVib = 0.3;
    int voices = 8, voiceMode = 0;
    double glide = 0.0, bend = 2.0;
    int transpose = 0;
    double volumeDb = -6.0, pitchEnv = 0.0, pitchDecay = 0.08;
    OpSettings op[kOps];
    int pdWave1 = kPdSaw, pdWave1b = 0, pdWave2 = kPdSquare, pdWave2b = 0;
    double pdDcw1 = 0.7, pdDcw2 = 0.7, pdKey = 0.3, pdA = 0.005, pdD = 0.8, pdS = 0.3, pdR = 0.3, pdDetune = 6.0;
    int pdOctave = 0, pdMod = 0;
};

// ---- algorithms ---------------------------------------------------------------------------------------------------
// mod[i]: the operators that modulate operator i (bit j = op j); car: the carriers; fb: the operator with feedback.
struct Algo {
    int n = 2;
    uint8_t mod[kOps] = {};
    uint8_t car = 1;
    int fb = 0;
    const char* name = "";
    int order[kOps] = {};   // modulators before what they modulate
};
namespace detail {
inline Algo make(int n, std::initializer_list<std::pair<int, int>> edges, std::initializer_list<int> carriers, int fb, const char* name)
{
    Algo a; a.n = n; a.fb = fb; a.name = name; a.car = 0;
    for (auto e : edges) a.mod[e.second] |= (uint8_t)(1u << e.first);
    for (int c : carriers) a.car |= (uint8_t)(1u << c);
    // topological order (Kahn): an operator once all its modulators are placed
    bool placed[kOps] = {}; int k = 0;
    while (k < n)
    {
        bool any = false;
        for (int i = 0; i < n; ++i)
        {
            if (placed[i]) continue;
            bool ready = true;
            for (int j = 0; j < n; ++j) if ((a.mod[i] >> j & 1) && !placed[j]) ready = false;
            if (ready) { placed[i] = true; a.order[k++] = i; any = true; }
        }
        if (!any) break;
    }
    return a;
}
} // namespace detail

inline const Algo& algoFor(int engine, int index)
{
    using detail::make;
    // the 4-operator chips (op 1 at the top, with the feedback; op 4 the last carrier)
    static const Algo chip4[8] = {
        make(4, { {0,1}, {1,2}, {2,3} }, { 3 }, 0, "1 > 2 > 3 > 4"),
        make(4, { {0,2}, {1,2}, {2,3} }, { 3 }, 0, "(1 + 2) > 3 > 4"),
        make(4, { {0,3}, {1,2}, {2,3} }, { 3 }, 0, "(1 + (2 > 3)) > 4"),
        make(4, { {0,1}, {1,3}, {2,3} }, { 3 }, 0, "((1 > 2) + 3) > 4"),
        make(4, { {0,1}, {2,3} }, { 1, 3 }, 0, "(1 > 2) + (3 > 4)"),
        make(4, { {0,1}, {0,2}, {0,3} }, { 1, 2, 3 }, 0, "1 > (2 + 3 + 4)"),
        make(4, { {0,1} }, { 1, 2, 3 }, 0, "(1 > 2) + 3 + 4"),
        make(4, {}, { 0, 1, 2, 3 }, 0, "1 + 2 + 3 + 4"),
    };
    static const Algo pc2[2] = {
        make(2, { {0,1} }, { 1 }, 0, "1 > 2 (FM)"),
        make(2, {}, { 0, 1 }, 0, "1 + 2 (additive)"),
    };
    static const Algo pc4[4] = {
        make(4, { {0,1}, {1,2}, {2,3} }, { 3 }, 0, "FM-FM: 1 > 2 > 3 > 4"),
        make(4, { {1,2}, {2,3} }, { 0, 3 }, 0, "AM-FM: 1 + (2 > 3 > 4)"),
        make(4, { {0,1}, {2,3} }, { 1, 3 }, 0, "FM-AM: (1 > 2) + (3 > 4)"),
        make(4, { {1,2} }, { 0, 2, 3 }, 0, "AM-AM: 1 + (2 > 3) + 4"),
    };
    // studio: op 1 is the carrier, the top operator has the feedback
    static const Algo studio[18] = {
        make(2, { {1,0} }, { 0 }, 1, "2-op: 2 > 1"),
        make(2, {}, { 0, 1 }, 1, "2-op: 1 + 2"),
        make(3, { {2,1}, {1,0} }, { 0 }, 2, "3-op: 3 > 2 > 1"),
        make(3, { {1,0}, {2,0} }, { 0 }, 2, "3-op: (2 + 3) > 1"),
        make(3, { {1,0} }, { 0, 2 }, 2, "3-op: (2 > 1) + 3"),
        make(4, { {3,2}, {2,1}, {1,0} }, { 0 }, 3, "4-op: 4 > 3 > 2 > 1"),
        make(4, { {1,0}, {3,2} }, { 0, 2 }, 3, "4-op: (2 > 1) + (4 > 3)"),
        make(4, { {1,0}, {2,0}, {3,0} }, { 0 }, 3, "4-op: (2 + 3 + 4) > 1"),
        make(4, { {3,0}, {3,1}, {3,2} }, { 0, 1, 2 }, 3, "4-op: 4 > (1 + 2 + 3)"),
        make(6, { {1,0}, {5,4}, {4,3}, {3,2} }, { 0, 2 }, 5, "6-op: (2 > 1) + (6 > 5 > 4 > 3)"),
        make(6, { {1,0}, {3,2}, {5,4} }, { 0, 2, 4 }, 5, "6-op: three pairs"),
        make(6, { {2,1}, {1,0}, {5,4}, {4,3} }, { 0, 3 }, 5, "6-op: two stacks of three"),
        make(6, { {5,4}, {4,3}, {3,2}, {2,1}, {1,0} }, { 0 }, 5, "6-op: one stack of six"),
        make(6, { {1,0}, {2,0}, {3,0}, {5,4} }, { 0, 4 }, 5, "6-op: (2 + 3 + 4) > 1, 6 > 5"),
        make(6, { {5,0}, {5,1}, {5,2}, {5,3}, {5,4} }, { 0, 1, 2, 3, 4 }, 5, "6-op: 6 > five carriers"),
        make(6, {}, { 0, 1, 2, 3, 4, 5 }, 5, "6-op: six carriers (organ)"),
        make(6, { {5,4}, {4,3}, {3,2}, {2,0}, {1,0} }, { 0 }, 5, "6-op: (6 > 5 > 4 > 3 + 2) > 1"),
        make(6, { {5,4}, {4,3}, {3,2}, {2,1} }, { 0, 1 }, 5, "6-op: 1 + (6 > 5 > 4 > 3 > 2)"),
    };
    switch (engine)
    {
    case kConsole: case kArcade: return chip4[clampv(index, 0, 7)];
    case kPc2: return pc2[clampv(index, 0, 1)];
    case kPc4: return pc4[clampv(index, 0, 3)];
    default: return studio[clampv(index, 0, 17)];
    }
}
inline int algoCount(int engine) { return kEngineAlgos[clampv(engine, 0, kNumEngines - 1)]; }
inline int opsFor(int engine, int index) { return engine == kPd ? 2 : algoFor(engine, index).n; }

// ---- the chips' tables ----------------------------------------------------------------------------------------------
struct ChipTables {
    uint16_t logsin[256], exp[256];
    ChipTables()
    {
        for (int i = 0; i < 256; ++i)
        {
            logsin[i] = (uint16_t)std::lround(-std::log2(std::sin((i + 0.5) * kPi / 512.0)) * 256.0);
            exp[i] = (uint16_t)std::lround((std::exp2(i / 256.0) - 1.0) * 1024.0);
        }
    }
};
inline const ChipTables& chipTables() { static const ChipTables t; return t; }

// one operator the chips' way: idx a 10-bit phase, att the attenuation in 1/256 octave; a 13-bit signed output
inline int chipWave(int idx, int att, int wave)
{
    const ChipTables& T = chipTables();
    idx &= 1023;
    auto ls = [&](int i) { int q = i & 255; if (i & 256) q = 255 - q; return (int)T.logsin[q]; };
    int l = 0; bool neg = false;
    switch (wave)
    {
    case 1: if (idx & 512) return 0; l = ls(idx); break;                                    // half sine
    case 2: l = ls(idx); break;                                                             // abs sine
    case 3: if (idx & 256) return 0; l = ls(idx); break;                                    // quarter (pulse) sine
    case 4: { if (idx & 512) return 0; const int i2 = (idx << 1) & 1023; neg = i2 & 512; l = ls(i2); break; }   // alternating
    case 5: { if (idx & 512) return 0; l = ls((idx << 1) & 1023); break; }                 // camel
    case 6: neg = idx & 512; l = 0; break;                                                  // square
    case 7: { neg = idx & 512; const int x = neg ? (1023 - idx) & 511 : idx & 511; l = x << 3; break; }   // log saw
    default: neg = idx & 512; l = ls(idx); break;                                           // sine
    }
    const int t = l + att;
    if (t >= 0x1FFF) return 0;
    const int v = (((int)T.exp[(t & 255) ^ 255] | 1024) << 1) >> (t >> 8);
    return neg ? -v : v;
}
// the same shapes in floating point (+-1)
inline double floatWave(double ph, int wave)
{
    ph -= std::floor(ph);
    switch (wave)
    {
    case 1: return ph < 0.5 ? std::sin(kTwoPi * ph) : 0.0;
    case 2: return std::fabs(std::sin(kTwoPi * ph));
    case 3: return std::fmod(ph, 0.5) < 0.25 ? std::fabs(std::sin(kTwoPi * ph)) : 0.0;
    case 4: return ph < 0.5 ? std::sin(2.0 * kTwoPi * ph) : 0.0;
    case 5: return ph < 0.5 ? std::fabs(std::sin(2.0 * kTwoPi * ph)) : 0.0;
    case 6: return ph < 0.5 ? 1.0 : -1.0;
    case 7: return ph < 0.5 ? std::exp2(-ph * 32.0) : -std::exp2(-(1.0 - ph) * 32.0);
    default: return std::sin(kTwoPi * ph);
    }
}
// phase distortion: the cosine through a bent phase (d 0 = a plain cosine)
inline double pdWave(double p, double d, int w)
{
    d = clampv(d, 0.0, 0.985);
    auto sawPh = [&](double q) { const double m = 0.5 * (1.0 - d); return q < m ? 0.5 * q / m : 0.5 + 0.5 * (q - m) / (1.0 - m); };
    switch (w)
    {
    case kPdSquare:
    {
        const double h = p < 0.5 ? 0.0 : 0.5, q = (p - h) * 2.0, m = 1.0 - d;
        return std::cos(kTwoPi * (h + 0.5 * (q < m ? q / m : 1.0)));
    }
    case kPdPulse: { const double m = 1.0 - d; return std::cos(kTwoPi * (p < m ? p / m : 1.0)); }
    case kPdDouble:
    {
        // two humps: each half is a whole cosine cycle bent toward its middle
        const double q = p < 0.5 ? 2.0 * p : 2.0 * p - 1.0;
        const double y = std::cos(kTwoPi * sawPh(q));
        return d < 1e-3 ? std::cos(kTwoPi * p) : (1.0 - d) * std::cos(kTwoPi * p) + d * y;
    }
    case kPdSawPulse: return p < 0.5 ? std::cos(kTwoPi * sawPh(2.0 * p)) : 1.0;
    case kPdResSaw: case kPdResTri: case kPdResTrap:
    {
        const double k = 1.0 + d * 15.0;
        const double win = w == kPdResSaw ? 1.0 - p : (w == kPdResTri ? 1.0 - std::fabs(2.0 * p - 1.0) : (p < 0.5 ? 1.0 : 2.0 * (1.0 - p)));
        return 1.0 - win * (1.0 - std::cos(kTwoPi * p * k));
    }
    default: return std::cos(kTwoPi * sawPh(p));   // saw
    }
}

inline double chipRateFor(int engine)
{
    static const double r[kNumEngines] = { 53267.0, 55930.0, 49716.0, 49716.0, 49096.0, 36000.0 };
    return r[clampv(engine, 0, kNumEngines - 1)];
}
// the chips' frequency multiples: 0 = x0.5; the PC card's table repeats 10, 12 and 15
inline double ratioFor(int engine, int coarse)
{
    static const double pcMul[16] = { 0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15 };
    if (engine == kPc2 || engine == kPc4) return pcMul[clampv(coarse, 0, 15)];
    if (engine == kConsole || engine == kArcade) coarse = clampv(coarse, 0, 15);
    return coarse <= 0 ? 0.5 : (double)coarse;
}
inline bool engineHasWaves(int engine) { return engine == kPc2 || engine == kPc4 || engine == kSix; }

class Engine {
public:
    static constexpr int kMaxVoices = 16, kCtl = 16;
    static constexpr double kSilent = 96.0;   // dB of attenuation that is silence

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        chipTables();
        for (auto& v : voices) v = Voice();
        prepared = true;
        configure();
        lfoPh = 0.0; held = 0.0;
        tone1 = tone2 = dcX = dcY = 0.0;
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        control();
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.voiceMode != set.voiceMode || s.voices != set.voices || s.engine != set.engine;
        set = s;
        if (re && prepared) { for (auto& v : voices) v.active = false; configure(); }
    }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    float opLevel(int k) const { return opFeed[(size_t)clampv(k, 0, kOps - 1)].load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        const bool chip = set.chip;
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if ((ctl++ % kCtl) == 0) control();
            if (chip)
            {
                while (!rs.canPull()) rs.push((float)renderSample());
                held = rs.pull();
            }
            else held = renderSample();
            double y = held;
            // the board's low-pass (TONE), then DC out
            if (toneK < 0.999) { tone1 += toneK * (y - tone1); tone2 += toneK * (tone1 - tone2); y = tone2; }
            const double hp = y - dcX + 0.9995 * dcY; dcX = y; dcY = hp;
            const float o = (float)(hp * volS.next());
            L[i] = o; R[i] = o;
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) v.active = false; tone1 = tone2 = dcX = dcY = held = 0.0; sanitizeBlock(L, n); sanitizeBlock(R, n); }
        int act = 0; uint64_t lit[2] = { 0, 0 }; float lv[kOps] = {};
        for (const Voice& v : voices)
        {
            if (!v.active) continue;
            ++act;
            if (v.gate) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63);
            for (int k = 0; k < kOps; ++k) lv[k] = std::max(lv[k], (float)std::exp2(-(v.op[k].att + tl[k]) / 6.0206));
        }
        activeFeed.store(act); litFeed[0].store(lit[0]); litFeed[1].store(lit[1]);
        for (int k = 0; k < kOps; ++k) opFeed[(size_t)k].store(lv[k]);
        for (int i = 0; i < n; ++i) outRing.push(L[i]);
    }
    void uiNote(uint8_t n, uint8_t v) { synth::Midi m { 0, (uint8_t)(v ? 0x90 : 0x80), n, v }; handle(m); }

private:
    struct Op {
        double ph = 0.0, inc = 0.0, att = kSilent, y1 = 0.0, y2 = 0.0, velAtt = 0.0;
        int stage = 0;   // 0 idle 1 attack 2 decay 3 sustain 4 release
    };
    struct Voice {
        bool active = false, gate = false;
        int note = 60;
        double vel = 1.0, pitch = 60.0, target = 60.0, penv = 0.0;
        Op op[kOps];
        // phase distortion
        double p1 = 0.0, p2 = 0.0, inc1 = 0.0, inc2 = 0.0, dcwKey = 1.0;
        bool alt1 = false, alt2 = false;
        synth::Adsr dcw;
    };
    struct EnvK { double ka = 0.5, dR = 0.01, sAtt = 0.0, rR = 0.01; };

    void configure()
    {
        if (set.voiceMode == 0) vm.setup(clampv(set.voices, 1, kMaxVoices), synth::kPoly);
        else vm.setup(1, set.voiceMode == 2 ? synth::kLegato : synth::kMono);
        vm.reset();
        chipRate = chipRateFor(set.engine);
        rs.prepare(chipRate, sr, 8, 12);
    }

    void startOps(Voice& v)
    {
        for (int k = 0; k < kOps; ++k)
        {
            Op& o = v.op[k];
            o.stage = 1;
            o.velAtt = set.op[k].vel * (1.0 - v.vel) * 30.0;
            o.ph = 0.0; o.y1 = o.y2 = 0.0;   // the chips reset the phase on key-on
        }
        v.dcw.gate(true);
        v.p1 = v.p2 = 0.0; v.alt1 = v.alt2 = false;
        v.penv = 1.0;
    }
    void handle(const synth::Midi& m)
    {
        vm.midi(m.status, m.d1, m.d2, [&](const synth::VoiceCmd& c) {
            Voice& v = voices[(size_t)clampv(c.voice, 0, kMaxVoices - 1)];
            switch (c.kind)
            {
            case synth::VoiceCmd::Start:
            {
                const bool was = v.active;
                v.active = true; v.gate = true; v.note = c.note; v.target = c.note;
                if (c.velocity >= 0.0f) v.vel = c.velocity;
                if (!(set.glide > 0.0 && was)) v.pitch = c.note;
                if (!was) { for (auto& o : v.op) o.att = kSilent; v.dcw.kill(); }
                v.dcw.setSampleRate(rate());
                startOps(v);
                break;
            }
            case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; break;
            case synth::VoiceCmd::Release:
                v.gate = false;
                for (auto& o : v.op) if (o.stage != 0) o.stage = 4;
                v.dcw.gate(false);
                break;
            case synth::VoiceCmd::Kill: v.active = false; v.gate = false; break;
            }
        });
    }
    double rate() const { return set.chip ? chipRate : sr; }

    void control()
    {
        const Settings& s = set;
        const double rt = rate(), dt = kCtl / sr;
        // the chips step their envelopes every few samples; clean steps every sample
        envEvery = s.chip ? (s.engine == kPc2 || s.engine == kPc4 ? 1 : 3) : 1;
        attStep = s.chip ? (s.engine == kPc2 || s.engine == kPc4 ? 8 : 4) : 0;
        const double es = envEvery;
        for (int k = 0; k < kOps; ++k)
        {
            const OpSettings& o = s.op[k];
            EnvK& K = env[k];
            K.ka = 1.0 - std::pow(25.0, -es / std::max(1.0, o.a * rt));
            K.dR = kSilent * es / std::max(1.0, o.d * rt);
            K.rR = kSilent * es / std::max(1.0, o.r * rt);
            const double sus = clampv(o.s, 0.0, 1.0);
            K.sAtt = sus <= 0.0 ? kSilent + 1.0 : kSilent * (1.0 - sus) * (1.0 - sus) * 0.5;
            tl[k] = o.level <= 0.0 ? 200.0 : (99.0 - clampv(o.level, 0.0, 99.0)) * 0.75;
        }
        // the LFO
        lfoPh += s.lfoRate * dt; lfoPh -= std::floor(lfoPh);
        const double tri = lfoPh < 0.5 ? 4.0 * lfoPh - 1.0 : 3.0 - 4.0 * lfoPh;
        const double am = s.lfoAm * 12.0 * (0.5 - 0.5 * tri);
        for (int k = 0; k < kOps; ++k) amAtt[k] = s.op[k].am ? am : 0.0;
        const double vib = (s.lfoPm * 100.0 + vm.wheel() * s.wheelVib * 50.0) * tri / 100.0;
        toneK = s.tone >= 0.995 ? 1.0 : 1.0 - std::exp(-kTwoPi * 800.0 * std::pow(25.0, clampv(s.tone, 0.0, 1.0)) / sr);
        const double gk = s.glide > 0.0 ? 1.0 - std::exp(-dt * 3000.0 / s.glide) : 1.0;
        const double pk = std::exp(-dt / std::max(0.001, s.pitchDecay));
        for (Voice& v : voices)
        {
            if (!v.active) continue;
            v.pitch += (v.target - v.pitch) * gk;
            v.penv *= pk;
            const double semis = v.pitch + s.transpose + vm.pitchBend() * s.bend + vib + s.pitchEnv * v.penv;
            const double hz = synth::midiToHz(semis);
            for (int k = 0; k < kOps; ++k)
            {
                const OpSettings& o = s.op[k];
                const double det = std::exp2(o.detune * 1.5 / 1200.0);
                double f;
                if (o.fixed) f = std::pow(10.0, (double)(o.ratio % 4)) * std::pow(10.0, o.fine / 100.0) * det;
                else f = hz * ratioFor(s.engine, o.ratio) * (1.0 + o.fine / 100.0) * det;
                v.op[k].inc = std::min(0.49, f / rt);
            }
            v.inc1 = std::min(0.49, hz / rt);
            const double d2 = s.algo == 2 || s.algo == 3 ? s.pdDetune : 0.0;
            v.inc2 = std::min(0.49, synth::midiToHz(semis + (s.algo == 1 ? 0 : 12.0 * s.pdOctave) + d2 / 100.0) / rt);
            v.dcwKey = clampv(1.0 - s.pdKey * clampv((v.pitch - 36.0) / 60.0, 0.0, 1.0), 0.05, 1.0);
            v.dcw.setSampleRate(rt);
            v.dcw.set(s.pdA, s.pdD, s.pdS, s.pdR);
        }
    }

    inline void envTick(Op& o, const EnvK& K)
    {
        switch (o.stage)
        {
        case 1: o.att -= (o.att + 4.0) * K.ka; if (o.att <= 0.0) { o.att = 0.0; o.stage = 2; } break;
        case 2: o.att += K.dR; if (o.att >= K.sAtt) { o.att = std::min(K.sAtt, kSilent); o.stage = K.sAtt > kSilent ? 0 : 3; } break;
        case 4: o.att += K.rR; if (o.att >= kSilent) { o.att = kSilent; o.stage = 0; } break;
        default: break;
        }
    }

    // one sample of every voice (at the chip's rate or the host's), mixed, through the DAC
    double renderSample()
    {
        const Settings& s = set;
        const bool chip = s.chip;
        const bool envNow = (++envCount % envEvery) == 0;
        double mix = 0.0;
        for (int vi = 0; vi < kMaxVoices; ++vi)
        {
            Voice& v = voices[(size_t)vi];
            if (!v.active) continue;
            double y = s.engine == kPd ? pdVoice(v, envNow) : fmVoice(v, envNow, chip);
            if (chip && s.engine == kConsole)
            {
                // the console's 9-bit DAC: each channel cut to 9 bits, with the step around zero
                double q = std::floor(clampv(y, -1.0, 0.998) * 256.0);
                q += q >= 0.0 ? 3.0 : -3.0;
                y = q / 259.0;
            }
            mix += y;
            if (!voiceAlive(v)) { v.active = false; if (vi < vm.voices()) vm.voiceFinished(vi); }
        }
        mix *= 0.35;
        if (chip && s.engine != kConsole)
        {
            // floating-point converters: a mantissa of 10 bits (12 on the studio's), 10-bit linear for phase distortion
            if (s.engine == kPd) mix = std::round(clampv(mix, -1.0, 1.0) * 512.0) / 512.0;
            else
            {
                const double a = std::fabs(mix);
                if (a > 1e-6)
                {
                    const double q = std::exp2(std::floor(std::log2(a)) - (s.engine == kSix ? 11.0 : 9.0));
                    mix = std::round(mix / q) * q;
                }
                else mix = 0.0;
            }
        }
        return mix;
    }
    bool voiceAlive(const Voice& v) const
    {
        if (set.engine == kPd)
        {
            const int m = clampv(set.algo, 0, 3);
            const bool a = v.op[0].stage != 0, b = v.op[1].stage != 0;
            return m == 1 ? b : (m == 3 ? a || b : a);   // line 1 (and 1') on op 1's envelope, line 2 on op 2's
        }
        const Algo& A = algoFor(set.engine, set.algo);
        for (int k = 0; k < A.n; ++k) if ((A.car >> k & 1) && v.op[k].stage != 0) return true;
        return false;
    }

    inline double fmVoice(Voice& v, bool envNow, bool chip)
    {
        const Settings& s = set;
        const Algo& A = algoFor(s.engine, s.algo);
        const bool waves = engineHasWaves(s.engine);
        const double fbScale = s.feedback > 0 ? std::exp2((double)s.feedback - 7.0) : 0.0;
        double out[kOps] = {};
        double sum = 0.0; int nc = 0;
        for (int oi = 0; oi < A.n; ++oi)
        {
            const int k = A.order[oi];
            Op& o = v.op[k];
            double modc = 0.0;
            for (int j = 0; j < A.n; ++j) if (A.mod[k] >> j & 1) modc += out[j] * 4.0;
            if (k == A.fb && fbScale > 0.0) modc += (o.y1 + o.y2) * fbScale;
            const double att = o.att + tl[k] + o.velAtt + amAtt[k];
            double y = 0.0;
            if (att < kSilent)
            {
                const int wave = waves ? s.op[k].wave : 0;
                if (s.engine == kArcade && k == 3 && s.op[k].wave > 0)
                {
                    // the arcade chip's noise generator on the last operator
                    y = ((noise.next() & 1) ? 1.0 : -1.0) * std::exp2(-att / 6.0206);
                    if (chip) y = std::round(y * 4096.0) / 4096.0;
                }
                else if (chip)
                {
                    int units = (int)(att * (256.0 / 6.0206));
                    if (attStep > 0) units &= ~(attStep - 1);
                    const int idx = (int)std::floor((o.ph + modc) * 1024.0);
                    y = chipWave(idx, units, wave) / 4096.0;
                }
                else y = std::exp2(-att / 6.0206) * floatWave(o.ph + modc, wave);
            }
            o.y2 = o.y1; o.y1 = y;
            out[k] = y;
            o.ph += o.inc; if (o.ph >= 1.0) o.ph -= std::floor(o.ph);
            if (envNow) envTick(o, env[k]);
            if (A.car >> k & 1) { sum += y; ++nc; }
        }
        for (int k = A.n; k < kOps; ++k) if (envNow) envTick(v.op[k], env[k]);   // unused operators still finish
        return nc > 0 ? sum / std::sqrt((double)nc) : 0.0;
    }

    inline double pdVoice(Voice& v, bool envNow)
    {
        const Settings& s = set;
        const double dcwEnv = v.dcw.next();
        if (envNow) { envTick(v.op[0], env[0]); envTick(v.op[1], env[1]); }
        const int mode = clampv(s.algo, 0, 3);
        auto line = [&](double& p, bool& alt, double inc, int wa, int wb, double dcw, double jitter) {
            const int w = (alt && wb > 0) ? wb - 1 : wa;
            const double y = pdWave(p, dcw * dcwEnv * v.dcwKey, w);
            p += inc * (1.0 + jitter);
            if (p >= 1.0) { p -= std::floor(p); alt = !alt; }
            return y;
        };
        auto dca = [&](int k) { const double att = v.op[k].att + tl[k] + v.op[k].velAtt + amAtt[k]; return att < kSilent ? std::exp2(-att / 6.0206) : 0.0; };
        double y1 = 0.0, y2 = 0.0;
        if (mode != 1) y1 = line(v.p1, v.alt1, v.inc1, s.pdWave1, s.pdWave1b, s.pdDcw1, 0.0) * dca(0);
        if (mode != 0)
        {
            const bool copy = mode == 2;
            const double jit = s.pdMod == 2 ? 1.5 * noiseVal() : 0.0;
            y2 = line(v.p2, v.alt2, v.inc2, copy ? s.pdWave1 : s.pdWave2, copy ? s.pdWave1b : s.pdWave2b, copy ? s.pdDcw1 : s.pdDcw2, jit) * dca(copy ? 0 : 1);
            if (s.pdMod == 1 && mode != 1) y2 *= y1 / std::max(1e-6, dca(0));   // ring: line 2 times line 1's wave
        }
        return mode == 0 || mode == 1 ? y1 + y2 : 0.6 * (y1 + y2);
    }
    double noiseVal() { return noise.bi(); }

    Settings set;
    double sr = 48000.0, chipRate = 53267.0;
    bool prepared = false;
    std::array<Voice, kMaxVoices> voices {};
    synth::VoiceManager vm;
    EnvK env[kOps];
    double tl[kOps] = {}, amAtt[kOps] = {};
    int envEvery = 1, attStep = 0;
    uint32_t envCount = 0;
    uint64_t ctl = 0;
    double lfoPh = 0.0, held = 0.0, toneK = 1.0, tone1 = 0.0, tone2 = 0.0, dcX = 0.0, dcY = 0.0;
    fx::Rng noise { 0x1F2E3D4Cu };
    LinearSmoother volS;
    StreamResampler rs;
    std::atomic<int> activeFeed { 0 };
    std::array<std::atomic<float>, kOps> opFeed {};
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
};

}} // namespace pump::phase
