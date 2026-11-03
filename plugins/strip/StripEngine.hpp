// pump STRIP engine: a channel strip that is eight consoles. Framework-free (tested offline in tests/test_strip.cpp).
//
// Signal flow (everything runs at the oversampled rate, so the preamp, the transformer and the dynamics' colour
// don't alias and the EQ curves stay analog-shaped up to 20 kHz):
//
//   in -> input gain -> polarity -> PREAMP (the model's stage + transformer) -> HPF / LPF + the model's voicing
//      -> EQ -> GATE -> COMP (or GATE -> COMP -> EQ) -> output gain (and the input's inverse when AUTO GAIN)
//
// A model is a whole channel, not a skin: its preamp (curve, bias, headroom, transformer), its filter slopes, the
// family its EQ curves come from, its compressor's topology and a fixed voicing (coupling, bandwidth, the low-end
// bump of a transformer). COLOUR scales everything the model adds on its own (preamp harmonics, transformer,
// voicing, the dynamics' distortion); the curves you dial in stay what they are.
//
// The EQ families (designs in buildBands(); every curve is an analog prototype matched to the digital filter):
//   Constant      fixed-Q bells and 12 dB/oct shelves (Q 0.71): the curve you draw is the curve you get
//   Proportional  bell Q grows with gain (broad when gentle, narrow when pushed); shelves overshoot before the
//                 corner (a dip above a low boost, a lift below a cut)
//   Inductor      broad musical bells (narrower cutting than boosting), a low shelf with a strong resonance
//                 (the bump and the dip above it), a gentle high shelf
//   Passive       a low boost that also cuts just above itself (the boost-and-cut trick of passive equalisers),
//                 broad mids, a peaking high boost and 6 dB/oct attenuation shelves
//   Wide          6 dB/oct shelves (tone-control style), broad mids (Studio 68 a little narrower than Broadcast)
//
// The compressors (stereo linked; the detector reads the sidechain, high-passed by SC HPF):
//   Clean      feed-forward, peak detector, 6 dB soft knee, attack/release on the gain in dB
//   VCA        feed-forward, RMS (3 ms) detector, 3 dB knee (Console 79); Console 87: half peak, hard knee
//   Feedback   the detector hears the output: the ratio grows into the curve, softer attack; THRUST takes the
//              lows out of the detector (Discrete)
//   Diode      feedback, 12 dB knee, attack never faster than 2 ms, odd harmonics as it works (Class A)
//   Opto       RMS 5 ms, softening ratio, a fast part and a slow "memory" that charges in sustained
//              compression and releases 4x slower (Valve)
//   Vari-mu    RMS 15 ms, the ratio rises with level over a very wide knee, two-pole attack, even harmonics as
//              it works (Broadcast)
//   Limiter    peak, hard knee, release in the linear gain domain (the breathing), crunch as it works (Studio 68)
// AUTO release (every type): the deeper of a fast follower (0.5 x release) and a slow one (3 x) wins.
#pragma once

#include "dsp/Util.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/Dynamics.hpp"
#include "dsp/Oversampler.hpp"
#include "dsp/Saturation.hpp"
#include "StripParams.hpp"
#include <atomic>

namespace pump { namespace strip {

struct Settings {
    int model = kClassA;
    float input = 0.0f, color = 100.0f;
    bool phase = false;
    float hpf = kHpfOff, lpf = kLpfOff;
    bool eqOn = true;
    float g[4] = { 0, 0, 0, 0 }, f[4] = { 100, 400, 2500, 10000 }, q[4] = { 1, 1, 1, 1 };   // q[0], q[3] unused
    bool lfBell = false, hfBell = false;
    bool gateOn = false; float gateThresh = -50, gateRange = 20, gateRelease = 150; int gateMode = kExpand;
    bool compOn = false; float thresh = -18, ratio = 4, attack = 10, release = 200; bool autoRel = false;
    float makeup = 0, mix = 100, scHpf = kScOff;
    int order = kEqThenDyn;
    float outDb = 0.0f;
    bool autoGain = true;
    int oversampling = 1;   // 0 = 1x, 1 = 2x, 2 = 4x
};

enum Curve : int { kCurveNone = 0, kCurveSoft, kCurveAlg, kCurveRail, kCurveTriode };
enum EqFamily : int { kEqConstant = 0, kEqProportional, kEqInductor, kEqPassive, kEqWide };
enum CompType : int { kCtClean = 0, kCtVca, kCtVcaHard, kCtFeedback, kCtDiode, kCtOpto, kCtVariMu, kCtLimiter };

struct Voice { Shape shape; double f, g, q; int slope; };

struct Profile {
    int curve;            // preamp shaper
    double drive;         // input scale into the shaper (larger = less headroom)
    double bias;          // operating point (even harmonics)
    double xfmr, xfmrHz;  // transformer: low-end saturation amount and its flux corner
    int hpfSlope, lpfSlope;
    int eq;               // EqFamily
    double shelfQ;        // shelf resonance (0.71 = plain)
    double midQ;          // bell Q scale
    int comp;             // CompType
    double compColor;     // distortion the compressor adds as it works
    Voice voice[3];       // fixed voicing (gains scale with COLOUR); g = 0 unused
};

inline const Profile& profile(int model)
{
    static const Profile P[kNumModels] = {
        // Clean: nothing of its own
        { kCurveNone, 0.0, 0.0, 0.0, 50.0, 24, 12, kEqConstant, 0.7071, 1.0, kCtClean, 0.0,
          { { Shape::Bell, 1000, 0, 1, 12 }, { Shape::Bell, 1000, 0, 1, 12 }, { Shape::Bell, 1000, 0, 1, 12 } } },
        // Class A '73: transformer-coupled discrete class-A (strong 2nd, some 3rd), inductor EQ, diode bridge
        { kCurveSoft, 0.45, 0.22, 0.45, 50.0, 18, 12, kEqInductor, 1.45, 0.65, kCtDiode, 0.5,
          { { Shape::LowShelf, 38, 0.7, 0.7071, 12 }, { Shape::HighShelf, 18000, -0.8, 0.7071, 12 }, { Shape::Bell, 1000, 0, 1, 12 } } },
        // Console '79: op-amp stages with headroom (clean until the rails), constant-Q EQ, VCA
        { kCurveRail, 0.55, 0.015, 0.0, 50.0, 18, 12, kEqConstant, 0.7071, 1.0, kCtVca, 0.0,
          { { Shape::LowShelf, 15, -0.6, 0.7071, 12 }, { Shape::HighShelf, 20000, -0.3, 0.7071, 12 }, { Shape::Bell, 1000, 0, 1, 12 } } },
        // Console '87: the second series: a little more drive, proportional Q, shelf dips, hard-knee VCA
        { kCurveRail, 0.6, 0.02, 0.12, 40.0, 18, 12, kEqProportional, 1.25, 1.0, kCtVcaHard, 0.0,
          { { Shape::LowShelf, 15, -0.5, 0.7071, 12 }, { Shape::HighShelf, 12000, 0.25, 0.7071, 12 }, { Shape::Bell, 1000, 0, 1, 12 } } },
        // Discrete '70: discrete op-amps into an output transformer, proportional Q, feedback compressor
        { kCurveAlg, 0.55, 0.08, 0.35, 60.0, 12, 12, kEqProportional, 0.9, 1.0, kCtFeedback, 0.0,
          { { Shape::Bell, 90, 0.6, 0.6, 12 }, { Shape::Bell, 5000, 0.3, 0.5, 12 }, { Shape::Bell, 1000, 0, 1, 12 } } },
        // Valve '60: triode preamp, passive EQ, optical leveler
        { kCurveTriode, 0.9, 0.05, 0.35, 45.0, 12, 12, kEqPassive, 0.55, 0.55, kCtOpto, 0.0,
          { { Shape::LowShelf, 70, 0.6, 0.7071, 12 }, { Shape::HighShelf, 15000, -1.2, 0.7071, 12 }, { Shape::Bell, 1000, 0, 1, 12 } } },
        // Studio '68: hot transistor stages, wide shelves and presence, limiter with crunch
        { kCurveSoft, 0.75, 0.06, 0.2, 70.0, 12, 12, kEqWide, 0.7071, 0.7, kCtLimiter, 0.6,
          { { Shape::Bell, 2800, 0.8, 0.6, 12 }, { Shape::HighShelf, 16000, -0.6, 0.7071, 12 }, { Shape::Bell, 1000, 0, 1, 12 } } },
        // Broadcast '55: heavy valve preamp and transformers, wide gentle EQ, variable-mu
        { kCurveTriode, 0.8, 0.08, 0.55, 60.0, 12, 12, kEqWide, 0.7071, 0.5, kCtVariMu, 0.5,
          { { Shape::Bell, 180, 0.9, 0.6, 12 }, { Shape::HighShelf, 12000, -1.8, 0.7071, 6 }, { Shape::LowShelf, 25, -1.0, 0.7071, 12 } } },
    };
    return P[clampv(model, 0, kNumModels - 1)];
}

// ---- the filter bank: fixed slots so each keeps its state ----
enum Slot : int { kSlotHpf = 0, kSlotLpf, kSlotV0, kSlotV1, kSlotV2, kSlotLf, kSlotLmf, kSlotHmf, kSlotHf, kSlotTrick, kNumSlots };
constexpr int kFirstEqSlot = kSlotLf;   // slots before it run in the filter stage, from it on in the EQ stage

// Bell Q for the proportional family: broad when gentle, narrow when pushed.
inline double proportionalQ(double q, double g) { return q * clampv(0.35 + 0.1 * std::fabs(g), 0.4, 2.0); }

// The bands for a setting. used[i] = false: the slot passes the signal (no gain, filter off, not this model).
inline void buildBands(const Settings& s, BandSpec* b, bool* used)
{
    const Profile& P = profile(s.model);
    const double col = clampv(s.color * 0.01, 0.0, 1.0);
    for (int i = 0; i < kNumSlots; ++i) { used[i] = false; b[i] = BandSpec(); }
    if (hpfActive(s.hpf)) { b[kSlotHpf] = { Shape::LowCut, (double)s.hpf, 0.0, 0.7071, P.hpfSlope }; used[kSlotHpf] = true; }
    if (lpfActive(s.lpf)) { b[kSlotLpf] = { Shape::HighCut, (double)s.lpf, 0.0, 0.7071, P.lpfSlope }; used[kSlotLpf] = true; }
    for (int v = 0; v < 3; ++v)
    {
        const Voice& vo = P.voice[v];
        if (vo.g == 0.0 || col <= 0.0) continue;
        b[kSlotV0 + v] = { vo.shape, vo.f, vo.g * col, vo.q, vo.slope };
        used[kSlotV0 + v] = true;
    }
    if (!s.eqOn) return;
    auto set = [&](int slot, Shape sh, double f, double g, double q, int slope) {
        if (std::fabs(g) < 0.01) return;
        b[slot] = { sh, f, g, q, slope };
        used[slot] = true;
    };
    const double gL = s.g[0], gLm = s.g[1], gHm = s.g[2], gH = s.g[3];
    switch (P.eq)
    {
    case kEqConstant:
        set(kSlotLf, s.lfBell ? Shape::Bell : Shape::LowShelf, s.f[0], gL, s.lfBell ? 0.9 : 0.7071, 12);
        set(kSlotLmf, Shape::Bell, s.f[1], gLm, s.q[1], 12);
        set(kSlotHmf, Shape::Bell, s.f[2], gHm, s.q[2], 12);
        set(kSlotHf, s.hfBell ? Shape::Bell : Shape::HighShelf, s.f[3], gH, s.hfBell ? 0.9 : 0.7071, 12);
        break;
    case kEqProportional:
        set(kSlotLf, s.lfBell ? Shape::Bell : Shape::LowShelf, s.f[0], gL, s.lfBell ? proportionalQ(1.0, gL) : P.shelfQ, 12);
        set(kSlotLmf, Shape::Bell, s.f[1], gLm, proportionalQ(s.q[1], gLm), 12);
        set(kSlotHmf, Shape::Bell, s.f[2], gHm, proportionalQ(s.q[2], gHm), 12);
        set(kSlotHf, s.hfBell ? Shape::Bell : Shape::HighShelf, s.f[3], gH, s.hfBell ? proportionalQ(1.0, gH) : P.shelfQ, 12);
        break;
    case kEqInductor:
        set(kSlotLf, s.lfBell ? Shape::Bell : Shape::LowShelf, s.f[0], gL, s.lfBell ? 0.8 : P.shelfQ, 12);
        set(kSlotLmf, Shape::Bell, s.f[1], gLm, s.q[1] * (gLm < 0 ? 0.9 : P.midQ), 12);
        set(kSlotHmf, Shape::Bell, s.f[2], gHm, s.q[2] * (gHm < 0 ? 0.9 : P.midQ), 12);
        set(kSlotHf, s.hfBell ? Shape::Bell : Shape::HighShelf, s.f[3], gH, s.hfBell ? 0.8 : 0.55, 12);
        break;
    case kEqPassive:
        if (s.lfBell) set(kSlotLf, Shape::Bell, s.f[0], gL, 0.6, 12);
        else if (gL > 0)
        {
            set(kSlotLf, Shape::LowShelf, s.f[0], gL, P.shelfQ, 12);
            // the attenuation network cutting just above the boost: the dip that keeps the low end from muddying
            set(kSlotTrick, Shape::Bell, std::min(2.6 * s.f[0], 2400.0), -0.42 * gL, 0.75, 12);
        }
        else set(kSlotLf, Shape::LowShelf, s.f[0], gL, 0.7071, 6);
        set(kSlotLmf, Shape::Bell, s.f[1], gLm, s.q[1] * P.midQ, 12);
        set(kSlotHmf, Shape::Bell, s.f[2], gHm, s.q[2] * P.midQ, 12);
        if (gH > 0) set(kSlotHf, s.hfBell ? Shape::Bell : Shape::HighShelf, s.f[3], gH, s.hfBell ? 0.7 : 0.6, 12);
        else set(kSlotHf, Shape::HighShelf, s.f[3], gH, 0.7071, 6);
        break;
    case kEqWide:
    default:
        set(kSlotLf, s.lfBell ? Shape::Bell : Shape::LowShelf, s.f[0], gL, 0.6, s.lfBell ? 12 : 6);
        set(kSlotLmf, Shape::Bell, s.f[1], gLm, s.q[1] * P.midQ, 12);
        set(kSlotHmf, Shape::Bell, s.f[2], gHm, s.q[2] * P.midQ, 12);
        set(kSlotHf, s.hfBell ? Shape::Bell : Shape::HighShelf, s.f[3], gH, 0.6, s.hfBell ? 12 : 6);
        break;
    }
}

// ---- the preamp's static curve (the UI draws it too) ----
inline double shaper(int curve, double u)
{
    switch (curve)
    {
    case kCurveSoft: return std::tanh(u);
    case kCurveAlg: return u / std::sqrt(1.0 + u * u);
    case kCurveRail: return (double)satCurve(SatMode::Clip, (float)u);
    case kCurveTriode: return (double)satCurve(SatMode::Triode, (float)u);
    default: return u;
    }
}
struct PreampCurve {
    int curve = kCurveNone;
    double h = 0.0, b = 0.0, s0 = 0.0, norm = 1.0;
    void setup(const Profile& P)
    {
        curve = P.curve; h = P.drive; b = P.bias;
        if (curve == kCurveNone || h <= 0.0) { curve = kCurveNone; return; }
        s0 = shaper(curve, b);
        const double e = 1e-4;
        const double slope = (shaper(curve, b + e) - shaper(curve, b - e)) / (2.0 * e);
        norm = 1.0 / (h * std::max(slope, 1e-6));
    }
    inline double eval(double x) const
    {
        if (curve == kCurveNone) return x;
        return (shaper(curve, h * x + b) - s0) * norm;
    }
};

// ---- the compressors' static curves (the UI draws them too). Gain change in dB (<= 0) for a level. ----
inline double compStatic(int type, double levelDb, double thresh, double ratio)
{
    const double r = std::max(ratio, 1.0), s = 1.0 - 1.0 / r;
    auto knee = [&](double kw) {
        const double over = levelDb - thresh;
        if (kw > 0.0 && std::fabs(over) <= 0.5 * kw) return -s * (over + 0.5 * kw) * (over + 0.5 * kw) / (2.0 * kw);
        return over > 0.0 ? -s * over : 0.0;
    };
    switch (type)
    {
    case kCtClean: return knee(6.0);
    case kCtVca: return knee(3.0);
    case kCtVcaHard: return knee(0.0);
    case kCtFeedback: return knee(8.0);
    case kCtDiode: return knee(12.0);
    case kCtOpto: { const double g = knee(10.0); return g / (1.0 + std::fabs(g) / 30.0); }
    case kCtVariMu: {
        const double W = 12.0, u = std::max(0.0, levelDb - thresh + 0.5 * W);
        return -s * u * u / (u + W);
    }
    case kCtLimiter: return knee(0.0);
    default: return 0.0;
    }
}
// Expander / gate static curve: gain change in dB (<= 0, at most -range).
inline double gateStatic(int mode, double levelDb, double thresh, double range)
{
    if (mode == kGate) return levelDb >= thresh ? 0.0 : -range;
    const double under = thresh - levelDb, kw = 6.0, slope = 2.0;   // 1:3 below the threshold
    double g;
    if (std::fabs(under) <= 0.5 * kw) g = -slope * (under + 0.5 * kw) * (under + 0.5 * kw) / (2.0 * kw);
    else g = under > 0.0 ? -slope * under : 0.0;
    return std::max(g, -range);
}

class Engine {
public:
    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        fadeStep = (float)(1.0 / (0.005 * sr));   // 5 ms each way
        maxBlk = std::max(64, maxBlock);
        applyPending(true);
        fade = 1.0f; fading = 0;
    }

    void setSettings(const Settings& s)
    {
        // switches that change the topology or the latency fade out, switch and fade back in
        const bool topo = s.model != tgt.model || s.oversampling != tgt.oversampling || s.order != tgt.order
                       || s.lfBell != tgt.lfBell || s.hfBell != tgt.hfBell;
        tgt = s;
        if (topo && prepared) fading = -1;
    }

    int latency() const { return os.latency(); }
    double rate() const { return sr; }
    const Settings& current() const { return cur; }

    // telemetry for the UI (the newest block)
    std::atomic<float> compGr { 0.0f }, gateGr { 0.0f }, compLevel { -120.0f }, inLevel { -120.0f };

    void process(float* L, float* R, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int m = std::min(n - done, kChunk);
            processChunk(L + done, R + done, m);
            done += m;
        }
    }

private:
    static constexpr int kChunk = 32;   // control-rate step (base samples)

    // ---- settings ----
    void applyPending(bool hard)
    {
        // the parts that may change the topology: only here, while faded out (or at prepare)
        const int osWant = clampv(tgt.oversampling, 0, 2);
        if (!prepared || osWant != osLog2)
        {
            osLog2 = osWant;
            os.prepare(osLog2, kChunk);
            os.reset();
            hard = true;
        }
        fs = sr * (1 << osLog2);
        cur = tgt;
        shown = tgt;
        pre.setup(profile(cur.model));
        if (hard) resetState();
        else for (auto& sl : slot) sl.reset();
        cGain.reset(gainOf(cur)); oGain.reset(outGainOf(cur));
        cGain.setRamp((int)(0.02 * fs)); oGain.setRamp((int)(0.02 * fs));
        eqMix = cur.eqOn ? 1.0 : 0.0; gateMix = cur.gateOn ? 1.0 : 0.0; compWet = cur.compOn ? cur.mix * 0.01 : 0.0;
        pol = cur.phase ? -1.0 : 1.0;
        redesign(true);
        dynCoefs();
        prepared = true;
    }
    void resetState()
    {
        for (auto& sl : slot) sl.reset();
        for (auto& sc : scS) sc.reset();
        for (int c = 0; c < 2; ++c) { lpX[c] = 0; dcX[c] = dcY[c] = 0; thr[c] = 0; dcC[c] = dcCy[c] = 0; }
        rms = 0; env = 0; env2 = 0; fastF = slowF = 0; optoA = optoB = 0; limG = 1.0;
        gEnv = 0; gLvl = -120; gOpen = false; gHold = 0;
        std::fill(std::begin(lastCount), std::end(lastCount), -1);
    }

    static double gainOf(const Settings& s) { return dbToGain(s.input); }
    static double outGainOf(const Settings& s) { return dbToGain(s.outDb - (s.autoGain ? s.input : 0.0)); }

    struct SlotF {
        DigitalBand d;
        BiquadState st[2][kMaxSections];
        void reset() { for (auto& ch : st) for (auto& x : ch) x.reset(); }
    };

    void redesign(bool full)
    {
        BandSpec b[kNumSlots];
        bool used[kNumSlots];
        Settings d = shown;
        d.eqOn = true;   // the EQ stage cross-fades itself in and out
        buildBands(d, b, used);
        for (int i = 0; i < kNumSlots; ++i)
        {
            on[i] = used[i];
            if (!used[i]) continue;
            const AnalogBand a = analogPrototype(b[i], fs);
            if (full || a.n != slot[i].d.n || lastCount[i] < 0) slot[i].d.design(a, fs);
            else slot[i].d.redesignFast(a);
            if (a.n != lastCount[i]) { for (auto& ch : slot[i].st) for (auto& x : ch) x.reset(); lastCount[i] = a.n; }
        }
        if (scActive(shown.scHpf))
        {
            BandSpec sb { Shape::LowCut, (double)shown.scHpf, 0.0, 0.7071, 12 };
            scD.design(analogPrototype(sb, fs), fs);
            scOn = true;
        }
        else scOn = false;
    }

    // per-chunk smoothing of the continuous values toward tgt (one pole, ~25 ms), then a redesign when they moved
    bool glide(float& v, float t, double a, bool logScale)
    {
        if (v == t) return false;
        if (logScale && v > 0 && t > 0)
        {
            const double r = std::log(t / v);
            if (std::fabs(r) < 1e-4) { v = t; return true; }
            v = (float)(v * std::exp(r * a));
        }
        else
        {
            const double d = t - v;
            if (std::fabs(d) < 1e-3) { v = t; return true; }
            v = (float)(v + d * a);
        }
        return true;
    }

    void controlTick()
    {
        const double a = 1.0 - std::exp(-(double)kChunk / (0.025 * sr));
        bool moved = false;
        moved |= glide(shown.color, tgt.color, a, false);
        // the filter end stops switch off: glide to the stop first (it is "off" there)
        moved |= glide(shown.hpf, tgt.hpf, a, true);
        moved |= glide(shown.lpf, tgt.lpf, a, true);
        moved |= glide(shown.scHpf, tgt.scHpf, a, true);
        for (int i = 0; i < 4; ++i)
        {
            moved |= glide(shown.g[i], tgt.g[i], a, false);
            moved |= glide(shown.f[i], tgt.f[i], a, true);
            moved |= glide(shown.q[i], tgt.q[i], a, true);
        }
        // instant: switches that don't change the topology (they cross-fade below)
        if (moved) { redesign(false); settled = false; }
        else if (!settled) { redesign(true); settled = true; }
        // the rest (dynamics, gains) follow directly; the dynamics smooth themselves
        const bool dynChanged = cur.thresh != tgt.thresh || cur.ratio != tgt.ratio || cur.attack != tgt.attack || cur.release != tgt.release
                             || cur.autoRel != tgt.autoRel || cur.gateRelease != tgt.gateRelease || cur.color != shown.color;
        // everything but the topology (that waits for the fade: applyPending)
        const Settings keep = cur;
        cur = tgt;
        cur.color = shown.color;
        cur.model = keep.model; cur.oversampling = keep.oversampling; cur.order = keep.order;
        cur.lfBell = keep.lfBell; cur.hfBell = keep.hfBell;
        if (dynChanged) dynCoefs();
        cGain.setTarget(gainOf(cur));
        oGain.setTarget(outGainOf(cur));
        colorK = clampv(shown.color * 0.01, 0.0, 1.0);
    }

    void dynCoefs()
    {
        const Profile& P = profile(cur.model);
        ctype = P.comp;
        double atk = cur.attack;
        if (ctype == kCtDiode) atk = std::max(atk, 2.0);
        if (ctype == kCtOpto) atk = std::max(atk, 1.0);
        cA = tauCoef(atk * 0.001, fs);
        cR = tauCoef(cur.release * 0.001, fs);
        cRf = tauCoef(0.5 * cur.release * 0.001, fs);
        cRs = tauCoef(3.0 * cur.release * 0.001, fs);
        cSlowA = tauCoef(0.4, fs);
        cOptoF = tauCoef(0.3 * cur.release * 0.001, fs);
        cOptoS = tauCoef(4.0 * cur.release * 0.001, fs);
        cOptoCharge = tauCoef(0.6, fs);
        const double rmsMs = ctype == kCtVariMu ? 15.0 : (ctype == kCtOpto ? 5.0 : (ctype == kCtVcaHard ? 1.5 : 3.0));
        cRms = tauCoef(rmsMs * 0.001, fs);
        cThrust = tauCoef(1.0 / (kTwoPi * 250.0), fs);
        gA = tauCoef(0.0003, fs);
        gR = tauCoef(cur.gateRelease * 0.001, fs);
        gLvlR = tauCoef(0.010, fs);
        gHoldN = (int)(0.020 * fs);
        compCol = P.compColor;
        xA = 1.0 - tauCoef(1.0 / (kTwoPi * P.xfmrHz), fs);
        dcA = tauCoef(1.0 / (kTwoPi * 5.0), fs);
        stageA = 1.0 - std::exp(-1.0 / (0.010 * fs));   // on/off cross-fades: ~10 ms
    }

    // ---- processing ----
    void processChunk(float* L, float* R, int n)
    {
        controlTick();
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
        inLevel.store(gainToDbF(pk) + (float)cur.input, std::memory_order_relaxed);

        float* const* up = os.upsample(L, R, n);
        const int N = n << osLog2;
        double* a = bufA; double* b = bufB;
        for (int i = 0; i < N; ++i) { a[i] = up[0][i]; b[i] = up[1][i]; }

        stagePreamp(a, b, N);
        stageSlots(a, b, N, 0, kFirstEqSlot);
        blockGr = 0.0; blockGate = 0.0; blockLvl = -120.0;
        if (cur.order == kEqThenDyn) { stageEq(a, b, N); stageDyn(a, b, N); }
        else { stageDyn(a, b, N); stageEq(a, b, N); }
        for (int i = 0; i < N; ++i)
        {
            const double g = oGain.next();
            up[0][i] = (float)(a[i] * g); up[1][i] = (float)(b[i] * g);
        }
        os.downsample(L, R, n);

        // fades for switches that change the topology
        for (int i = 0; i < n; ++i)
        {
            if (fading < 0) { fade -= fadeStep; if (fade <= 0.0f) { fade = 0.0f; applyPending(false); fading = 1; } }
            else if (fading > 0) { fade += fadeStep; if (fade >= 1.0f) { fade = 1.0f; fading = 0; } }
            L[i] *= fade; R[i] *= fade;
        }
        compGr.store((float)blockGr, std::memory_order_relaxed);
        gateGr.store((float)blockGate, std::memory_order_relaxed);
        compLevel.store((float)blockLvl, std::memory_order_relaxed);
    }

    void stagePreamp(double* a, double* b, int N)
    {
        const Profile& P = profile(cur.model);
        const double k = colorK, xf = P.xfmr * colorK;
        const double polT = cur.phase ? -1.0 : 1.0;
        double* ch[2] = { a, b };
        for (int i = 0; i < N; ++i)
        {
            const double g = cGain.next();
            pol += (polT - pol) * stageA;
            for (int c = 0; c < 2; ++c)
            {
                double x = ch[c][i] * g * pol;
                if (pre.curve != kCurveNone && k > 0.0)
                {
                    const double s = pre.eval(x);
                    double y = x + k * (s - x);
                    if (xf > 0.0)
                    {
                        // the transformer's core: flux (the low end) saturates first
                        lpX[c] += xA * (y - lpX[c]);
                        const double fl = lpX[c];
                        y -= xf * (fl - std::tanh(fl * 0.9) / 0.9);
                    }
                    // the bias puts DC on the stage: block it (one pole, 5 Hz)
                    const double dy = y - dcX[c] + dcA * dcY[c];
                    dcX[c] = y; dcY[c] = dy;
                    x = dy;
                }
                ch[c][i] = x;
            }
        }
    }

    void stageSlots(double* a, double* b, int N, int from, int to)
    {
        for (int s = from; s < to; ++s)
        {
            if (!on[s]) continue;
            SlotF& f = slot[s];
            for (int i = 0; i < N; ++i)
            {
                double x = a[i], y = b[i];
                for (int k = 0; k < f.d.n; ++k) { x = f.st[0][k].process(f.d.c[k], x); y = f.st[1][k].process(f.d.c[k], y); }
                a[i] = x; b[i] = y;
            }
            for (auto& ch : f.st) for (auto& x : ch) x.sanitize();
        }
    }

    void stageEq(double* a, double* b, int N)
    {
        const double want = cur.eqOn ? 1.0 : 0.0;
        if (want == 0.0 && eqMix < 1e-5) { eqMix = 0.0; return; }
        if (want == 1.0 && eqMix > 1.0 - 1e-5)
        {
            eqMix = 1.0;
            stageSlots(a, b, N, kFirstEqSlot, kNumSlots);
            return;
        }
        // cross-fade (EQ in/out)
        for (int i = 0; i < N; ++i) { dryA[i] = a[i]; dryB[i] = b[i]; }
        stageSlots(a, b, N, kFirstEqSlot, kNumSlots);
        for (int i = 0; i < N; ++i)
        {
            eqMix += (want - eqMix) * stageA;
            a[i] = dryA[i] + (a[i] - dryA[i]) * eqMix;
            b[i] = dryB[i] + (b[i] - dryB[i]) * eqMix;
        }
    }

    void stageDyn(double* a, double* b, int N)
    {
        const double gateWant = cur.gateOn ? 1.0 : 0.0;
        const double wetWant = cur.compOn ? clampv(cur.mix * 0.01, 0.0, 1.0) : 0.0;
        const bool gateIdle = gateWant == 0.0 && gateMix < 1e-5;
        const bool compIdle = wetWant == 0.0 && compWet < 1e-5;
        if (gateIdle) gateMix = 0.0;
        if (compIdle) compWet = 0.0;
        if (gateIdle && compIdle) return;

        const double r = std::max(1.0, (double)cur.ratio), T = cur.thresh;
        const double makeup = dbToGain(cur.makeup);
        const double cc = compCol * colorK;
        for (int i = 0; i < N; ++i)
        {
            double l = a[i], rr = b[i];
            // sidechain
            double sl = l, sr2 = rr;
            if (scOn) { sl = scS[0].process(scD.c[0], sl); sr2 = scS[1].process(scD.c[0], sr2); }
            const double pk = std::max(std::fabs(sl), std::fabs(sr2));

            // ---- gate / expander (on the sidechain level) ----
            if (!gateIdle)
            {
                const double lvl = gainToDb(pk + 1e-12);
                gLvl = lvl > gLvl ? lvl : lvl + (gLvl - lvl) * gLvlR;
                double tgtG;
                if (cur.gateMode == kGate)
                {
                    if (gLvl >= cur.gateThresh) { gOpen = true; gHold = gHoldN; }
                    else if (gLvl < cur.gateThresh - 4.0) { if (gHold > 0) --gHold; else gOpen = false; }
                    tgtG = gOpen ? 0.0 : -cur.gateRange;
                }
                else tgtG = gateStatic(kExpand, gLvl, cur.gateThresh, cur.gateRange);
                gEnv = tgtG + (gEnv - tgtG) * (tgtG > gEnv ? gR : gA);
                gateMix += (gateWant - gateMix) * stageA;
                const double gg = 1.0 + (dbToGain(gEnv) - 1.0) * gateMix;
                l *= gg; rr *= gg;
                blockGate = std::min(blockGate, gEnv * gateMix);
            }

            // ---- compressor ----
            if (!compIdle)
            {
                const double scl = sl, scr = sr2;   // (the gate does not open the sidechain: it hears the source)
                const double ms = 0.5 * (scl * scl + scr * scr);
                rms = ms + (rms - ms) * cRms;
                double lvl;
                switch (ctype)
                {
                case kCtClean: case kCtLimiter: lvl = gainToDb(pk + 1e-12); break;
                case kCtVcaHard: lvl = 0.5 * (gainToDb(pk + 1e-12) + 10.0 * std::log10(2.0 * rms + 1e-24)); break;
                case kCtFeedback: {
                    // thrust: the detector hears less of the lows
                    double tl = 0.0;
                    for (int c = 0; c < 2; ++c) { const double x = c ? scr : scl; thr[c] = x + (thr[c] - x) * cThrust; tl = std::max(tl, std::fabs(x - 0.65 * thr[c])); }
                    lvl = gainToDb(tl * 1.25 + 1e-12);
                    break;
                }
                default: lvl = 10.0 * std::log10(2.0 * rms + 1e-24); break;
                }
                blockLvl = std::max(blockLvl, lvl);
                double gr = 0.0;
                switch (ctype)
                {
                case kCtFeedback: case kCtDiode: {
                    // the detector hears the output: level + the gain it already applies; the loop's steady state
                    // is the drawn curve (slope r - 1 on the output's overshoot), its step limited so it stays stable
                    const double y = lvl + env;
                    const double kw = ctype == kCtDiode ? 12.0 : 8.0;
                    const double over = y - T;
                    double t;
                    if (std::fabs(over) <= 0.5 * kw) t = -(r - 1.0) * (over + 0.5 * kw) * (over + 0.5 * kw) / (2.0 * kw);
                    else t = over > 0.0 ? -(r - 1.0) * over : 0.0;
                    t = std::max(t, -60.0);
                    const double c = t < env ? cA : (cur.autoRel ? cRf : cR);
                    const double step = std::min(1.0 - c, 1.5 / r);
                    env += (t - env) * step;
                    if (cur.autoRel) { slowF = t < slowF ? t + (slowF - t) * cSlowA : t + (slowF - t) * cRs; gr = std::min(env, slowF); }
                    else gr = env;
                    break;
                }
                case kCtOpto: {
                    const double t = compStatic(kCtOpto, lvl, T, r);
                    // program-dependent attack: big jumps drive the lamp harder
                    const double ca = (t - optoA) < -6.0 ? cA * cA : cA;
                    optoA = t < optoA ? t + (optoA - t) * ca : t + (optoA - t) * (cur.autoRel ? cRf : cOptoF);
                    // the slow memory charges only while it keeps working
                    if (optoA < optoB) optoB = optoA + (optoB - optoA) * cOptoCharge;
                    else optoB = optoA + (optoB - optoA) * cOptoS;
                    gr = 0.6 * optoA + 0.4 * optoB;
                    break;
                }
                case kCtVariMu: {
                    const double t = compStatic(kCtVariMu, lvl, T, r);
                    // two-pole attack (rounded onset)
                    env2 = t < env2 ? t + (env2 - t) * cA : t + (env2 - t) * (cur.autoRel ? cRf : cR);
                    env = env2 < env ? env2 + (env - env2) * cA : env2 + (env - env2) * (cur.autoRel ? cRf : cR);
                    gr = cur.autoRel ? std::min(env, autoSlow(t)) : env;
                    break;
                }
                case kCtLimiter: {
                    // release in the linear gain domain: quick out of deep reduction, then a soft landing
                    const double t = compStatic(kCtLimiter, lvl, T, r);
                    const double gt = dbToGain(t);
                    limG = gt < limG ? gt + (limG - gt) * cA : gt + (limG - gt) * (cur.autoRel ? cRf : cR);
                    gr = gainToDb(limG);
                    break;
                }
                default: {   // Clean, VCA, VCA hard: feed-forward, attack/release on the gain in dB
                    const double t = compStatic(ctype, lvl, T, r);
                    env = t < env ? t + (env - t) * cA : t + (env - t) * (cur.autoRel ? cRf : cR);
                    gr = cur.autoRel ? std::min(env, autoSlow(t)) : env;
                    break;
                }
                }
                compWet += (wetWant - compWet) * stageA;
                const double g = dbToGain(gr) * makeup;
                double wl = l * g, wr = rr * g;
                if (cc > 0.0 && gr < -0.05)
                {
                    const double depth = std::min(1.0, -gr / 12.0) * cc;
                    if (ctype == kCtVariMu)
                    {
                        // even harmonics from the biased tubes, DC-blocked
                        double* w[2] = { &wl, &wr };
                        for (int c = 0; c < 2; ++c)
                        {
                            const double y = *w[c] + 0.12 * depth * (*w[c]) * (*w[c]);
                            const double d = y - dcC[c] + dcA * dcCy[c];
                            dcC[c] = y; dcCy[c] = d; *w[c] = d;
                        }
                    }
                    else
                    {
                        // diode bridge / limiter: odd harmonics as it works
                        const double k = ctype == kCtLimiter ? 1.8 : 1.3;
                        wl += depth * (std::tanh(k * wl) / k - wl);
                        wr += depth * (std::tanh(k * wr) / k - wr);
                    }
                }
                l = l + (wl - l) * compWet;
                rr = rr + (wr - rr) * compWet;
                blockGr = std::min(blockGr, gr * compWet);
            }
            a[i] = l; b[i] = rr;
        }
        if (!std::isfinite(env) || !std::isfinite(env2) || !std::isfinite(optoA) || !std::isfinite(optoB) || !std::isfinite(limG) || !std::isfinite(rms))
        { env = env2 = optoA = optoB = slowF = 0.0; limG = 1.0; rms = 0.0; }
        scS[0].sanitize(); scS[1].sanitize();
    }
    double autoSlow(double t)
    {
        slowF = t < slowF ? t + (slowF - t) * cSlowA : t + (slowF - t) * cRs;
        return slowF;
    }

    // ---- state ----
    double sr = 48000.0, fs = 48000.0;
    int maxBlk = 512, osLog2 = -1;
    bool prepared = false, settled = false;
    Settings tgt, cur, shown;   // wanted; in use; the filters' gliding values
    Oversampler os;
    PreampCurve pre;
    SlotF slot[kNumSlots];
    bool on[kNumSlots] = {};
    int lastCount[kNumSlots];
    DigitalBand scD; BiquadState scS[2]; bool scOn = false;
    LinearSmoother cGain, oGain;
    double pol = 1.0, colorK = 1.0, eqMix = 1.0, gateMix = 0.0, compWet = 0.0;
    float fade = 1.0f, fadeStep = 1.0f / 240.0f; int fading = 0;
    // preamp
    double lpX[2] = {}, dcX[2] = {}, dcY[2] = {}, xA = 0.0, dcA = 0.0;
    // dynamics
    int ctype = kCtClean;
    double cA = 0, cR = 0, cRf = 0, cRs = 0, cSlowA = 0, cOptoF = 0, cOptoS = 0, cOptoCharge = 0, cRms = 0, cThrust = 0, stageA = 0.01;
    double rms = 0, env = 0, env2 = 0, fastF = 0, slowF = 0, optoA = 0, optoB = 0, limG = 1.0, thr[2] = {}, dcC[2] = {}, dcCy[2] = {};
    double compCol = 0.0;
    double gA = 0, gR = 0, gLvlR = 0, gEnv = 0, gLvl = -120; bool gOpen = false; int gHold = 0, gHoldN = 960;
    double blockGr = 0, blockGate = 0, blockLvl = -120;
    double bufA[kChunk * 4], bufB[kChunk * 4], dryA[kChunk * 4], dryB[kChunk * 4];
};

}} // namespace pump::strip
