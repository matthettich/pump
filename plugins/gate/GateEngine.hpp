// pump GATE engine: a tempo-synced sequenced (step) gate. Framework-free.
//
// Timing. The engine keeps a musical position in quarter notes and works out, for every sample, where that sample
// falls in the pattern. While the host plays with a valid position, each block starts at the host's song position
// (ppq) and advances sample by sample at the host tempo, so step edges land on the exact sample inside the block,
// and a loop or a jump simply resyncs at the next block. When the host is stopped (or sends no position) the
// position free-runs at the host tempo (120 BPM when there is none), so the gate keeps pulsing.
//   steps       RATE is a note division (Tempo.hpp). Step n of the song starts at n x RATE from the song's start.
//   swing       steps go in pairs, counted from the song's start (the groove stays on the beat whatever the pattern
//               length): the second step of each pair starts SWING x a step late (33 % = a triplet shuffle), so the
//               first step lasts 1 + swing steps and the second 1 - swing.
//   pattern     song step n plays pattern step n mod STEPS. The right channel plays the pattern rotated by STEREO
//               OFFSET steps (what the left played OFFSET steps ago, on the same swung grid): stereo motion.
//   gate        each step opens for GATE LENGTH of its own (swung) duration at its level, then closes (100 % =
//               legato: equal neighbours never dip).
// The envelope (the gate's openness, 0..1), three stages per sample:
//   SMOOTH      a one-pole glide on the step levels (time constant up to 0.45 step): hard steps -> rolling tremolo
//   ATTACK /    linear slews: rising takes ATTACK for 0 -> 1, falling takes RELEASE for 1 -> 0 (0 = immediate)
//   RELEASE
//   rounding    two one-poles (0.35 ms each, critically damped): the minimum ramp, so even ATTACK and RELEASE 0
//               never click (a full-scale edge moves the gain at most ~2.2 % per sample at 48 kHz; the edge starts
//               on its sample and is half way after 0.59 ms)
// What a closed step does (MODE); DEPTH = how far it closes:
//   Volume      gain = 1 - DEPTH x (1 - openness)
//   Filter      a resonant 12 dB/oct low-pass (TPT SVF, stable while it moves) at FILTER HIGH when open, gliding
//               exponentially toward FILTER LOW as the step closes (DEPTH scales how far). RESONANCE = Q 0.55..8;
//               the passband is pulled down as the filter closes and resonates (to -6 dB at full resonance) so
//               the peak stays in proportion.
//   Both        the two together
// Mode changes crossfade (20 ms); depth, filter range, resonance, mix and output glide (20 ms). No latency: MIX blends
// the dry input sample-exactly. UI feeds: the position, the step each channel is on, the openness (at each
// block's end), the bar length, and the output's peak along the pattern (8 bins per step) for the grid's audio lane.
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/Tempo.hpp"
#include "GateParams.hpp"

namespace pump { namespace gate {

struct Settings {
    int steps = 16, rate = kNote16, mode = kVolume, offset = 0;
    double swing = 0.0, length = 75.0, attack = 1.0, release = 20.0, smooth = 0.0, depth = 100.0;
    double filterLow = 250.0, filterHigh = 20000.0, resonance = 25.0, mix = 100.0, outDb = 0.0;
    float level[kMaxSteps];   // 0..100 %
    Settings() { for (int k = 0; k < kMaxSteps; ++k) level[k] = defaultLevel(k); }
};

// ---- the pattern clock (shared with the UI's envelope display) ------------------------------------------------------
struct StepPos {
    long long global = 0;   // step number counted from the song's start
    double phase = 0.0;     // 0..1 through this step's (swung) duration
};
// (a position within 1e-7 step of an edge counts as past it: a host's ppq, and q0 + i x dq, carry rounding errors,
// and an edge must not slip a sample because 3.25 arrived as 3.2499999999999996)
inline StepPos locate(double q, double stepQ, double swing)
{
    const double s = q / stepQ + 1e-7;
    const double pair = std::floor(s * 0.5);
    const double w = s - 2.0 * pair;   // 0..2 through the pair
    const double split = 1.0 + swing;
    StepPos p;
    if (w < split) { p.global = (long long)(2.0 * pair); p.phase = w / split; }
    else { p.global = (long long)(2.0 * pair) + 1; p.phase = (w - split) / (2.0 - split); }
    return p;
}
inline int patternStep(long long global, int steps, int rotate)
{
    long long m = (global - rotate) % steps;
    if (m < 0) m += steps;
    return (int)m;
}
// The gate's target at a position: the step's level (0..1) while it is open, 0 after GATE LENGTH.
inline double gateTarget(double q, double stepQ, double swing, int steps, int rotate, double length, const float* level01, int* stepOut = nullptr)
{
    const StepPos p = locate(q, stepQ, swing);
    const int k = patternStep(p.global, steps, rotate);
    if (stepOut) *stepOut = k;
    return p.phase < length ? (double)level01[k] : 0.0;
}

// ---- the envelope ----------------------------------------------------------------------------------------------------
constexpr double kRoundTau = 0.00035;   // seconds, each of the two rounding poles

struct EnvCoefs { double glide = 0.0, up = 1.0, down = 1.0, round = 1.0; };
inline EnvCoefs envCoefs(double sr, double attackMs, double releaseMs, double smoothPct, double stepSeconds)
{
    EnvCoefs c;
    c.up = attackMs > 0.0 ? std::min(1.0, 1.0 / (attackMs * 0.001 * sr)) : 1.0;
    c.down = releaseMs > 0.0 ? std::min(1.0, 1.0 / (releaseMs * 0.001 * sr)) : 1.0;
    const double sm = clampv(smoothPct, 0.0, 100.0) * 0.01;
    c.glide = sm > 0.0 ? tauCoef(0.45 * sm * std::sqrt(sm) * stepSeconds, sr) : 0.0;
    c.round = 1.0 - std::exp(-1.0 / (kRoundTau * sr));
    return c;
}
struct Env {
    double glide = 0.0, slew = 0.0, r1 = 0.0, out = 0.0;
    void reset(double v) { glide = slew = r1 = out = v; }
    inline double next(double target, const EnvCoefs& c)
    {
        glide = target + (glide - target) * c.glide;
        const double d = glide - slew;
        slew += d > 0.0 ? std::min(d, c.up) : std::max(d, -c.down);
        r1 += (slew - r1) * c.round;
        out += (r1 - out) * c.round;
        return out;
    }
    bool finite() const { return std::isfinite(glide + slew + r1 + out); }
};

// The gate's openness after DEPTH over [q0, q1) (quarter notes), for the display: the envelope run at `points`
// samples over the range after one pattern's warm-up. `right` = the right channel (rotated by the offset).
inline void simulate(const Settings& s, double bpm, double q0, double q1, int points, bool right, float* out)
{
    if (points <= 0 || !(q1 > q0)) return;
    const int steps = clampv(s.steps, 1, kMaxSteps);
    const double stepQ = noteQuarters(s.rate);
    const double secPerQ = 60.0 / clampv(bpm, 20.0, 999.0);
    const double dq = (q1 - q0) / points;
    const EnvCoefs c = envCoefs(1.0 / (dq * secPerQ), s.attack, s.release, s.smooth, stepQ * secPerQ);
    float lv[kMaxSteps];
    for (int k = 0; k < kMaxSteps; ++k) lv[k] = clampv(s.level[k], 0.0f, 100.0f) * 0.01f;
    const double sw = clampv(s.swing, 0.0, 75.0) * 0.01, len = clampv(s.length, 5.0, 100.0) * 0.01;
    const int rot = right ? clampv(s.offset, 0, 16) : 0;
    const double dep = clampv(s.depth, 0.0, 100.0) * 0.01;
    const int warm = std::min(100000, (int)std::ceil(steps * stepQ / dq));
    Env e;
    e.reset(gateTarget(q0 - warm * dq, stepQ, sw, steps, rot, len, lv));
    for (int i = -warm; i < points; ++i)
    {
        const double v = e.next(gateTarget(q0 + i * dq, stepQ, sw, steps, rot, len, lv), c);
        if (i >= 0) out[i] = (float)(1.0 - dep * (1.0 - v));
    }
}

class Engine {
public:
    static constexpr int kBinsPerStep = 8, kMaxBins = kMaxSteps * kBinsPerStep;
    static constexpr double kMaxPos = 1e8;   // quarter notes (1.6 years at 120 BPM): beyond is garbage, free-run instead

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        const int ramp = std::max(1, (int)(0.020 * sr));
        for (LinearSmoother* s : { &depthS, &volAmt, &filtAmt, &mixS, &outS, &loS, &hiS, &resS }) s->setRamp(ramp);
        depthS.reset(set.depth * 0.01);
        volAmt.reset(set.mode != kFilter ? 1.0 : 0.0);
        filtAmt.reset(set.mode != kVolume ? 1.0 : 0.0);
        mixS.reset(set.mix * 0.01);
        outS.reset(dbToGain(set.outDb));
        loS.reset(std::log(clampv(set.filterLow, 20.0, 20000.0)));
        hiS.reset(std::log(clampv(set.filterHigh, 20.0, 20000.0)));
        resS.reset(clampv(set.resonance, 0.0, 100.0) * 0.01);
        for (int k = 0; k < kMaxSteps; ++k) lv[k] = clampv(set.level[k], 0.0f, 100.0f) * 0.01f;
        pos = 0.0; locked = false;
        for (auto& b : bins) b.store(0.0f, std::memory_order_relaxed);
        binIdx = -1; binPeak = 0.0f;
        clearState();
    }

    void setSettings(const Settings& s)
    {
        set = s;
        set.steps = clampv(s.steps, 1, kMaxSteps);
        set.rate = clampv(s.rate, 0, kNumNotes - 1);
        set.mode = clampv(s.mode, 0, kNumModes - 1);
        set.offset = clampv(s.offset, 0, 16);
        for (int k = 0; k < kMaxSteps; ++k) lv[k] = clampv(s.level[k], 0.0f, 100.0f) * 0.01f;
    }

    // At the start of every host block: lock to the song position while playing, free-run otherwise.
    void setTransport(const Transport& t)
    {
        bpm = (t.bpm > 1.0 && std::isfinite(t.bpm)) ? clampv(t.bpm, 20.0, 999.0) : 120.0;
        locked = t.playing && t.valid && std::isfinite(t.ppq) && std::fabs(t.ppq) < kMaxPos;
        if (locked) pos = t.ppq;
        if (t.valid && t.barQuarters > 0.0 && t.barQuarters < 64.0) barFeed.store((float)t.barQuarters, std::memory_order_relaxed);
    }

    // ---- feeds (UI) ----
    double positionQ() const { return posFeed.load(std::memory_order_relaxed); }   // at the end of the last block
    int currentStep(int ch) const { return stepFeed[ch & 1].load(std::memory_order_relaxed); }
    float tempo() const { return bpmFeed.load(std::memory_order_relaxed); }
    bool hostLocked() const { return lockFeed.load(std::memory_order_relaxed); }
    float openNow(int ch) const { return openFeed[ch & 1].load(std::memory_order_relaxed); }   // after depth, 0..1
    float barQuarters() const { return barFeed.load(std::memory_order_relaxed); }
    // the output's peak along the pattern: bin b covers [b, b + 1) / (STEPS x 8) of one cycle
    float bin(int b) const { return bins[clampv(b, 0, kMaxBins - 1)].load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n)
    {
        volAmt.setTarget(set.mode != kFilter ? 1.0 : 0.0);
        filtAmt.setTarget(set.mode != kVolume ? 1.0 : 0.0);
        depthS.setTarget(set.depth * 0.01);
        mixS.setTarget(set.mix * 0.01);
        outS.setTarget(dbToGain(set.outDb));
        loS.setTarget(std::log(clampv(set.filterLow, 20.0, 20000.0)));
        hiS.setTarget(std::log(clampv(set.filterHigh, 20.0, 20000.0)));
        resS.setTarget(clampv(set.resonance, 0.0, 100.0) * 0.01);

        const int steps = set.steps, rot = set.offset;
        const double stepQ = noteQuarters(set.rate);
        const double patQ = steps * stepQ;
        const int nb = steps * kBinsPerStep;
        const double dq = bpm / (60.0 * sr);
        const double sw = clampv(set.swing, 0.0, 75.0) * 0.01, len = clampv(set.length, 5.0, 100.0) * 0.01;
        const EnvCoefs c = envCoefs(sr, set.attack, set.release, set.smooth, stepQ * 60.0 / bpm);
        const double q0 = pos;
        const double fMax = std::log(0.45 * sr);
        int st[2] = { 0, 0 };
        float oEnd[2] = { 1.0f, 1.0f };
        for (int i = 0; i < n; ++i)
        {
            const double q = q0 + i * dq;
            const double dp = depthS.next(), va = volAmt.next(), fa = filtAmt.next(), mx = mixS.next(), og = outS.next();
            const double lo = loS.next(), hi = hiS.next(), rs = resS.next();
            // the two channels share the grid; the right plays the pattern rotated
            const StepPos sp = locate(q, stepQ, sw);
            const bool gateOpen = sp.phase < len;
            double Q = 0.0, compFull = 1.0;
            if (fa > 0.0) { Q = 0.55 + 7.45 * rs * rs; compFull = std::pow(Q / 0.55, -0.27); }
            // the filter only runs while it is heard; when it comes back it starts settled on the input (as if it
            // had been passing it all along), so a mode change never starts from silence
            const bool prime = fa > 0.0 && !filterLive;
            for (int ch = 0; ch < 2; ++ch)
            {
                st[ch] = patternStep(sp.global, steps, ch ? rot : 0);
                const double e = env[ch].next(gateOpen ? (double)lv[st[ch]] : 0.0, c);
                const double closed = dp * (1.0 - e);
                oEnd[ch] = (float)(1.0 - closed);
                float* buf = ch ? R : L;
                const double x = buf[i];
                double w = x;
                if (fa > 0.0)
                {
                    if (prime) { svf[ch].ic1 = 0.0; svf[ch].ic2 = x; }
                    const double lf = std::min(fMax, hi + (lo - hi) * closed);
                    if (lf != svfLogF[ch] || Q != svfQ[ch]) { svf[ch].set(std::exp(lf), Q, sr); svfLogF[ch] = lf; svfQ[ch] = Q; }
                    const double y = svf[ch].lowpass(x) * (1.0 + (compFull - 1.0) * closed);
                    w = x + (y - x) * fa;
                }
                w *= 1.0 - va * closed;
                buf[i] = (float)((x + mx * (w - x)) * og);
            }
            filterLive = fa > 0.0;
            // the audio lane: peak per bin of the cycle
            const double cyc = q / patQ;
            const int b = std::min(nb - 1, (int)((cyc - std::floor(cyc)) * nb));
            if (b != binIdx)
            {
                if (binIdx >= 0 && binIdx < kMaxBins) bins[binIdx].store(binPeak, std::memory_order_relaxed);
                binIdx = b; binPeak = 0.0f;
            }
            binPeak = std::max(binPeak, std::max(std::fabs(L[i]), std::fabs(R[i])));
        }
        pos = q0 + n * dq;
        if (!env[0].finite() || !env[1].finite() || !std::isfinite(svf[0].ic1 + svf[0].ic2 + svf[1].ic1 + svf[1].ic2))
        {
            clearState();
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        if (!std::isfinite(pos) || std::fabs(pos) >= kMaxPos) pos = 0.0;
        if (!std::isfinite(binPeak)) binPeak = 0.0f;
        posFeed.store(pos, std::memory_order_relaxed);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (n <= 0) continue;
            stepFeed[ch].store(st[ch], std::memory_order_relaxed);
            openFeed[ch].store(oEnd[ch], std::memory_order_relaxed);
        }
        bpmFeed.store((float)bpm, std::memory_order_relaxed);
        lockFeed.store(locked, std::memory_order_relaxed);
    }

private:
    void clearState()
    {
        for (int ch = 0; ch < 2; ++ch) { env[ch].reset(0.0); svf[ch].reset(); svfLogF[ch] = svfQ[ch] = -1.0; }
        filterLive = false;
    }

    double sr = 48000.0, bpm = 120.0, pos = 0.0;
    bool locked = false, filterLive = false;
    Settings set;
    float lv[kMaxSteps] = {};
    Env env[2];
    fx::Svf svf[2];
    double svfLogF[2] = { -1.0, -1.0 }, svfQ[2] = { -1.0, -1.0 };   // what each filter is set to (skip redesigns)
    LinearSmoother depthS, volAmt, filtAmt, mixS, outS, loS, hiS, resS;
    int binIdx = -1;
    float binPeak = 0.0f;
    std::atomic<float> bins[kMaxBins] = {};
    std::atomic<double> posFeed { 0.0 };
    std::atomic<int> stepFeed[2] = { { 0 }, { 0 } };
    std::atomic<float> bpmFeed { 120.0f }, barFeed { 4.0f };
    std::atomic<float> openFeed[2] = { { 1.0f }, { 1.0f } };
    std::atomic<bool> lockFeed { false };
};

}} // namespace pump::gate
