// pump CALM engine: a dynamic resonance suppressor (resonance suppressor-style). Framework-free, header-only.
//
// Per STFT frame (sqrt-Hann, 75% overlap, latency exactly N) and per detection channel:
//   1. power per bin, smoothed in time (a short one-pole across frames) and in frequency (an energy average over a
//      small log-frequency window whose width follows Sharpness), so noise flicker and the fine harmonic structure
//      of tonal sounds don't read as resonances, while real regional emphasis does;
//   2. a smooth spectral envelope that ignores peaks: average in dB over a wide log-frequency window, clip whatever
//      sticks out above it, average again (three times), so the envelope follows the body of the spectrum and its
//      tilt, not its peaks;
//   3. excess = detection level - envelope + the sensitivity curve at that frequency; past the Selectivity threshold
//      it becomes a reduction: Soft = soft knee and a gentle slope, Hard = pulls the peak down onto the threshold
//      line; scaled by Depth, capped, weighted by the soft-edged focus range and a low-level gate;
//   4. the reduction is spread to neighbouring bins (sliding max then average, so the notch keeps its depth but
//      gets smooth flanks), runs through per-bin attack/release (in frames), is lightly smoothed across bins, and is
//      applied as real gains to the complex bins (zero-phase per frame, smooth across frequency: no ringing).
// Stereo: Linked (one detector on the summed power, same gains on both channels), Stereo L/R or Mid/Side (two
// detectors; M/S is done on the spectra). Delta = delayed dry - processed (what is taken away). Mix is aligned to
// the latency sample-exactly.
#pragma once

#include "dsp/FFT.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/Util.hpp"
#include "CalmParams.hpp"
#include <atomic>

namespace pump { namespace calm {

struct NodeSpec {
    float freq = 1000.0f, gain = 0.0f, q = 1.0f;
    bool operator==(const NodeSpec& o) const { return freq == o.freq && gain == o.gain && q == o.q; }
    bool operator!=(const NodeSpec& o) const { return !(*this == o); }
};

struct Settings {
    float depth = 50.0f, sharpness = 50.0f, selectivity = 50.0f;   // percent
    float attackMs = 8.0f, releaseMs = 120.0f;
    int mode = kSoft;
    float focusLo = 80.0f, focusHi = 20000.0f;
    int stereo = kLinked;
    float balance = 0.0f;   // -100 (only channel 1: left / mid) .. +100 (only channel 2: right / side)
    int quality = 1;        // index into kQualitySizes
    bool delta = false;
    float mix = 100.0f, trimDb = 0.0f;
    NodeSpec nodes[kNumNodes];
    Settings()
    {
        for (int i = 0; i < kNumNodes; ++i) { nodes[i].freq = kNodeDefFreq[i]; nodes[i].q = kNodeDefQ[i]; nodes[i].gain = 0.0f; }
    }
};

// The analog shape of one sensitivity node (low shelf, bell or high shelf).
inline BandSpec nodeBand(int i, const NodeSpec& n)
{
    BandSpec b;
    const NodeType t = nodeType(i);
    b.shape = t == kNodeLowShelf ? Shape::LowShelf : (t == kNodeHighShelf ? Shape::HighShelf : Shape::Bell);
    b.freq = n.freq;
    b.gainDb = n.gain;
    b.q = n.q;
    b.slope = 12;
    return b;
}

// The sensitivity curve: the sum (in dB) of the six node shapes. Used by the engine (per bin) and the UI.
struct SensitivityCurve {
    AnalogBand band[kNumNodes];
    bool used[kNumNodes] = {};
    double sr = 48000.0;
    void set(const NodeSpec* nodes, double sampleRate)
    {
        sr = sampleRate;
        for (int i = 0; i < kNumNodes; ++i)
        {
            used[i] = std::fabs(nodes[i].gain) > 1e-4f;
            if (used[i]) band[i] = analogPrototype(nodeBand(i, nodes[i]), sr);
        }
    }
    double db(double f) const
    {
        const double w = kTwoPi * f / sr;
        double m = 1.0;
        for (int i = 0; i < kNumNodes; ++i) if (used[i]) m *= band[i].magSq(w);
        return 10.0 * std::log10(std::max(m, 1e-30));
    }
    double nodeDb(int i, double f) const { return used[i] ? 10.0 * std::log10(std::max(band[i].magSq(kTwoPi * f / sr), 1e-30)) : 0.0; }
};

// FFT size for a quality index at a sample rate: the 48 kHz size times the nearest power of two of sr / 48 kHz,
// so every rate gets the same time and frequency resolution.
inline int fftSizeFor(int quality, double sr)
{
    const double r = sr / 48000.0;
    const int e = (int)std::lround(std::log2(std::max(0.2, r)));
    int n = kQualitySizes[clampv(quality, 0, 3)];
    if (e > 0) n <<= std::min(e, 3); else if (e < 0) n >>= std::min(-e, 2);
    return std::max(256, n);
}

class Engine {
public:
    static constexpr int kUiPoints = 220;
    static constexpr double kUiLo = 20.0, kUiHi = 20000.0;
    static constexpr int kMaxChunk = 4096;

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (int q = 0; q < 4; ++q)
        {
            sizes[q] = fftSizeFor(q, sr);
            stft[q].prepare(sizes[q], 2);
        }
        const int Nmax = sizes[3];
        Kmax = Nmax / 2 + 1;
        auto fa = [&](std::vector<float>& v) { v.assign((size_t)Kmax + 2, 0.0f); };
        for (int c = 0; c < 2; ++c) { fa(Ps[c]); fa(Rs[c]); fa(Rout[c]); fa(Sp[c]); }
        fa(P); fa(Q); fa(D); fa(E); fa(C); fa(T); fa(T1); fa(T2); fa(G); fa(sens); fa(fw);
        pre.assign((size_t)Kmax + 2, 0.0);
        dq.assign((size_t)Kmax + 2, 0);
        for (auto* t : { &detA, &detB, &envA, &envB, &sprA, &sprB }) t->assign((size_t)Kmax + 2, 0);
        for (int c = 0; c < 2; ++c)
        {
            dly[c].setMaxDelay(Nmax + 8);
            dryBuf[c].assign(kMaxChunk, 0.0f);
            wetBuf[c].assign(kMaxChunk, 0.0f);
        }
        mixS.setRamp((int)(0.02 * sr)); mixS.reset(cur.mix * 0.01);
        trimS.setRamp((int)(0.02 * sr)); trimS.reset(dbToGain(cur.trimDb));
        deltaS.setRamp((int)(0.01 * sr)); deltaS.reset(cur.delta ? 1.0 : 0.0);
        swGain.setRamp((int)(0.005 * sr)); swGain.reset(1.0);
        swState = 0; pending = -1;
        active = clampv(cur.quality, 0, 3);
        activate(active);
        for (int c = 0; c < 2; ++c) for (int p = 0; p < kUiPoints; ++p) ui[c][p].store(0.0f, std::memory_order_relaxed);
    }

    void reset()
    {
        stft[active].reset();
        for (int c = 0; c < 2; ++c)
        {
            dly[c].clear();
            std::fill(Ps[c].begin(), Ps[c].end(), 0.0f);
            std::fill(Rs[c].begin(), Rs[c].end(), 0.0f);
        }
    }

    // Settings: call at block start (audio thread).
    void set(const Settings& s)
    {
        if (s.sharpness != cur.sharpness) tablesDirty = true;
        bool nodesChanged = false;
        for (int i = 0; i < kNumNodes; ++i) if (s.nodes[i] != cur.nodes[i]) nodesChanged = true;
        if (nodesChanged || s.focusLo != cur.focusLo || s.focusHi != cur.focusHi) sensDirty = true;
        const int q = clampv(s.quality, 0, 3);
        cur = s;
        cur.quality = q;
        if (q != active && q != pending) { pending = q; if (swState == 0 || swState == 3) { swState = 1; swGain.setTarget(0.0); } }
        else if (q == active && pending >= 0 && swState == 1) { pending = -1; swState = 0; swGain.setTarget(1.0); }   // changed back in time
        mixS.setTarget(clampv(cur.mix, 0.0f, 100.0f) * 0.01);
        trimS.setTarget(dbToGain(cur.trimDb));
        deltaS.setTarget(cur.delta ? 1.0 : 0.0);
    }
    const Settings& settings() const { return cur; }

    int latency() const { return sizes[active]; }
    int fftSize() const { return sizes[active]; }
    int sizeFor(int q) const { return sizes[clampv(q, 0, 3)]; }

    // In place, any block size.
    void process(float* L, float* R, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int m = std::min(kMaxChunk, n - done);
            processChunk(L + done, R + done, m);
            done += m;
        }
    }
    // The dry signal (delayed by the latency) of the last process() call, when it was <= kMaxChunk samples.
    const float* dry(int ch) const { return dryBuf[ch].data(); }

    // ---- UI feeds -----------------------------------------------------------------------------------------------
    // current reduction (dB, >= 0) at display point p (log-spaced kUiLo..kUiHi) for detector channel c
    float uiReduction(int c, int p) const { return ui[c][p].load(std::memory_order_relaxed); }
    static double uiPointFreq(int p) { return kUiLo * std::pow(kUiHi / kUiLo, (p + 0.5) / kUiPoints); }
    MeterValue grMax;          // broadband (energy-weighted) reduction in dB, max-hold until the UI exchanges it
    std::atomic<int> framesDone { 0 };

private:
    void activate(int q)
    {
        active = q;
        N = sizes[q]; K = N / 2 + 1; H = N / 4;
        stft[q].reset();
        for (int c = 0; c < 2; ++c)
        {
            dly[c].setDelay(N);
            dly[c].clear();
            std::fill(Ps[c].begin(), Ps[c].end(), 0.0f);
            std::fill(Rs[c].begin(), Rs[c].end(), 0.0f);
            std::fill(Sp[c].begin(), Sp[c].end(), 0.0f);
        }
        tablesDirty = sensDirty = true;
        buildUiTables();
    }

    void processChunk(float* L, float* R, int n)
    {
        if (swState == 2)
        {
            // the fade-out has finished: switch FFT size now (the output stays muted until the new frames are full)
            activate(pending);
            pending = -1;
            swState = 3;
            muteLeft = N;
        }
        float* io[2] = { wetBuf[0].data(), wetBuf[1].data() };
        for (int i = 0; i < n; ++i)
        {
            wetBuf[0][(size_t)i] = L[i]; wetBuf[1][(size_t)i] = R[i];
            dryBuf[0][(size_t)i] = dly[0].process(L[i]);
            dryBuf[1][(size_t)i] = dly[1].process(R[i]);
        }
        stft[active].process(io, n, &Engine::frameCb, this);
        for (int i = 0; i < n; ++i)
        {
            const float m = (float)mixS.next(), d = (float)deltaS.next();
            float g = (float)trimS.next();
            if (swState == 3) { g = 0.0f; if (--muteLeft <= 0) { swState = 0; swGain.setTarget(1.0); } }
            else
            {
                g *= (float)swGain.next();
                if (swState == 1 && !swGain.isSmoothing() && swGain.current() <= 0.0) swState = 2;
            }
            const float dl = dryBuf[0][(size_t)i], dr = dryBuf[1][(size_t)i];
            const float wl = wetBuf[0][(size_t)i], wr = wetBuf[1][(size_t)i];
            const float nl = dl + (wl - dl) * m, nr = dr + (wr - dr) * m;   // normal (mixed)
            const float xl = (dl - wl) * m, xr = (dr - wr) * m;             // delta: what is removed
            L[i] = (nl + (xl - nl) * d) * g;
            R[i] = (nr + (xr - nr) * d) * g;
        }
    }

    static void frameCb(void* user, float* const* spectra, int channels, int fftSize)
    {
        static_cast<Engine*>(user)->frame(spectra, channels, fftSize);
    }

    // ---- per-frame analysis ------------------------------------------------------------------------------------------
    void buildTables()
    {
        const float s = clampv(cur.sharpness, 0.0f, 100.0f) * 0.01f;
        // half-widths in octaves: detection pooling, envelope, spreading (sharper = narrower everything)
        const double hd = 1.0 / 12.0 + (1.0 / 48.0 - 1.0 / 12.0) * s;
        const double he = 1.0 + (0.35 - 1.0) * s;
        const double hs = 1.0 / 8.0 + (1.0 / 56.0 - 1.0 / 8.0) * s;
        // Windows are at least `minHz` wide on each side, so the detection hears the same thing at every FFT size
        // (a big FFT resolves single harmonics of a note, which must not read as resonances).
        const double binHz = sr / N;
        auto mk = [&](std::vector<int>& A, std::vector<int>& B, double halfOct, double minHz, int minHalf) {
            const double r = std::pow(2.0, halfOct);
            const int mh = std::max(minHalf, (int)std::lround(minHz / binHz));
            for (int k = 0; k < K; ++k)
            {
                const int kk = std::max(k, 1);
                int a = std::min((int)std::floor(kk / r), kk - mh);
                int b = std::max((int)std::ceil(kk * r), kk + mh);
                A[(size_t)k] = std::max(1, a);
                B[(size_t)k] = std::min(K - 1, b);
            }
        };
        mk(detA, detB, hd, 50.0 - 30.0 * s, 0);
        mk(envA, envB, he, 120.0, 4);
        mk(sprA, sprB, hs, 6.0, 1);
        tablesDirty = false;
    }

    void buildSens()
    {
        SensitivityCurve sc;
        sc.set(cur.nodes, sr);
        double lo = std::max(10.0, (double)std::min(cur.focusLo, cur.focusHi));
        double hi = std::max(lo * 1.01, (double)std::max(cur.focusLo, cur.focusHi));
        const double binHz = sr / N;
        const double edge = 0.5;   // octaves of soft edge outside the focus range
        for (int k = 0; k < K; ++k)
        {
            const double f = std::max(1, k) * binHz;
            sens[(size_t)k] = (float)sc.db(f);
            double w = 1.0;
            if (f < lo) { const double t = std::log2(lo / f) / edge; w *= t >= 1.0 ? 0.0 : 0.5 + 0.5 * std::cos(kPi * t); }
            if (f > hi) { const double t = std::log2(f / hi) / edge; w *= t >= 1.0 ? 0.0 : 0.5 + 0.5 * std::cos(kPi * t); }
            // below a few bins the spectrum can't tell a resonance from a note: fade out
            w *= clampv((k - 2.0) / 4.0, 0.0, 1.0);
            fw[(size_t)k] = (float)w;
        }
        sensDirty = false;
    }

    void buildUiTables()
    {
        const double binHz = sr / N;
        for (int p = 0; p < kUiPoints; ++p)
        {
            const double f0 = kUiLo * std::pow(kUiHi / kUiLo, (double)p / kUiPoints);
            const double f1 = kUiLo * std::pow(kUiHi / kUiLo, (double)(p + 1) / kUiPoints);
            int k0 = (int)std::ceil(f0 / binHz), k1 = (int)std::floor(f1 / binHz);
            k0 = clampv(k0, 1, K - 1); k1 = clampv(k1, 0, K - 1);
            uiK0[p] = k0; uiK1[p] = k1;
            const double fc = std::sqrt(f0 * f1) / binHz;
            uiKi[p] = clampv((int)std::floor(fc), 1, K - 2);
            uiFr[p] = (float)clampv(fc - uiKi[p], 0.0, 1.0);
        }
    }

    // mean of x over [A[k], B[k]] for every k (prefix sums; windows grow monotonically)
    void boxMean(const float* x, float* out, const std::vector<int>& A, const std::vector<int>& B)
    {
        pre[0] = 0.0;
        for (int i = 0; i < K; ++i) pre[(size_t)i + 1] = pre[(size_t)i] + x[i];
        for (int k = 0; k < K; ++k)
        {
            const int a = A[(size_t)k], b = B[(size_t)k];
            out[k] = (float)((pre[(size_t)b + 1] - pre[(size_t)a]) / (double)(b - a + 1));
        }
    }
    // max of x over [A[k], B[k]] (monotonic deque, O(K))
    void slidingMax(const float* x, float* out, const std::vector<int>& A, const std::vector<int>& B)
    {
        int head = 0, tail = 0, next = 1;
        for (int k = 1; k < K; ++k)
        {
            while (next <= B[(size_t)k])
            {
                while (tail > head && x[dq[(size_t)tail - 1]] <= x[next]) --tail;
                dq[(size_t)tail++] = next++;
            }
            while (head < tail && dq[(size_t)head] < A[(size_t)k]) ++head;
            out[k] = head < tail ? x[dq[(size_t)head]] : x[k];
        }
        out[0] = out[1];
    }

    // One detector: power P[0..K) in, gains G[0..K) and the final reduction Rout[c] out.
    void analyze(int c, float depthMul)
    {
        const double hopSec = (double)H / sr;
        // detection smoothing in time: short (it should hear changes, but not every flicker of noise)
        const float aT = (float)(1.0 - std::exp(-hopSec / 0.014));
        float* ps = Ps[c].data();
        for (int k = 0; k < K; ++k) ps[k] += (P[(size_t)k] - ps[k]) * aT;
        // energy average over the detection window, then dB
        boxMean(ps, Q.data(), detA, detB);
        for (int k = 0; k < K; ++k) D[(size_t)k] = 10.0f * std::log10(Q[(size_t)k] + 1e-24f);
        D[0] = D[1];
        // peak-ignoring envelope
        boxMean(D.data(), E.data(), envA, envB);
        static const float margins[3] = { 2.0f, 1.0f, 0.5f };
        for (float mg : margins)
        {
            for (int k = 0; k < K; ++k) C[(size_t)k] = std::min(D[(size_t)k], E[(size_t)k] + mg);
            boxMean(C.data(), E.data(), envA, envB);
        }
        // the clipped envelope sits in the valleys; lift it most of the way back toward the local mean (deviations
        // limited to +-6 dB so a resonance can't pull it up), so broad, dense regions don't read as excess
        for (int k = 0; k < K; ++k) C[(size_t)k] = clampv(D[(size_t)k] - E[(size_t)k], -6.0f, 6.0f);
        boxMean(C.data(), T1.data(), envA, envB);
        for (int k = 0; k < K; ++k) E[(size_t)k] += 0.7f * T1[(size_t)k];
        // excess -> reduction target
        const float depth = clampv(cur.depth, 0.0f, 100.0f) * 0.01f * depthMul;
        const float sel = clampv(cur.selectivity, 0.0f, 100.0f) * 0.01f;
        const bool hard = cur.mode == kHard;
        // threshold above the envelope: Selectivity raises it; more Depth also lowers it a little
        const float thr = 0.7f * (hard ? 0.5f + 6.0f * sel : 1.0f + 6.0f * sel) * std::max(0.35f, 1.5f - depth);
        // soft knee, but never reaching below 1 dB above the envelope (noise flicker lives there)
        const float knee = std::max(0.5f, std::min(hard ? 1.5f : 6.0f, 2.0f * (thr - 1.0f)));
        // how much of the excess is taken away: Soft leaves some of the peak, Hard pulls it onto the threshold line
        const float slope = hard ? std::min(1.0f, 0.4f + 1.2f * depth) : std::min(1.0f, 0.25f + 1.2f * depth);
        const float cap = 2.0f + 22.0f * depth;
        const float floorDb = -125.0f;
        // persistence: a resonance is excess that keeps coming back. Excess beyond what has persisted recently
        // (a running average) plus a margin is not counted, so transients and the moving harmonics of a melody are
        // left alone while steady ringing is caught
        // (window 0.15..1.35 s and margin both follow Selectivity: more selective = only long-lived peaks count)
        const float ap = (float)(1.0 - std::exp(-hopSec / (0.15 + 1.2 * sel)));
        const float pm = depth + 2.0f * (1.0f - sel);
        float* sp = Sp[c].data();
        if (depth <= 0.0f)
        {
            std::fill(T.begin(), T.begin() + K, 0.0f);
            for (int k = 0; k < K; ++k) sp[k] += (std::max(0.0f, D[(size_t)k] - E[(size_t)k]) - sp[k]) * ap;
        }
        else
            for (int k = 0; k < K; ++k)
            {
                float x0 = D[(size_t)k] - E[(size_t)k];
                sp[k] += (std::max(0.0f, x0) - sp[k]) * ap;
                x0 = std::min(x0, sp[k] + pm);
                const float x = x0 + sens[(size_t)k] - thr;
                float r;
                if (x <= -0.5f * knee) r = 0.0f;
                else if (x < 0.5f * knee) { const float u = x + 0.5f * knee; r = u * u / (2.0f * knee); }
                else r = x;
                r = std::min(r * slope, cap);
                const float gate = clampv((D[(size_t)k] - floorDb) * (1.0f / 12.0f), 0.0f, 1.0f);
                T[(size_t)k] = r * fw[(size_t)k] * gate;
            }
        T[0] = T[1];
        // spread to the neighbours (keep the depth, smooth the flanks)
        slidingMax(T.data(), T1.data(), sprA, sprB);
        boxMean(T1.data(), T2.data(), sprA, sprB);
        // per-bin attack / release
        const float ca = (float)std::exp(-hopSec / std::max(1e-4, cur.attackMs * 1e-3));
        const float cr = (float)std::exp(-hopSec / std::max(1e-3, cur.releaseMs * 1e-3));
        float* rs = Rs[c].data();
        for (int k = 0; k < K; ++k)
        {
            const float t = T2[(size_t)k];
            rs[k] = t + (rs[k] - t) * (t > rs[k] ? ca : cr);
        }
        // light smoothing across bins and gains
        float* ro = Rout[c].data();
        ro[0] = rs[0];
        ro[K - 1] = rs[K - 1];
        for (int k = 1; k < K - 1; ++k) ro[k] = 0.25f * rs[k - 1] + 0.5f * rs[k] + 0.25f * rs[k + 1];
        for (int k = 0; k < K; ++k) G[(size_t)k] = ro[k] > 1e-5f ? dbToGainF(-ro[k]) : 1.0f;
    }

    static inline float binPower(const float* s, int k, int K)
    {
        if (k == 0) return s[0] * s[0];
        if (k == K - 1) return s[1] * s[1];
        return s[2 * k] * s[2 * k] + s[2 * k + 1] * s[2 * k + 1];
    }
    void applyGains(float* s)
    {
        s[0] *= G[0];
        s[1] *= G[(size_t)K - 1];
        for (int k = 1; k < K - 1; ++k) { s[2 * k] *= G[(size_t)k]; s[2 * k + 1] *= G[(size_t)k]; }
    }
    void publish(int c)
    {
        const float* r = Rout[c].data();
        for (int p = 0; p < kUiPoints; ++p)
        {
            float v;
            if (uiK1[p] >= uiK0[p]) { v = 0.0f; for (int k = uiK0[p]; k <= uiK1[p]; ++k) v = std::max(v, r[k]); }
            else v = r[uiKi[p]] + (r[uiKi[p] + 1] - r[uiKi[p]]) * uiFr[p];
            ui[c][p].store(v, std::memory_order_relaxed);
        }
    }

    void frame(float* const* spec, int channels, int fftSize)
    {
        if (fftSize != N) return;   // (never: the active STFT has size N)
        if (tablesDirty) buildTables();
        if (sensDirty) buildSens();
        float* L = spec[0];
        float* R = channels > 1 ? spec[1] : spec[0];
        const int st = cur.stereo;
        // norm: a full-scale sine reads about 0 dB in its peak bin (sine window: peak = N / pi)
        const float norm = (float)(kPi * kPi / ((double)N * (double)N));
        if (st == kMidSide)
            for (int i = 0; i < N; ++i) { const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]); L[i] = m; R[i] = s; }
        double eIn = 0.0, eOut = 0.0;
        if (st == kLinked)
        {
            for (int k = 0; k < K; ++k) P[(size_t)k] = 0.5f * (binPower(L, k, K) + binPower(R, k, K)) * norm;
            analyze(0, 1.0f);
            for (int k = 0; k < K; ++k) { eIn += P[(size_t)k]; eOut += P[(size_t)k] * G[(size_t)k] * G[(size_t)k]; }
            applyGains(L);
            applyGains(R);
            publish(0);
            for (int p = 0; p < kUiPoints; ++p) ui[1][p].store(ui[0][p].load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        else
        {
            const float b = clampv(cur.balance, -100.0f, 100.0f) * 0.01f;
            const float dm[2] = { std::min(1.0f, 1.0f - b), std::min(1.0f, 1.0f + b) };
            for (int c = 0; c < 2; ++c)
            {
                float* s = c == 0 ? L : R;
                for (int k = 0; k < K; ++k) P[(size_t)k] = binPower(s, k, K) * norm;
                analyze(c, dm[c]);
                for (int k = 0; k < K; ++k) { eIn += P[(size_t)k]; eOut += P[(size_t)k] * G[(size_t)k] * G[(size_t)k]; }
                applyGains(s);
                publish(c);
            }
        }
        if (st == kMidSide)
            for (int i = 0; i < N; ++i) { const float m = L[i], s = R[i]; L[i] = m + s; R[i] = m - s; }
        if (eIn > 1e-20) grMax.setMax((float)(10.0 * std::log10(eIn / std::max(eOut, 1e-30))));
        framesDone.fetch_add(1, std::memory_order_relaxed);
    }

    // ---- data ----------------------------------------------------------------------------------------------------
    double sr = 48000.0;
    Settings cur;
    STFT stft[4];
    int sizes[4] = { 1024, 2048, 4096, 8192 };
    int active = 1, pending = -1;
    int N = 2048, K = 1025, H = 512, Kmax = 4097;
    bool tablesDirty = true, sensDirty = true;
    std::vector<float> Ps[2], Rs[2], Rout[2], Sp[2], P, Q, D, E, C, T, T1, T2, G, sens, fw;
    std::vector<double> pre;
    std::vector<int> dq, detA, detB, envA, envB, sprA, sprB;
    int uiK0[kUiPoints] = {}, uiK1[kUiPoints] = {}, uiKi[kUiPoints] = {};
    float uiFr[kUiPoints] = {};
    std::atomic<float> ui[2][kUiPoints];
    DelayLine dly[2];
    std::vector<float> dryBuf[2], wetBuf[2];
    LinearSmoother mixS, trimS, deltaS, swGain;
    int swState = 0;   // 0 idle, 1 fading out before an FFT size change, 2 switch now, 3 muted while the new frames fill
    int muteLeft = 0;
};

}} // namespace pump::calm
