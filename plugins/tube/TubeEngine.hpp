// pump TUBE engine: creative tube distortion. Framework-free.
//
// Signal path (stereo; the two channels share the power supply):
//   base rate   low cut (12 dB/oct) -> focus pre-emphasis (+9 dB shelf / bell where the tube should bite first)
//   oversampled 1..3 tube stages, each: drive -> grid (with a dynamic bias) -> plate curve -> coupling high-pass
//               (10 Hz) -> +10 dB into the next stage
//   base rate   DC blocker -> focus de-emphasis (the exact inverse of the pre-emphasis, so a clean signal comes out
//               flat) -> tone tilt (+-6 dB around 900 Hz) -> high cut -> auto gain -> mix with the latency-aligned dry
//               signal -> output
//
// The circuits (plate curves, all with a small-signal slope of 1 at the unbiased operating point):
//   Glow      a softly asymmetric tanh: warmth without obvious distortion
//   Triode    3/2-power plate current with a soft cutoff below and grid conduction above: strong 2nd harmonic,
//             compresses on positive swings first
//   Pentode   a sharper, nearly symmetric knee: more odd harmonics, more bite
//   Push-Pull two class-AB tubes in opposition: odd harmonics only. Bias sets the class: hot = class A (clean,
//             round), cold = class B (crossover distortion around zero, crackly on quiet signals)
//   Starve    a triode at a low plate voltage: early cutoff, strong grid blocking; the bias runs away on loud
//             notes and the stage sputters and gates
//   Fuzz      a hard clip (rounded knee; anti-aliased with first-order ADAA when oversampling); Bias below 0 gates
//             it (dying battery), above 0 full-wave rectifies the signal going in (octave up)
// Dynamic bias ("blocking"): when the grid goes positive it draws current and charges the coupling capacitor, which
// pushes the bias negative (fast, a few ms) and lets it recover slowly. Loud notes shift the operating point:
// the asymmetry, compression and (in Starve) the gating all follow the playing.
// Voltage lowers the supply: less headroom (earlier clipping) and a colder bias, toward a starved, sputtering stage.
// Sag: the supply drops with the current drawn (15 ms attack, 220 ms recovery, shared by both channels), so loud
// passages clip earlier and bloom as the supply recovers.
//
// Auto gain keeps the level steady across drive, stages, voltage and bias: the wet signal is divided by the gain the
// chain has at the input's level (its small-signal gain, compressed toward the headroom), then a slow level match
// (300 ms RMS, wet vs dry, -18..+18 dB, frozen in silence) takes out the rest. Circuit changes crossfade (15 ms); stage-count, focus and oversampling changes fade the output out, switch,
// and fade back in (6 ms each way). Oversampling: linear-phase halfband 1x / 2x / 4x / 8x (all prepared up front);
// the dry path is delayed by the same latency, so the mix lines up sample-exactly.
#pragma once

#include "dsp/Oversampler.hpp"
#include "dsp/Fx.hpp"
#include "TubeParams.hpp"

namespace pump { namespace tube {

struct Settings {
    int mode = kTriode;
    double drive = 12.0, bias = 0.0, sag = 25.0, voltage = 100.0;
    int stages = 1, focus = kFocusFull;
    double lowCut = kLowCutOff, tone = 0.0, highCut = kHighCutOff, mix = 100.0, outDb = 0.0;
    bool autoGain = true;
    int oversampling = 2;
};

// ---- the plate curves ----------------------------------------------------------------------------------------------
inline double softplus(double z) { return z > 20.0 ? z : (z < -20.0 ? std::exp(z) : std::log1p(std::exp(z))); }
// plate current of one triode: 3/2 power law, soft cutoff at x = -c (k = how sharp the cutoff knee is)
inline double plateCurrent(double x, double c, double k) { const double p = softplus(k * (x + c)) / k; return p * std::sqrt(p); }
inline double plateSlope(double x, double c, double k)
{
    const double z = k * (x + c);
    return 1.5 * std::sqrt(softplus(z) / k) / (1.0 + std::exp(-z));
}
// grid conduction: positive grid swings are limited by a soft knee at gc
inline double gridKnee(double x, double gc) { return x > 0.0 ? gc * std::tanh(x / gc) : x; }

struct Circuit {
    double c, k, gc;            // cutoff, cutoff sharpness, grid-conduction knee (triode-family curves)
    double block;               // how strongly grid current shifts the bias
    double tauCharge, tauRecover;   // seconds
};
inline const Circuit& circuit(int m)
{
    static const Circuit cs[kNumModes] = {
        { 1.20, 4.0, 0.80, 0.04, 0.010, 0.060 },   // Glow (curve is tanh; constants for blocking only)
        { 1.20, 4.0, 0.80, 0.12, 0.008, 0.080 },   // Triode
        { 1.00, 5.0, 0.90, 0.04, 0.010, 0.060 },   // Pentode (curve is its own)
        { 0.45, 8.0, 0.85, 0.10, 0.008, 0.080 },   // Push-Pull (c = the class at Bias 0; the knee is sharp for crossover)
        { 0.45, 6.0, 0.30, 0.90, 0.002, 0.300 },   // Starve
        { 1.00, 4.0, 0.80, 0.25, 0.004, 0.150 },   // Fuzz
    };
    return cs[clampv(m, 0, kNumModes - 1)];
}

// Per-circuit normalisers (small-signal slope 1 at x = 0, unbiased)
struct Norms {
    double glow, triode, pp, starve;
    Norms()
    {
        const double t = std::tanh(0.15);
        glow = 1.0 - t * t;
        triode = plateSlope(0.0, circuit(kTriode).c, circuit(kTriode).k);
        pp = 2.0 * plateSlope(0.0, circuit(kPushPull).c, circuit(kPushPull).k);   // at Bias 0
        starve = plateSlope(0.0, circuit(kStarve).c, circuit(kStarve).k);
    }
};
inline const Norms& norms() { static const Norms n; return n; }

// Static operating parameters derived from the controls (shared by the engine and the UI's curve display).
struct Operating {
    double V = 1.0;          // supply headroom (1 = full voltage)
    double biasX = 0.0;      // static grid offset (normalised)
    double ppClass = 0.0;    // push-pull class, -1 (cold, class B) .. +1 (hot, class A)
    double rect = 0.0;       // fuzz rectification 0..0.7
};
inline Operating operating(int mode, double biasPct, double voltagePct)
{
    Operating o;
    const double b = clampv(biasPct, -100.0, 100.0) * 0.01;
    const double v = clampv(voltagePct, 0.0, 100.0) * 0.01;
    o.V = 0.12 + 0.88 * v * std::sqrt(v);
    const double starve = (1.0 - v) * (1.0 - v);
    if (mode == kPushPull) { o.ppClass = clampv(b - 1.2 * starve, -1.0, 1.0); o.biasX = 0.0; }
    else if (mode == kFuzz) { o.biasX = 0.8 * std::min(0.0, b) - 0.7 * starve; o.rect = 0.7 * std::max(0.0, b); }
    else o.biasX = 0.6 * b - 0.7 * starve;
    return o;
}

// push-pull: the class sets where each tube cuts off (cold = past zero, a dead zone around the crossover)
inline double ppCutoff(double ppClass) { return 0.45 + 0.6 * ppClass; }

// fuzz: a hard clip with a rounded quadratic knee (linear to 0.6, flat at 1 from 1.4) and its antiderivative, so it
// can be anti-aliased (first-order ADAA) when oversampling is on
constexpr double kFuzzKnee = 0.4;
inline double fuzzClip(double x)
{
    const double a = std::fabs(x), sg = x < 0.0 ? -1.0 : 1.0, k = kFuzzKnee;
    if (a < 1.0 - k) return x;
    if (a > 1.0 + k) return sg;
    const double d = a - (1.0 - k);
    return sg * (a - d * d / (4.0 * k));
}
inline double fuzzClipAD(double x)
{
    const double a = std::fabs(x), k = kFuzzKnee, a0 = 1.0 - k;
    if (a < a0) return 0.5 * a * a;
    auto knee = [&](double t) { const double d = t - a0; return 0.5 * t * t - d * d * d / (12.0 * k); };
    if (a <= 1.0 + k) return knee(a);
    return knee(1.0 + k) + (a - (1.0 + k));
}
// the fuzz's input stage: rectification (octave up) blends the signal toward its full-wave rectified form
inline double fuzzRectify(double xs, double rect) { return xs + rect * (std::fabs(xs) - xs); }

// The plate curve of a stage at grid voltage x (including all bias) for a circuit. (Push-pull: the curve of the
// pair with no bias shift; stageEval below shifts both tubes together.)
inline double shape(int mode, double x, const Operating& o)
{
    switch (mode)
    {
    case kGlow: return std::tanh(x + 0.15) / norms().glow;
    case kPentode:
    {
        const double a = x + 0.05, m = std::fabs(a);
        return a / std::cbrt(1.0 + m * m * m);
    }
    case kPushPull:
    {
        const Circuit& c = circuit(kPushPull);
        const double cc = ppCutoff(o.ppClass);
        return (plateCurrent(gridKnee(x, c.gc), cc, c.k) - plateCurrent(gridKnee(-x, c.gc), cc, c.k)) / norms().pp;
    }
    case kStarve:
    {
        const Circuit& c = circuit(kStarve);
        return plateCurrent(gridKnee(x, c.gc), c.c, c.k) / norms().starve;
    }
    case kFuzz: return fuzzClip(x);
    case kTriode:
    default:
    {
        const Circuit& c = circuit(kTriode);
        return plateCurrent(gridKnee(x, c.gc), c.c, c.k) / norms().triode;
    }
    }
}
// One stage for an input u (already driven) around the static operating point, with a dynamic bias shift b; s0 =
// shape(mode, o.biasX, o), the quiescent output, so silence gives 0 once the bias has recovered. `grid` returns the
// grid voltage of the conducting tube (what charges the coupling capacitor). In push-pull the shift moves both
// tubes the same way (colder), so the pair stays symmetric: odd harmonics only.
inline double stageEval(int mode, double u, double b, const Operating& o, double s0, double& grid, double* adaaPrev = nullptr)
{
    const double xs = u / o.V;
    if (mode == kFuzz)
    {
        const double x = fuzzRectify(xs, o.rect) + o.biasX + b;
        grid = x;
        double y;
        if (adaaPrev)
        {
            const double d = x - *adaaPrev;
            y = std::fabs(d) < 1e-6 ? fuzzClip(0.5 * (x + *adaaPrev)) : (fuzzClipAD(x) - fuzzClipAD(*adaaPrev)) / d;
            *adaaPrev = x;
        }
        else y = fuzzClip(x);
        return o.V * (y - s0);
    }
    if (mode == kPushPull)
    {
        const Circuit& c = circuit(kPushPull);
        const double cc = ppCutoff(o.ppClass);
        grid = std::fabs(xs) + b;
        return o.V * (plateCurrent(gridKnee(xs + b, c.gc), cc, c.k) - plateCurrent(gridKnee(-xs + b, c.gc), cc, c.k)) / norms().pp;
    }
    const double x = xs + o.biasX + b;
    grid = x;
    return o.V * (shape(mode, x, o) - s0);
}
inline double stageSlope(int mode, const Operating& o)
{
    const double h = 1e-4;
    return (shape(mode, o.biasX + h, o) - shape(mode, o.biasX - h, o)) / (2.0 * h);
}

constexpr double kInterstage = 3.16227766;   // +10 dB between stages

// ---- RBJ biquad (pre-/de-emphasis need exact inverses: the de-emphasis swaps numerator and denominator) ----------
struct Bq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double process(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0.0; }
    // from unnormalised (B, A); inverse = from (A, B)
    void set(const double B[3], const double A[3]) { b0 = B[0] / A[0]; b1 = B[1] / A[0]; b2 = B[2] / A[0]; a1 = A[1] / A[0]; a2 = A[2] / A[0]; }
};
// RBJ cookbook: type 0 low shelf, 1 peak, 2 high shelf (S = 1 / Q as given), gain dB
inline void rbj(int type, double f, double q, double gainDb, double sr, double B[3], double A[3])
{
    const double Aa = std::pow(10.0, gainDb / 40.0), w = kTwoPi * clampv(f, 10.0, 0.45 * sr) / sr, cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / (2.0 * q);
    if (type == 1)
    {
        B[0] = 1 + alpha * Aa; B[1] = -2 * cw; B[2] = 1 - alpha * Aa;
        A[0] = 1 + alpha / Aa; A[1] = -2 * cw; A[2] = 1 - alpha / Aa;
        return;
    }
    const double sq = 2.0 * std::sqrt(Aa) * alpha;
    if (type == 0)
    {
        B[0] = Aa * ((Aa + 1) - (Aa - 1) * cw + sq); B[1] = 2 * Aa * ((Aa - 1) - (Aa + 1) * cw); B[2] = Aa * ((Aa + 1) - (Aa - 1) * cw - sq);
        A[0] = (Aa + 1) + (Aa - 1) * cw + sq; A[1] = -2 * ((Aa - 1) + (Aa + 1) * cw); A[2] = (Aa + 1) + (Aa - 1) * cw - sq;
    }
    else
    {
        B[0] = Aa * ((Aa + 1) + (Aa - 1) * cw + sq); B[1] = -2 * Aa * ((Aa - 1) + (Aa + 1) * cw); B[2] = Aa * ((Aa + 1) + (Aa - 1) * cw - sq);
        A[0] = (Aa + 1) - (Aa - 1) * cw + sq; A[1] = 2 * ((Aa - 1) - (Aa + 1) * cw); A[2] = (Aa + 1) - (Aa - 1) * cw - sq;
    }
}

class Engine {
public:
    static constexpr int kChunk = 256;
    static constexpr int kMaxStages = 3;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        int maxLat = 0;
        for (int lg = 1; lg <= 3; ++lg) { os[lg - 1].prepare(lg, kChunk); maxLat = std::max(maxLat, os[lg - 1].latency()); }
        for (int ch = 0; ch < 2; ++ch) { dry[ch].setMaxDelay(maxLat + 2); dry[ch].clear(); }
        for (auto& b : work) b.assign((size_t)kChunk, 0.0f);
        const int ramp = std::max(1, (int)(0.020 * sr));
        driveS.setRamp(ramp); driveS.reset(dbToGain(set.drive));
        mixS.setRamp(ramp); mixS.reset(set.mix * 0.01);
        outS.setRamp(ramp); outS.reset(dbToGain(set.outDb));
        biasS.setRamp(ramp); biasS.reset(set.bias);
        voltS.setRamp(ramp); voltS.reset(set.voltage);
        sagS.setRamp(ramp); sagS.reset(set.sag * 0.01);
        toneS.setRamp(ramp); toneS.reset(set.tone);
        autoBlend.setRamp(ramp); autoBlend.reset(set.autoGain ? 1.0 : 0.0);
        lcOn.setRamp(ramp); lcOn.reset(lowCutActive(set.lowCut) ? 1.0 : 0.0);
        hcOn.setRamp(ramp); hcOn.reset(highCutActive(set.highCut) ? 1.0 : 0.0);
        trans.setRamp(std::max(1, (int)(0.006 * sr))); trans.reset(1.0);
        agcK = 1.0 - tauCoef(0.3, sr);
        agcSmooth = 1.0 - tauCoef(0.15, sr);
        inK = 1.0 - tauCoef(0.3, sr);
        curMode = fromMode = set.mode; modeT = 1.0;
        wantStages = curStages = set.stages; wantLg = curLg = set.oversampling; wantFocus = curFocus = set.focus;
        msDry = msWet = 0.0; agc = 1.0; msIn = 0.0;
        applyStructural();
    }

    // ---- settings (audio thread, block start) ----
    void setSettings(const Settings& s)
    {
        set = s;
        set.stages = clampv(s.stages, 1, kMaxStages);
        wantStages = set.stages;
        wantLg = clampv(s.oversampling, 0, 3);
        wantFocus = clampv(s.focus, 0, kNumFocus - 1);
        pending = wantStages != curStages || wantLg != curLg || wantFocus != curFocus;
    }

    int latency() const { return curLg == 0 ? 0 : os[curLg - 1].latency(); }
    int oversampling() const { return 1 << curLg; }

    // ---- feeds (UI) ----
    float inLevelDb() const { return inFeed.load(std::memory_order_relaxed); }      // input RMS (300 ms)
    float sagNow() const { return sagFeed.load(std::memory_order_relaxed); }        // supply now / nominal (0..1)
    float biasShift() const { return biasFeed.load(std::memory_order_relaxed); }    // stage 1's dynamic bias shift
    float autoGainDb() const { return agFeed.load(std::memory_order_relaxed); }     // the level match in use

    void process(float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += kChunk) processChunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    struct StageState {
        double b[2] = { 0, 0 };          // dynamic bias shift per channel
        double xPrev[2] = { 0, 0 };      // the fuzz's previous grid voltage (ADAA)
        fx::OnePole hp[2];               // coupling capacitor (oversampled rate)
    };

    void designFilters()
    {
        const double osr = sr * (1 << curLg);
        for (auto& st : stages)
            for (int ch = 0; ch < 2; ++ch) st.hp[ch].set(10.0, osr);
        double B[3], A[3];
        switch (curFocus)
        {
        case kFocusLow: rbj(0, 250.0, 0.7, 9.0, sr, B, A); break;
        case kFocusMid: rbj(1, 1000.0, 0.7, 9.0, sr, B, A); break;
        case kFocusHigh: rbj(2, 3000.0, 0.7, 9.0, sr, B, A); break;
        default: B[0] = A[0] = 1.0; B[1] = B[2] = A[1] = A[2] = 0.0; break;
        }
        for (int ch = 0; ch < 2; ++ch) { pre[ch].set(B, A); de[ch].set(A, B); }
        const double osK = 1.0 / osr;
        sagAtt = 1.0 - std::exp(-osK / 0.015);
        sagRel = 1.0 - std::exp(-osK / 0.220);
        for (int m = 0; m < kNumModes; ++m)
        {
            kCharge[m] = 1.0 - std::exp(-osK / circuit(m).tauCharge);
            kRecover[m] = 1.0 - std::exp(-osK / circuit(m).tauRecover);
        }
        modeStep = 1.0 / (0.015 * osr);
    }
    // the parts of the path that change topology: applied while the wet path is faded out
    void applyStructural()
    {
        curStages = wantStages; curLg = wantLg; curFocus = wantFocus;
        if (curLg > 0) os[curLg - 1].reset();
        const int lat = latency();
        for (int ch = 0; ch < 2; ++ch)
        {
            dry[ch].clear(); dry[ch].setDelay(lat);
            pre[ch].reset(); de[ch].reset(); dc[ch].reset(); lc[ch].reset(); hc[ch].reset(); tiltLo[ch].reset(); tiltHi[ch].reset();
            env[ch] = 0.0;
        }
        for (auto& st : stages) for (int ch = 0; ch < 2; ++ch) { st.b[ch] = 0.0; st.xPrev[ch] = 0.0; st.hp[ch].reset(); }
        designFilters();
        for (int ch = 0; ch < 2; ++ch) dc[ch].set(5.0, sr);
        pending = false;
    }
    void clearState()
    {
        if (curLg > 0) os[curLg - 1].reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            dry[ch].clear(); pre[ch].reset(); de[ch].reset(); dc[ch].reset(); lc[ch].reset(); hc[ch].reset();
            tiltLo[ch].reset(); tiltHi[ch].reset(); env[ch] = 0.0;
        }
        for (auto& st : stages) for (int ch = 0; ch < 2; ++ch) { st.b[ch] = 0.0; st.xPrev[ch] = 0.0; st.hp[ch].reset(); }
        msDry = msWet = msIn = 0.0; agc = 1.0;
    }

    void processChunk(float* L, float* R, int n)
    {
        if (pending)
        {
            trans.setTarget(0.0);
            if (!trans.isSmoothing() && trans.current() <= 0.0) { applyStructural(); trans.setTarget(1.0); }
        }
        else trans.setTarget(1.0);
        if (set.mode != curMode) { fromMode = curMode; curMode = set.mode; modeT = 0.0; }

        const int F = 1 << curLg;
        driveS.setTarget(dbToGain(set.drive));
        mixS.setTarget(set.mix * 0.01);
        outS.setTarget(dbToGain(set.outDb));
        biasS.setTarget(set.bias);
        voltS.setTarget(set.voltage);
        sagS.setTarget(set.sag * 0.01);
        toneS.setTarget(set.tone);
        autoBlend.setTarget(set.autoGain ? 1.0 : 0.0);
        lcOn.setTarget(lowCutActive(set.lowCut) ? 1.0 : 0.0);
        hcOn.setTarget(highCutActive(set.highCut) ? 1.0 : 0.0);

        // control-rate values for this chunk (bias / voltage / tone glide per chunk; drive per sample)
        double bias = 0, volt = 0, sagAmt = 0, tone = 0;
        float drv[kChunk];
        for (int i = 0; i < n; ++i) { drv[i] = (float)driveS.next(); bias = biasS.next(); volt = voltS.next(); sagAmt = sagS.next(); tone = toneS.next(); }
        const Operating opCur = operating(curMode, bias, volt), opFrom = operating(fromMode, bias, volt);
        const double s0Cur = shape(curMode, opCur.biasX, opCur), s0From = shape(fromMode, opFrom.biasX, opFrom);
        // static small-signal gain of the chain (for the static part of auto gain)
        double slope = std::max(0.3, std::fabs(stageSlope(curMode, opCur)));
        double chainGain = slope;
        for (int s = 1; s < curStages; ++s) chainGain *= kInterstage * slope;
        // the level the input reaches the curve at (peak-ish, from the 300 ms RMS): auto gain divides by the gain the
        // chain really has at that level (the small-signal gain, compressed toward the headroom), so turning drive
        // up keeps the level right away instead of waiting for the slow level match
        const double aIn = std::sqrt(2.0 * msIn) * 1.4;
        const double headroom = 0.8 * opCur.V * (1.0 - 0.6 * sagAmt * std::min(std::max(env[0], env[1]), 1.2));
        // filters set per chunk
        for (int ch = 0; ch < 2; ++ch)
        {
            lc[ch].set(std::max(10.0, set.lowCut), 0.70710678, sr);
            hc[ch].set(std::min(set.highCut, 0.45 * sr), 0.70710678, sr);
            tiltLo[ch].set(900.0, 0.5, sr);
            tiltHi[ch].set(900.0, 0.5, sr);
        }
        const double tiltG = dbToGain(0.03 * tone);   // +-3 dB per side: +-6 dB tilt

        // ---- base rate in: low cut, pre-emphasis; keep the dry signal (aligned with the latency)
        float* pl = work[0].data();
        float* pr = work[1].data();
        double eIn = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double lcm = lcOn.next();
            double x[2] = { L[i], R[i] };
            for (int ch = 0; ch < 2; ++ch)
            {
                const double h = lc[ch].highpass(x[ch]);
                const double v = lcm >= 1.0 ? h : (lcm <= 0.0 ? x[ch] : x[ch] + (h - x[ch]) * lcm);
                (ch ? pr : pl)[i] = (float)pre[ch].process(v);
                eIn += x[ch] * x[ch];
            }
            work[2][(size_t)i] = dry[0].process(L[i]);
            work[3][(size_t)i] = dry[1].process(R[i]);
        }
        msIn += (eIn / (2.0 * std::max(1, n)) - msIn) * std::min(1.0, n * inK);

        // ---- the tube stages (oversampled)
        float* bufL = pl;
        float* bufR = pr;
        float* const* up = nullptr;
        if (curLg > 0) { up = os[curLg - 1].upsample(pl, pr, n); bufL = up[0]; bufR = up[1]; }
        const Circuit& cc = circuit(curMode);
        const double kc = kCharge[curMode], kr = kRecover[curMode], blk = cc.block;
        const Circuit& cf = circuit(fromMode);
        const double kcF = kCharge[fromMode], krF = kRecover[fromMode];
        double mt = modeT;
        const int total = n * F;
        const bool adaa = curLg > 0;
        for (int j = 0; j < total; ++j)
        {
            const double g = drv[j / F];
            // the supply: shared by both channels; sags with the louder channel's output
            const double e = std::max(env[0], env[1]);
            const double Vs = 1.0 - 0.6 * sagAmt * std::min(e, 1.2);
            Operating oc = opCur; oc.V *= Vs;
            Operating ofm = opFrom; ofm.V *= Vs;
            const double w = mt < 1.0 ? 0.5 - 0.5 * std::cos(kPi * mt) : 1.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* buf = ch ? bufR : bufL;
                double u = (double)buf[j] * g;
                double y = 0.0;
                for (int s = 0; s < curStages; ++s)
                {
                    StageState& st = stages[s];
                    double& b = st.b[ch];
                    double xg = 0.0;
                    const double xpFrom = st.xPrev[ch];   // (the outgoing curve reads the same history)
                    double ys = stageEval(curMode, u, b, oc, s0Cur, xg, adaa ? &st.xPrev[ch] : nullptr);
                    double bk = blk, kch = kc, krc = kr;
                    if (w < 1.0)
                    {
                        double xf = 0.0, xpf = xpFrom;
                        const double yf = stageEval(fromMode, u, b, ofm, s0From, xf, adaa ? &xpf : nullptr);
                        if (curMode != kFuzz && adaa) st.xPrev[ch] = xpf;
                        ys = yf + (ys - yf) * w;
                        xg = xf + (xg - xf) * w;
                        bk = cf.block + (blk - cf.block) * w; kch = kcF + (kc - kcF) * w; krc = krF + (kr - krF) * w;
                    }
                    // grid current charges the coupling capacitor: the bias moves negative, then recovers
                    if (xg > 0.0) b -= bk * kch * xg;
                    b -= b * krc;
                    b = std::max(b, -1.5);
                    ys = st.hp[ch].hp(ys);
                    y = ys;
                    u = ys * kInterstage;
                }
                // supply sag follows the stage output (normalised to the headroom)
                const double a = std::fabs(y) / std::max(0.05, opCur.V);
                env[ch] += (a - env[ch]) * (a > env[ch] ? sagAtt : sagRel);
                buf[j] = (float)y;
            }
            if (mt < 1.0) mt = std::min(1.0, mt + modeStep);
        }
        modeT = mt;
        if (modeT >= 1.0) fromMode = curMode;
        if (curLg > 0) os[curLg - 1].downsample(pl, pr, n);

        // ---- base rate out: DC, de-emphasis, tone, high cut, auto gain, mix, output
        const float* dl = work[2].data();
        const float* dr = work[3].data();
        double chk = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double hcm = hcOn.next(), ab = autoBlend.next(), mx = mixS.next(), og = outS.next(), tr = trans.next();
            const double gs = drv[i] * chainGain;
            const double sat = gs * aIn / std::max(1e-6, headroom);
            const double g = gs / std::sqrt(1.0 + sat * sat);
            double w[2], d[2] = { dl[i], dr[i] };
            for (int ch = 0; ch < 2; ++ch)
            {
                double v = (ch ? pr : pl)[i];
                v = dc[ch].process(v);
                v = de[ch].process(v);
                v = tiltHi[ch].highShelf(tiltLo[ch].lowShelf(v, 1.0 / tiltG), tiltG);
                const double h = hc[ch].lowpass(v);
                v = hcm >= 1.0 ? h : (hcm <= 0.0 ? v : v + (h - v) * hcm);
                w[ch] = v;
                chk += v;
            }
            // auto gain: static (divide by the chain's small-signal gain) + slow level match, stereo-linked
            const double pd = 0.5 * (d[0] * d[0] + d[1] * d[1]);
            const double ws0 = w[0] / g, ws1 = w[1] / g;
            const double pw = 0.5 * (ws0 * ws0 + ws1 * ws1);
            msDry += agcK * (pd - msDry);
            msWet += agcK * (pw - msWet);
            if (msDry > 1e-10 && msWet > 1e-14)
            {
                const double target = clampv(std::sqrt(msDry / msWet), 0.125, 8.0);
                agc += (target - agc) * agcSmooth;
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                const double raw = w[ch];
                const double matched = (ch ? ws1 : ws0) * agc;
                const double wet = raw + (matched - raw) * ab;
                const double o = mx > 0.0 ? d[ch] + mx * (wet - d[ch]) : d[ch];
                (ch ? R : L)[i] = (float)(o * og * tr);   // a structural switch dips the whole output (the dry delay changes too)
            }
        }
        chk += msDry + msWet + agc + env[0] + env[1];
        for (const auto& st : stages) chk += st.b[0] + st.b[1] + st.xPrev[0] + st.xPrev[1];
        if (!std::isfinite(chk))
        {
            clearState();
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        inFeed.store((float)(10.0 * std::log10(std::max(1e-12, msIn))), std::memory_order_relaxed);
        sagFeed.store((float)(1.0 - 0.6 * sagAmt * std::min(std::max(env[0], env[1]), 1.2)), std::memory_order_relaxed);
        biasFeed.store((float)(0.5 * (stages[0].b[0] + stages[0].b[1])), std::memory_order_relaxed);
        agFeed.store((float)(set.autoGain ? gainToDb(agc) : 0.0), std::memory_order_relaxed);
    }

    double sr = 48000.0;
    Settings set;
    Oversampler os[3];
    DelayLine dry[2];
    std::vector<float> work[4];
    StageState stages[kMaxStages];
    Bq pre[2], de[2];
    fx::DcBlocker dc[2];
    fx::Svf lc[2], hc[2], tiltLo[2], tiltHi[2];
    double env[2] = { 0, 0 };
    double kCharge[kNumModes] = {}, kRecover[kNumModes] = {};
    double sagAtt = 0.01, sagRel = 0.001, modeStep = 0.001, modeT = 1.0;
    int curMode = kTriode, fromMode = kTriode;
    int wantStages = 1, curStages = 1, wantLg = 2, curLg = 2, wantFocus = 0, curFocus = 0;
    bool pending = false;
    double msDry = 0, msWet = 0, agc = 1.0, agcK = 1e-4, agcSmooth = 1e-4, msIn = 0.0, inK = 1e-4;
    LinearSmoother driveS, mixS, outS, biasS, voltS, sagS, toneS, autoBlend, lcOn, hcOn, trans;
    std::atomic<float> inFeed { -120.0f }, sagFeed { 1.0f }, biasFeed { 0.0f }, agFeed { 0.0f };
};

}} // namespace pump::tube
