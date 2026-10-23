// pump DSP: loudness metering to ITU-R BS.1770-4 / EBU R128 (momentary, short-term, integrated, loudness
// range) plus true peak. Integrated loudness and LRA use histograms (0.1 LU bins) so they run forever in
// constant memory and time.
#pragma once

#include "Util.hpp"
#include "Biquad.hpp"
#include "Oversampler.hpp"

namespace pump {

class LoudnessMeter {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        // K-weighting coefficients for this rate (pre-filter shelf + RLB high-pass), from the BS.1770 analog
        // definitions as implemented in libebur128.
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPi * f0 / sr), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2.0 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0; shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan(kPi * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
            hp.a1 = 2.0 * (K * K - 1.0) / a0; hp.a2 = (1.0 - K / Q + K * K) / a0;
        }
        sub = std::max(1, (int)std::lround(sr * 0.1));   // 100 ms
        reset();
    }

    void reset()
    {
        for (auto& c : st) for (auto& s : c) s.reset();
        acc = 0.0; cnt = 0;
        std::fill(std::begin(ring), std::end(ring), 0.0);
        ringPos = 0; ringFill = 0;
        std::fill(std::begin(histI), std::end(histI), 0u);
        std::fill(std::begin(histIE), std::end(histIE), 0.0);
        std::fill(std::begin(histS), std::end(histS), 0u);
        momentary = shortTerm = -INFINITY;
        maxMomentary = maxShortTerm = -INFINITY;
        integrated = lra = -INFINITY;
        tpMax = 0.0f;
        for (auto& t : tp) t.reset();
        blocksI = 0;
        dirty = false;
    }

    // Feed a stereo block. Updates momentary/short-term every 100 ms.
    // Non-finite samples (a NaN or Inf from an upstream device) are measured as silence, so one bad sample can't
    // leave the filters, the gating histogram or the readouts stuck at NaN.
    void process(const float* L, const float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float xl = std::isfinite(L[i]) ? L[i] : 0.0f, xr = std::isfinite(R[i]) ? R[i] : 0.0f;
            const double l = st[0][1].process(hp, st[0][0].process(shelf, xl));
            const double r = st[1][1].process(hp, st[1][0].process(shelf, xr));
            acc += l * l + r * r;
            tpMax = std::max(tpMax, std::max(tp[0].process(xl), tp[1].process(xr)));
            if (++cnt >= sub)
            {
                pushSubblock(acc / cnt);
                acc = 0.0; cnt = 0;
            }
        }
    }

    // Accessors (LUFS / LU / dBTP). Integrated and LRA are recomputed lazily.
    double getMomentary() const { return momentary; }
    double getShortTerm() const { return shortTerm; }
    double getMaxMomentary() const { return maxMomentary; }
    double getMaxShortTerm() const { return maxShortTerm; }
    double getIntegrated() { refresh(); return integrated; }
    double getRange() { refresh(); return lra; }
    double getTruePeakDb() const { return gainToDb(tpMax); }
    float getTruePeak() const { return tpMax; }

private:
    static constexpr int kBins = 1000;             // -70 .. +30 LUFS in 0.1 LU steps
    static double binLoud(int b) { return -70.0 + (b + 0.5) * 0.1; }
    static int loudBin(double l) { return clampv((int)std::floor((l + 70.0) * 10.0), 0, kBins - 1); }
    static double toLufs(double ms) { return ms > 0.0 ? -0.691 + 10.0 * std::log10(ms) : -INFINITY; }

    void pushSubblock(double ms)
    {
        ring[ringPos] = ms;
        ringPos = (ringPos + 1) % 30;
        ringFill = std::min(ringFill + 1, 30);
        // momentary: last 4 sub-blocks (400 ms); short-term: last 30 (3 s)
        double m = 0.0, s = 0.0;
        const int nm = std::min(ringFill, 4), ns = ringFill;
        for (int k = 0; k < ns; ++k)
        {
            const double v = ring[(ringPos - 1 - k + 30) % 30];
            if (k < nm) m += v;
            s += v;
        }
        const double mMs = m / 4.0;   // partial windows count as silence at the start (as in the standard)
        momentary = toLufs(mMs);
        if (ringFill >= 4)
        {
            maxMomentary = std::max(maxMomentary, momentary);
            // gating block (400 ms, 75% overlap): one every 100 ms
            if (momentary > -70.0)
            {
                const int b = loudBin(momentary);
                histI[b]++;
                histIE[b] += mMs;
                blocksI++;
                dirty = true;
            }
        }
        shortTerm = toLufs(s / 30.0);
        if (ringFill >= 30)
        {
            maxShortTerm = std::max(maxShortTerm, shortTerm);
            if (shortTerm > -70.0) { histS[loudBin(shortTerm)]++; dirty = true; }
        }
    }

    void refresh()
    {
        if (!dirty) return;
        dirty = false;
        // integrated: absolute gate -70 (already applied), relative gate -10 LU below the gated mean
        double sumE = 0.0; uint64_t n = 0;
        for (int b = 0; b < kBins; ++b) { sumE += histIE[b]; n += histI[b]; }
        if (n == 0) integrated = -INFINITY;
        else
        {
            const double gate = toLufs(sumE / (double)n) - 10.0;
            double e = 0.0; uint64_t m = 0;
            for (int b = 0; b < kBins; ++b)
                if (binLoud(b) >= gate) { e += histIE[b]; m += histI[b]; }
            integrated = m ? toLufs(e / (double)m) : -INFINITY;
        }
        // LRA (EBU Tech 3342): short-term values, -70 absolute gate, -20 LU relative gate, 10th..95th percentile
        double sE = 0.0; uint64_t sn = 0;
        for (int b = 0; b < kBins; ++b) { sE += histS[b] * std::pow(10.0, (binLoud(b) + 0.691) / 10.0); sn += histS[b]; }
        if (sn == 0) { lra = -INFINITY; return; }
        const double gate = toLufs(sE / (double)sn) - 20.0;
        uint64_t total = 0;
        for (int b = 0; b < kBins; ++b) if (binLoud(b) >= gate) total += histS[b];
        if (total == 0) { lra = 0.0; return; }
        const uint64_t lo = (uint64_t)std::llround(0.10 * (double)(total - 1)), hi = (uint64_t)std::llround(0.95 * (double)(total - 1));
        uint64_t run = 0; double vLo = 0.0, vHi = 0.0; bool gotLo = false;
        for (int b = 0; b < kBins; ++b)
        {
            if (binLoud(b) < gate) continue;
            const uint64_t next = run + histS[b];
            if (!gotLo && lo < next) { vLo = binLoud(b); gotLo = true; }
            if (hi < next) { vHi = binLoud(b); break; }
            run = next;
        }
        lra = std::max(0.0, vHi - vLo);
    }

    double sr = 48000.0;
    BiquadCoeffs shelf, hp;
    BiquadState st[2][2];
    double acc = 0.0;
    int cnt = 0, sub = 4800;
    double ring[30] = {};
    int ringPos = 0, ringFill = 0;
    uint32_t histI[kBins] = {};
    double histIE[kBins] = {};
    uint32_t histS[kBins] = {};
    uint64_t blocksI = 0;
    bool dirty = false;
    double momentary = -INFINITY, shortTerm = -INFINITY, maxMomentary = -INFINITY, maxShortTerm = -INFINITY;
    double integrated = -INFINITY, lra = -INFINITY;
    TruePeakDetector tp[2];
    float tpMax = 0.0f;
};

} // namespace pump
