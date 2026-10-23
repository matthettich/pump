// pump DSP: linear-phase oversampling (1x, 2x, 4x, 8x) with halfband FIR cascades, and a 4x true-peak meter.
// Stage 1 is steep (passband to 0.4535 fs, 98 dB stopband); stages 2 and 3 only clean up images of an
// already band-limited signal. Latency is an integer number of base-rate samples (padded where needed), so
// dry/wet mixes line up exactly.
#pragma once

#include "Util.hpp"
#include "FilterTables.hpp"

namespace pump {

// One 2x halfband stage, mono. Taps have the halfband structure (every other tap zero except the centre).
class HalfbandStage {
public:
    void setup(const double* taps, int n)
    {
        // nonzero "even" taps (h[0], h[2], ...): the convolution phase; the centre (odd index) is the pure-delay phase
        nTaps = n;
        centre = (n - 1) / 2;                       // odd
        evenTaps.clear();
        for (int i = 0; i < n; i += 2) evenTaps.push_back((float)taps[i]);
        nEven = (int)evenTaps.size();
        // up: history of input samples
        histLen = nEven;
        upHist.assign((size_t)(2 * histLen), 0.0f);
        upPos = 0;
        // down: history of the "even" phase inputs and a delay for the centre phase
        dnHist.assign((size_t)(2 * histLen), 0.0f);
        dnPos = 0;
        dnDelay.assign((size_t)(centre / 2 + 2), 0.0f);
        dnDelayPos = 0;
        upDelay.assign((size_t)(centre / 2 + 2), 0.0f);
        upDelayPos = 0;
    }
    void reset()
    {
        std::fill(upHist.begin(), upHist.end(), 0.0f);
        std::fill(dnHist.begin(), dnHist.end(), 0.0f);
        std::fill(dnDelay.begin(), dnDelay.end(), 0.0f);
        std::fill(upDelay.begin(), upDelay.end(), 0.0f);
        upPos = dnPos = dnDelayPos = upDelayPos = 0;
    }
    // Group delay in output-rate samples (up) / input-rate samples (down).
    int delay() const { return centre; }

    // Upsample: one input sample -> two outputs (y0 then y1).
    inline void up(float x, float& y0, float& y1)
    {
        // y[2m]   = 2 * sum_j h[2j]   x[m - j]          (convolution phase)
        // y[2m+1] = 2 * h[centre] x[m - (centre-1)/2]   (pure delay, h[centre] = 0.5)
        upHist[(size_t)upPos] = upHist[(size_t)(upPos + histLen)] = x;
        const float* h = upHist.data() + upPos;   // h[0] = newest ... we stored newest at upPos; walk backwards via layout below
        float acc = 0.0f;
        // history layout: index upPos is newest, upPos+1 is one older, ... (we decrement upPos each sample)
        for (int j = 0; j < nEven; ++j) acc += evenTaps[(size_t)j] * h[j];
        y0 = 2.0f * acc;
        // the delay phase: x delayed by (centre-1)/2 input samples
        const int d = (centre - 1) / 2;
        const int dl = (int)upDelay.size();
        upDelay[(size_t)upDelayPos] = x;
        y1 = upDelay[(size_t)((upDelayPos - d + dl) % dl)];
        upDelayPos = (upDelayPos + 1) % dl;
        upPos = (upPos - 1 + histLen) % histLen;
    }

    // Downsample: two inputs (x0 = even, x1 = odd) -> one output.
    inline float down(float x0, float x1)
    {
        // z[j] = sum_i h[i] u[2j - i], decimating on the even phase so the delay is exactly `centre` input
        // samples: the even-index taps run over the even inputs; the centre tap (odd index) meets the odd input
        // of pair j - (centre+1)/2.
        dnHist[(size_t)dnPos] = dnHist[(size_t)(dnPos + histLen)] = x0;
        const float* h = dnHist.data() + dnPos;
        float acc = 0.0f;
        for (int j = 0; j < nEven; ++j) acc += evenTaps[(size_t)j] * h[j];
        const int dl = (int)dnDelay.size();
        const int d = (centre + 1) / 2;   // pairs back; the current x1 is stored after reading (it is "pair j")
        const float c = dnDelay[(size_t)((dnDelayPos - d + dl) % dl)];
        dnDelay[(size_t)dnDelayPos] = x1;
        dnDelayPos = (dnDelayPos + 1) % dl;
        dnPos = (dnPos - 1 + histLen) % histLen;
        return acc + 0.5f * c;
    }

private:
    std::vector<float> evenTaps, upHist, dnHist, dnDelay, upDelay;
    int nTaps = 0, centre = 0, nEven = 0, histLen = 1, upPos = 0, dnPos = 0, dnDelayPos = 0, upDelayPos = 0;
};

// Stereo oversampler. Call prepare(), then for each block: upsample(), process the oversampled buffers,
// downsample(). factor = 1 passes through with zero latency.
class Oversampler {
public:
    static constexpr int kMaxFactorLog2 = 3;

    void prepare(int factorLog2, int maxBlock)
    {
        stages = clampv(factorLog2, 0, kMaxFactorLog2);
        factor = 1 << stages;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int s = 0; s < kMaxFactorLog2; ++s)
            {
                const double* t = s == 0 ? tables::kHB1 : (s == 1 ? tables::kHB2 : tables::kHB3);
                const int n = s == 0 ? (int)(sizeof(tables::kHB1) / sizeof(double))
                            : (s == 1 ? (int)(sizeof(tables::kHB2) / sizeof(double)) : (int)(sizeof(tables::kHB3) / sizeof(double)));
                st[ch][s].setup(t, n);
            }
            buf[ch].assign((size_t)(maxBlock * 8 + 8), 0.0f);
            tmp[ch].assign((size_t)(maxBlock * 8 + 8), 0.0f);
        }
        // total delay in base samples = sum over stages of (taps-1) / 2^s; pad to an integer at the top rate
        double d = 0.0;
        for (int s = 0; s < stages; ++s)
        {
            const int n = s == 0 ? 127 : (s == 1 ? 23 : 11);
            d += (double)(n - 1) / (double)(1 << (s + 1)) * 2.0 / 2.0;   // up + down: (n-1)/2 each at rate 2^(s+1)
        }
        latencyBase = (int)std::ceil(d - 1e-9);
        padTop = (int)std::lround((latencyBase - d) * factor);
        for (int ch = 0; ch < 2; ++ch) { pad[ch].setMaxDelay(16); pad[ch].setDelay(padTop); }
    }
    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& s : st[ch]) s.reset();
            pad[ch].clear();
        }
    }
    int getFactor() const { return factor; }
    int latency() const { return stages == 0 ? 0 : latencyBase; }

    // Returns pointers to the oversampled buffers (n * factor samples each).
    float* const* upsample(const float* L, const float* R, int n)
    {
        const float* in[2] = { L, R };
        for (int ch = 0; ch < 2; ++ch)
        {
            if (stages == 0) { std::memcpy(buf[ch].data(), in[ch], sizeof(float) * (size_t)n); continue; }
            std::memcpy(tmp[ch].data(), in[ch], sizeof(float) * (size_t)n);
            int len = n;
            for (int s = 0; s < stages; ++s)
            {
                float* src = (s & 1) ? buf[ch].data() : tmp[ch].data();
                float* dst = (s & 1) ? tmp[ch].data() : buf[ch].data();
                for (int i = 0; i < len; ++i) st[ch][s].up(src[i], dst[2 * i], dst[2 * i + 1]);
                len *= 2;
            }
            if ((stages & 1) == 0) std::memcpy(buf[ch].data(), tmp[ch].data(), sizeof(float) * (size_t)len);
        }
        ptr[0] = buf[0].data(); ptr[1] = buf[1].data();
        return ptr;
    }

    // Takes the processed oversampled buffers (as returned by upsample) back to the base rate.
    void downsample(float* L, float* R, int n)
    {
        float* out[2] = { L, R };
        for (int ch = 0; ch < 2; ++ch)
        {
            if (stages == 0) { std::memcpy(out[ch], buf[ch].data(), sizeof(float) * (size_t)n); continue; }
            int len = n * factor;
            float* b = buf[ch].data();
            for (int i = 0; i < len; ++i) b[i] = pad[ch].process(b[i]);
            float* src = b;
            float* dst = tmp[ch].data();
            for (int s = stages - 1; s >= 0; --s)
            {
                len /= 2;
                for (int i = 0; i < len; ++i) dst[i] = st[ch][s].down(src[2 * i], src[2 * i + 1]);
                std::swap(src, dst);
            }
            std::memcpy(out[ch], src, sizeof(float) * (size_t)n);
        }
    }

private:
    HalfbandStage st[2][kMaxFactorLog2];
    std::vector<float> buf[2], tmp[2];
    DelayLine pad[2];
    float* ptr[2] = { nullptr, nullptr };
    int stages = 0, factor = 1, latencyBase = 0, padTop = 0;
};

// 4x polyphase interpolator for true-peak detection (ITU-R BS.1770 style, steeper filter).
// Feed samples; peak() returns the largest absolute value among the sample and its three interpolated
// neighbours, delayed by delay() samples relative to the input.
class TruePeakDetector {
public:
    TruePeakDetector() { reset(); }
    void reset() { std::fill(std::begin(hist), std::end(hist), 0.0f); pos = 0; }
    static constexpr int delay() { return tables::kTPPhaseLen / 2; }
    inline float process(float x)
    {
        constexpr int L = tables::kTPPhaseLen;
        hist[pos] = hist[pos + L] = x;
        const float* h = hist + pos;   // h[0] newest
        float m = 0.0f;
        const double* ph[4] = { tables::kTP0, tables::kTP1, tables::kTP2, tables::kTP3 };
        for (int p = 0; p < 4; ++p)
        {
            double acc = 0.0;
            const double* c = ph[p];
            for (int j = 0; j < L; ++j) acc += c[j] * h[j];
            m = std::max(m, (float)std::fabs(acc));
        }
        pos = (pos - 1 + L) % L;
        return m;
    }
private:
    float hist[2 * tables::kTPPhaseLen];
    int pos = 0;
};

} // namespace pump
