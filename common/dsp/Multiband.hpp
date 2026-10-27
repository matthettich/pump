// pump multiband: the band splitter shared by BAND, WIDE and HEAT. Framework-free.
//
//  Analog:  a 4-leaf Linkwitz-Riley 24 dB/oct tree (the common LR4Split / LR4Allpass), zero latency: a root split
//           at crossover 2, the low side split at crossover 1 and the high side at crossover 3, each side with the
//           other side's allpass, so every leaf is the same allpass times a real non-negative magnitude. All
//           sections always run; fewer bands park the unused split points at the top of the spectrum and route
//           several leaves into one band. Changing the band count glides the split points and ramps the routing
//           (~20 ms), so it never clicks, and the bands always sum exactly to the tree's allpass.
//  Linear:  zero-phase FIR bands with exactly the same LR4 magnitudes (they add up to exactly 1: |LP| + |HP| = 1
//           for LR4), one uniformly partitioned FFT convolution that transforms the input once for all bands. The
//           last band is the delayed input minus the others, so the bands always sum to the delayed input exactly,
//           also while new kernels crossfade in. Kernels are designed on a worker thread into a spare slot and
//           swapped in (crossfaded over one 512-sample block) with no locks or allocation on the audio thread.
//           They are only asked for while linear phase is in use (a crossover dragged or automated in Analog mode
//           would otherwise keep the worker designing kernels nobody hears); switching to Linear asks for the
//           current settings, built while the engines' crossover-type fade (and the FIR's own latency) runs.
// Non-finite input (the plugins sanitize theirs) can't get stuck: the analog tree's states are checked once per
// process() call and cleared if they went non-finite; the linear path is FIR only and flushes by itself.
#pragma once

#include "dsp/Crossover.hpp"
#include "dsp/FFT.hpp"
#include <atomic>
#if !PUMP_SYNC_KERNELS
 #include <chrono>
 #include <thread>
#endif

namespace pump { namespace mb {

constexpr int kMaxBands = 4;
constexpr double kMinXover = 20.0, kMaxXover = 20000.0;
constexpr double kMinRatio = 1.2;   // neighbouring crossovers stay at least ~1/4 octave apart

// The crossover frequencies in use for nb bands, from (possibly unordered) parameter values: the first nb-1 are
// used, clamped to the audio range and kept kMinRatio apart. Unused ones are placed above the last (display only).
inline void effectiveCrossovers(int nb, const float x[3], double sr, double out[3])
{
    nb = clampv(nb, 1, kMaxBands);
    const int nx = nb - 1;
    const double hi = std::min(kMaxXover, 0.45 * sr);
    for (int k = 0; k < 3; ++k) out[k] = clampv((double)x[k], kMinXover, hi);
    for (int k = 1; k < nx; ++k) out[k] = std::max(out[k], out[k - 1] * kMinRatio);
    for (int k = nx - 1; k >= 0; --k) out[k] = std::min(out[k], hi / std::pow(kMinRatio, (double)(nx - 1 - k)));
    for (int k = 1; k < nx; ++k) out[k] = std::max(out[k], out[k - 1] * kMinRatio);
    for (int k = std::max(nx, 1); k < 3; ++k) out[k] = std::min(hi, out[k - 1] * 2.0);
}

// LR4 magnitudes (not squared), exactly as the digital (bilinear, prewarped) filters respond:
// |LP| = 1 / (1 + r^4) with r = tan(pi f / sr) / tan(pi fc / sr), and |HP| = 1 - |LP|.
// The linear-phase kernels use the same magnitudes, so both crossover types have identical band shapes.
inline double lr4Lo(double f, double fc, double sr)
{
    if (f >= 0.4999 * sr) return 0.0;
    const double r = std::tan(kPi * f / sr) / std::tan(kPi * std::min(fc, 0.49 * sr) / sr), r2 = r * r;
    return 1.0 / (1.0 + r2 * r2);
}
inline double lr4Hi(double f, double fc, double sr) { return 1.0 - lr4Lo(f, fc, sr); }

// Magnitude of band k (0..nb-1) at f for effective crossovers e: 2 bands split at e[0]; 3 bands have the root at
// e[1] and the low split at e[0]; 4 bands add the high split at e[2]. These are the bands' exact magnitudes in
// both modes; for any f they sum to 1.
inline double bandMag(int nb, const double e[3], int k, double f, double sr)
{
    switch (nb)
    {
    case 1: return k == 0 ? 1.0 : 0.0;
    case 2: return k == 0 ? lr4Lo(f, e[0], sr) : (k == 1 ? lr4Hi(f, e[0], sr) : 0.0);
    case 3:
        if (k == 0) return lr4Lo(f, e[1], sr) * lr4Lo(f, e[0], sr);
        if (k == 1) return lr4Lo(f, e[1], sr) * lr4Hi(f, e[0], sr);
        return k == 2 ? lr4Hi(f, e[1], sr) : 0.0;
    default:
        if (k == 0) return lr4Lo(f, e[1], sr) * lr4Lo(f, e[0], sr);
        if (k == 1) return lr4Lo(f, e[1], sr) * lr4Hi(f, e[0], sr);
        if (k == 2) return lr4Hi(f, e[1], sr) * lr4Lo(f, e[2], sr);
        return lr4Hi(f, e[1], sr) * lr4Hi(f, e[2], sr);
    }
}

// Which band a frequency belongs to (mouse hit tests).
inline int bandAt(int nb, const double e[3], double f)
{
    int best = 0;
    for (int k = 1; k < nb; ++k) if (f >= e[k - 1]) best = k;
    return best;
}

class Splitter {
public:
    enum Mode { kAnalog = 0, kLinear = 1 };
    static constexpr int kBlock = 512;   // linear-phase partition size (adds this much latency)

    Splitter() { for (auto& f : reqF) f.store(1000.0f); }
    ~Splitter() { stopWorker(); }
    Splitter(const Splitter&) = delete;
    Splitter& operator=(const Splitter&) = delete;

    // Allocates; not on the audio thread. Keeps the current mode, band count and frequencies.
    void prepare(double sampleRate, int maxBlock)
    {
        stopWorker();
        sr = sampleRate;
        maxBlk = std::max(1, maxBlock);
        for (auto& b : buf) for (auto& c : b) c.assign((size_t)maxBlk, 0.0f);
        park = 0.49 * sr;
        // linear phase: the kernels span ~170 ms at any rate
        N = sr <= 50000.0 ? 8192 : (sr <= 100000.0 ? 16384 : 32768);
        T = N - 1; D = N / 2 - 1; F = 2 * kBlock;
        P = (T + kBlock - 1) / kBlock;
        fft.setup(F);
        wFftN.setup(N); wFftF.setup(F);
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < kMaxBands - 1; ++k)
            {
                spec[s][k].clear();
                for (int p = 0; p < P; ++p) spec[s][k].emplace_back((size_t)F);
            }
        for (int ch = 0; ch < 2; ++ch)
        {
            fdl[ch].clear();
            for (int p = 0; p < P; ++p) fdl[ch].emplace_back((size_t)F);
            inBuf[ch].resize((size_t)F);
            dly[ch].setMaxDelay(D + kBlock + 4);
            dly[ch].setDelay(D + kBlock);
        }
        for (auto& set : convOut) for (auto& k : set) for (auto& c : k) c.assign((size_t)kBlock, 0.0f);
        acc.resize((size_t)F); tmp.resize((size_t)F);
        wSpec.resize((size_t)N); wTmp.resize((size_t)N); wPart.resize((size_t)F);
        wKernel.assign((size_t)T, 0.0f);
        win.assign((size_t)T, 0.0f);
        for (int t = 0; t < T; ++t)
        {
            // Tukey: flat over the middle 70%, raised-cosine tapers at the ends
            const double u = (t + 1.0) / (T + 1.0), a = 0.30;
            double w = 1.0;
            if (u < 0.5 * a) w = 0.5 * (1.0 - std::cos(kTwoPi * u / a));
            else if (u > 1.0 - 0.5 * a) w = 0.5 * (1.0 - std::cos(kTwoPi * (1.0 - u) / a));
            win[(size_t)t] = (float)w;
        }
        wRamp = std::max(1, (int)(0.02 * sr));
        effectiveCrossovers(nb, raw, sr, eff);
        // the first kernel set, synchronously
        design(0, nb, eff);
        active.store(0); swapPending.store(false);
        reqNb.store(nb); for (int k = 0; k < 3; ++k) reqF[k].store((float)eff[k]);
        reqSeq.store(0); builtSeq = 0;
        resetStates();
        startWorker();
    }

    // ---- settings (audio thread, block start; no allocation) ----
    void setMode(int m)
    {
        m = m ? kLinear : kAnalog;
        if (m == mode) return;
        mode = m;
        resetStates();
    }
    int getMode() const { return mode; }
    void setBands(int n)
    {
        n = clampv(n, 1, kMaxBands);
        if (n == nb) return;
        nb = n;
        recompute();
    }
    int bands() const { return nb; }
    // Bands that may carry signal right now (more than bands() while a band-count change fades).
    int bandsInUse() const
    {
        if (mode == kLinear) return std::max(nb, std::max(blkNb[0], blkFade ? blkNb[1] : 1));
        return std::max(nb, wSteps > 0 ? wFromNb : 1);
    }
    // Crossover parameter values (Hz, any order); the ones used follow effectiveCrossovers().
    void setCrossovers(float x1, float x2, float x3)
    {
        if (x1 == raw[0] && x2 == raw[1] && x3 == raw[2]) return;
        raw[0] = x1; raw[1] = x2; raw[2] = x3;
        recompute();
    }
    // The effective crossovers (what the analog splits glide toward and the linear kernels were asked for).
    void effective(double out[3]) const { for (int k = 0; k < 3; ++k) out[k] = eff[k]; }

    int latency() const { return mode == kLinear ? D + kBlock : 0; }
    int linearLatency() const { return D + kBlock; }
    void reset() { resetStates(); }

    // Split n (<= maxBlock) samples into band(b, ch). Bands >= bandsInUse() come out as silence.
    void process(const float* L, const float* R, int n)
    {
       #if PUMP_SYNC_KERNELS
        designRequested();   // what the worker thread would have picked up by now
       #endif
        if (mode == kLinear) processLinear(L, R, n);
        else processAnalog(L, R, n);
    }
    float* band(int b, int ch) { return buf[b][ch].data(); }
    const float* band(int b, int ch) const { return buf[b][ch].data(); }

    // For tests: the newest kernel request has been built and swapped in.
    bool kernelsCurrent() const { return builtSeq.load() == reqSeq.load() && !swapPending.load() && !blkFade; }
    // For tests: analog split points have reached their targets and the routing isn't ramping.
    bool analogSettled() const
    {
        for (int k = 0; k < 3; ++k) if (lcur[k] != ltgt[k]) return false;
        return wSteps == 0;
    }

private:
    void recompute()
    {
        effectiveCrossovers(nb, raw, sr, eff);
        // analog: target split points and leaf routing
        // tree split points: 2 bands split at the root; 3 bands add the low split; 4 bands the high split.
        // Unused split points are parked just below Nyquist (their leaves then carry nothing audible).
        double t[3] = { park, park, park };
        if (nb == 2) t[1] = eff[0];
        if (nb >= 3) { t[0] = eff[0]; t[1] = eff[1]; }
        if (nb >= 4) t[2] = eff[2];
        for (int k = 0; k < 3; ++k) ltgt[k] = std::log(t[k]);
        float W[4][4] = {};
        routing(nb, W);
        bool same = true;
        for (int l = 0; l < 4; ++l) for (int b = 0; b < 4; ++b) same = same && W[l][b] == wTgt[l][b];
        if (!same)
        {
            wFromNb = std::max(curRouteNb, wSteps > 0 ? wFromNb : 1);
            for (int l = 0; l < 4; ++l) for (int b = 0; b < 4; ++b)
            {
                wTgt[l][b] = W[l][b];
                wInc[l][b] = (W[l][b] - wCur[l][b]) / (float)wRamp;
            }
            wSteps = wRamp;
            curRouteNb = nb;
        }
        // linear: ask the worker for matching kernels, only while linear phase is in use (setMode() switching to
        // Linear comes through here too, via resetStates(), and asks for the current settings then)
        if (mode == kLinear) requestKernels();
    }
    void requestKernels()
    {
        bool changed = reqNb.load(std::memory_order_relaxed) != nb;
        for (int k = 0; k < 3; ++k) changed = changed || reqF[k].load(std::memory_order_relaxed) != (float)eff[k];
        if (changed)
        {
            reqNb.store(nb, std::memory_order_relaxed);
            for (int k = 0; k < 3; ++k) reqF[k].store((float)eff[k], std::memory_order_relaxed);
            reqSeq.fetch_add(1, std::memory_order_release);
        }
    }
    // leaves: 0 = below split 1, 1 = split 1..root, 2 = root..split 3, 3 = above split 3
    static void routing(int nb, float W[4][4])
    {
        for (int l = 0; l < 4; ++l) for (int b = 0; b < 4; ++b) W[l][b] = 0.0f;
        static const int map[4][4] = { { 0, 0, 0, 0 }, { 0, 0, 1, 1 }, { 0, 1, 2, 2 }, { 0, 1, 2, 3 } };
        for (int l = 0; l < 4; ++l) W[l][map[nb - 1][l]] = 1.0f;
    }

    void setTree(const double* f)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            sLo[ch].set(f[0], sr); sRoot[ch].set(f[1], sr); sHi[ch].set(f[2], sr);
            apLo[ch].set(f[0], sr); apHi[ch].set(f[2], sr);
        }
    }

    void clearTree()
    {
        for (int ch = 0; ch < 2; ++ch) { sLo[ch].reset(); sRoot[ch].reset(); sHi[ch].reset(); apLo[ch].reset(); apHi[ch].reset(); }
    }
    // false if a NaN / Inf got into the analog tree's (recursive) states
    bool treeFinite() const
    {
        double s = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (const LR4Split* p : { &sLo[ch], &sRoot[ch], &sHi[ch] })
                s += p->lp1.ic1 + p->lp1.ic2 + p->lp2.ic1 + p->lp2.ic2 + p->hp1.ic1 + p->hp1.ic2 + p->hp2.ic1 + p->hp2.ic2;
            s += apLo[ch].f.ic1 + apLo[ch].f.ic2 + apHi[ch].f.ic1 + apHi[ch].f.ic2;
        }
        return std::isfinite(s);
    }

    void resetStates()
    {
        clearTree();
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& b : fdl[ch]) b.clear();
            inBuf[ch].clear();
            dly[ch].clear();
        }
        for (auto& set : convOut) for (auto& k : set) for (auto& c : k) std::fill(c.begin(), c.end(), 0.0f);
        fill = 0; fdlPos = 0; blkFade = false;
        blkNb[0] = blkNb[1] = slotNb[active.load(std::memory_order_relaxed)];
        // analog: jump straight to the targets
        recompute();
        double t[3];
        for (int k = 0; k < 3; ++k) { lcur[k] = ltgt[k]; t[k] = std::exp(ltgt[k]); }
        setTree(t);
        for (int l = 0; l < 4; ++l) for (int b = 0; b < 4; ++b) wCur[l][b] = wTgt[l][b];
        wSteps = 0;
        curRouteNb = nb;
    }

    void processAnalog(const float* L, const float* R, int n)
    {
        // a kernel set built meanwhile is simply taken (nothing to crossfade while the FIRs aren't running)
        if (swapPending.load(std::memory_order_acquire))
        {
            active.store(pendingSlot.load(std::memory_order_relaxed), std::memory_order_relaxed);
            blkNb[0] = blkNb[1] = slotNb[active.load(std::memory_order_relaxed)];
            swapPending.store(false, std::memory_order_release);
        }
        // backstop (the plugins sanitize their input): one non-finite sample would stay in the tree for good
        if (!treeFinite()) clearTree();
        constexpr int kChunk = 32;
        const double a = 1.0 - std::exp(-kChunk / (0.006 * sr));   // split points glide (time constant 6 ms)
        const float* in[2] = { L, R };
        for (int s0 = 0; s0 < n; s0 += kChunk)
        {
            bool moved = false;
            double t[3];
            for (int k = 0; k < 3; ++k)
            {
                if (lcur[k] != ltgt[k])
                {
                    lcur[k] += (ltgt[k] - lcur[k]) * a;
                    if (std::fabs(ltgt[k] - lcur[k]) < 1e-4) lcur[k] = ltgt[k];
                    moved = true;
                }
                t[k] = std::exp(lcur[k]);
            }
            if (moved) setTree(t);
            const int s1 = std::min(n, s0 + kChunk);
            for (int i = s0; i < s1; ++i)
            {
                const bool ramp = wSteps > 0;
                if (ramp)
                {
                    for (int l = 0; l < 4; ++l) for (int b = 0; b < 4; ++b) wCur[l][b] += wInc[l][b];
                    if (--wSteps == 0) for (int l = 0; l < 4; ++l) for (int b = 0; b < 4; ++b) wCur[l][b] = wTgt[l][b];
                }
                for (int ch = 0; ch < 2; ++ch)
                {
                    double lo, hi, leaf[4];
                    sRoot[ch].process(in[ch][i], lo, hi);
                    sLo[ch].process(apHi[ch].process(lo), leaf[0], leaf[1]);
                    sHi[ch].process(apLo[ch].process(hi), leaf[2], leaf[3]);
                    if (!ramp && wSteps == 0)
                    {
                        float o[4] = { 0, 0, 0, 0 };
                        switch (nb)
                        {
                        case 1: o[0] = (float)(leaf[0] + leaf[1] + leaf[2] + leaf[3]); break;
                        case 2: o[0] = (float)(leaf[0] + leaf[1]); o[1] = (float)(leaf[2] + leaf[3]); break;
                        case 3: o[0] = (float)leaf[0]; o[1] = (float)leaf[1]; o[2] = (float)(leaf[2] + leaf[3]); break;
                        default: o[0] = (float)leaf[0]; o[1] = (float)leaf[1]; o[2] = (float)leaf[2]; o[3] = (float)leaf[3]; break;
                        }
                        for (int b = 0; b < 4; ++b) buf[b][ch][(size_t)i] = o[b];
                    }
                    else
                    {
                        for (int b = 0; b < 4; ++b)
                        {
                            double v = 0.0;
                            for (int l = 0; l < 4; ++l) v += wCur[l][b] * leaf[l];
                            buf[b][ch][(size_t)i] = (float)v;
                        }
                    }
                }
            }
        }
    }

    void processLinear(const float* L, const float* R, int n)
    {
        const float* in[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            const float t = blkFade ? (fill + 0.5f) / (float)kBlock : 1.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = in[ch][i];
                inBuf[ch][(size_t)(kBlock + fill)] = x;
                const float d = dly[ch].process(x);
                float outN[kMaxBands] = { 0, 0, 0, 0 }, outO[kMaxBands] = { 0, 0, 0, 0 };
                splitOne(0, ch, d, outN);
                if (blkFade)
                {
                    splitOne(1, ch, d, outO);
                    for (int b = 0; b < kMaxBands; ++b) outN[b] = outO[b] + (outN[b] - outO[b]) * t;
                }
                for (int b = 0; b < kMaxBands; ++b) buf[b][ch][(size_t)i] = outN[b];
            }
            if (++fill == kBlock) { runBlock(); fill = 0; }
        }
    }
    // band outputs of kernel set `set` (0 = current, 1 = the one fading out) for this sample
    inline void splitOne(int set, int ch, float delayed, float* out) const
    {
        const int n = blkNb[set];
        float sum = 0.0f;
        for (int k = 0; k < n - 1; ++k) { const float v = convOut[set][k][ch][(size_t)fill]; out[k] = v; sum += v; }
        out[n - 1] = delayed - sum;
    }

    void runBlock()
    {
        fdlPos = (fdlPos + 1) % P;
        for (int ch = 0; ch < 2; ++ch) fft.forwardU(inBuf[ch].data(), fdl[ch][(size_t)fdlPos].data());
        const int prev = active.load(std::memory_order_relaxed);
        // the slot that faded out during the last block is free again: let the worker design into it
        if (blkFade) swapPending.store(false, std::memory_order_release);
        blkFade = false;
        int cur = prev;
        if (swapPending.load(std::memory_order_acquire))
        {
            cur = pendingSlot.load(std::memory_order_relaxed);
            active.store(cur, std::memory_order_relaxed);
            blkFade = true;
        }
        blkNb[0] = slotNb[cur];
        convolveSet(cur, 0);
        if (blkFade) { blkNb[1] = slotNb[prev]; convolveSet(prev, 1); }
        for (int ch = 0; ch < 2; ++ch) std::memcpy(inBuf[ch].data(), inBuf[ch].data() + kBlock, sizeof(float) * (size_t)kBlock);
    }
    void convolveSet(int slot, int set)
    {
        const float scale = 1.0f / (float)F;
        for (int k = 0; k < slotNb[slot] - 1; ++k)
            for (int ch = 0; ch < 2; ++ch)
            {
                acc.clear();
                for (int p = 0; p < P; ++p)
                    fft.convolveAccumulate(fdl[ch][(size_t)((fdlPos - p + P) % P)].data(), spec[slot][k][(size_t)p].data(), acc.data(), scale);
                fft.inverseU(acc.data(), tmp.data());
                std::memcpy(convOut[set][k][ch].data(), tmp.data() + kBlock, sizeof(float) * (size_t)kBlock);
            }
    }

    // ---- kernel design (worker thread, or prepare) ----
    void design(int slot, int bands, const double* e)
    {
        const int half = N / 2;
        for (int k = 0; k < bands - 1; ++k)
        {
            for (int j = 0; j <= half; ++j)
            {
                const double m = bandMag(bands, e, k, j * sr / N, sr);
                if (j == 0) wSpec[0] = (float)m;
                else if (j == half) wSpec[1] = (float)m;
                else { wSpec[(size_t)(2 * j)] = (float)m; wSpec[(size_t)(2 * j + 1)] = 0.0f; }
            }
            wFftN.inverse(wSpec.data(), wTmp.data());
            for (int t = 0; t < T; ++t) wKernel[(size_t)t] = wTmp[(size_t)((t - D + N) % N)] / (float)N * win[(size_t)t];
            for (int p = 0; p < P; ++p)
            {
                wPart.clear();
                const int s0 = p * kBlock, cnt = std::min(kBlock, T - s0);
                for (int i = 0; i < cnt; ++i) wPart[(size_t)i] = wKernel[(size_t)(s0 + i)];
                wFftF.forwardU(wPart.data(), spec[slot][k][(size_t)p].data());
            }
        }
        slotNb[slot] = bands;
    }
    // The worker's job: design the newest request into the spare slot (unless a swap is still pending).
    void designRequested()
    {
        const uint32_t seq = reqSeq.load(std::memory_order_acquire);
        if (seq == builtSeq.load(std::memory_order_relaxed) || swapPending.load(std::memory_order_acquire)) return;
        const int bands = reqNb.load(std::memory_order_relaxed);
        double e[3];
        for (int k = 0; k < 3; ++k) e[k] = reqF[k].load(std::memory_order_relaxed);
        const int slot = 1 - active.load(std::memory_order_relaxed);
        design(slot, bands, e);
        pendingSlot.store(slot, std::memory_order_relaxed);
        builtSeq.store(seq, std::memory_order_relaxed);
        swapPending.store(true, std::memory_order_release);
    }
#if PUMP_SYNC_KERNELS
    // no worker: process() calls designRequested() itself
    void startWorker() {}
    void stopWorker() {}
#else
    void startWorker()
    {
        quit.store(false);
        // Polls (the audio thread only sets an atomic, never wakes a thread): every 2 ms while settings are moving,
        // backing off to 50 ms once nothing has changed for a second, so an idle instance costs ~20 wake-ups a second,
        // not 500. The first change after a pause is picked up within 50 ms.
        worker = std::thread([this] {
            int idleMs = 0;
            while (!quit.load(std::memory_order_acquire))
            {
                const uint32_t seq = reqSeq.load(std::memory_order_acquire);
                if (seq == builtSeq.load(std::memory_order_relaxed) || swapPending.load(std::memory_order_acquire))
                {
                    const int wait = idleMs < 1000 ? 2 : 50;
                    std::this_thread::sleep_for(std::chrono::milliseconds(wait));
                    idleMs = std::min(idleMs + wait, 1000);
                    continue;
                }
                idleMs = 0;
                designRequested();
            }
        });
    }
    void stopWorker()
    {
        if (!worker.joinable()) return;
        quit.store(true, std::memory_order_release);
        worker.join();
    }
#endif

    double sr = 48000.0, park = 22000.0;
    int maxBlk = 512, mode = kAnalog, nb = 3;
    float raw[3] = { 120.0f, 2000.0f, 8000.0f };
    double eff[3] = { 120, 2000, 8000 };
    std::vector<float> buf[kMaxBands][2];

    // analog tree
    LR4Split sLo[2], sRoot[2], sHi[2];
    LR4Allpass apLo[2], apHi[2];
    double lcur[3] = { 0, 0, 0 }, ltgt[3] = { 0, 0, 0 };
    float wCur[4][4] = {}, wTgt[4][4] = {}, wInc[4][4] = {};
    int wSteps = 0, wRamp = 960, wFromNb = 1, curRouteNb = 3;

    // linear phase
    int N = 8192, T = 8191, D = 4095, F = 1024, P = 16;
    RealFFT fft, wFftN, wFftF;
    std::vector<AlignedBuffer> spec[2][kMaxBands - 1];   // [slot][band][partition]
    int slotNb[2] = { 3, 3 };
    std::vector<AlignedBuffer> fdl[2];
    AlignedBuffer inBuf[2], acc, tmp;
    std::vector<float> convOut[2][kMaxBands - 1][2];     // [current / fading-out set][band][ch]
    int blkNb[2] = { 3, 3 };
    bool blkFade = false;
    DelayLine dly[2];
    int fill = 0, fdlPos = 0;
    // worker-side buffers
    AlignedBuffer wSpec, wTmp, wPart;
    std::vector<float> wKernel, win;
#if !PUMP_SYNC_KERNELS
    std::thread worker;
#endif
    std::atomic<bool> quit { false }, swapPending { false };
    std::atomic<int> active { 0 }, pendingSlot { 0 }, reqNb { 3 };
    std::atomic<float> reqF[3];
    std::atomic<uint32_t> reqSeq { 0 }, builtSeq { 0 };
};

}} // namespace pump::mb
