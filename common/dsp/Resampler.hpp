// pump DSP: sample-rate conversion. Header-only, framework-free.
//
//   StreamResampler   streaming conversion between two rates with a Kaiser-windowed sinc (push input, pull output);
//                     the output is time-aligned with the input (output sample m is the input at time m x fin/fout),
//                     it just becomes available `delayIn()` input samples later
//   RateBridge        runs a block of processing at another rate: host -> inner rate -> process -> host, with a
//                     fixed, known latency (AMP runs neural amp models at the rate they were trained at)
//   resampleOffline   converts a whole buffer (impulse responses loaded from files)
#pragma once

#include "Util.hpp"

namespace pump {

// zeroth-order modified Bessel function (Kaiser window)
inline double besselI0(double x)
{
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; if (t < 1e-12 * s) break; }
    return s;
}

class StreamResampler {
public:
    // fin -> fout; maxIn = most input samples pushed between pulls
    void prepare(double fin, double fout, int maxIn, int halfTaps = 24)
    {
        ratio = fin / fout;                              // input samples per output sample
        const double down = std::max(1.0, ratio);        // converting down: widen the kernel, lower the cutoff
        H = (int)std::ceil(halfTaps * down);
        fc = 0.5 * std::min(1.0, 1.0 / ratio) * 0.90;    // cycles per input sample (10 % guard band)
        constexpr double beta = 8.0;
        const double i0b = besselI0(beta);
        table.assign((size_t)(2 * H * kPhases + 1), 0.0f);
        for (int i = 0; i <= 2 * H * kPhases; ++i)
        {
            const double x = (double)i / kPhases - H;    // -H .. H input samples
            const double r = x / H;
            const double w = std::fabs(r) <= 1.0 ? besselI0(beta * std::sqrt(1.0 - r * r)) / i0b : 0.0;
            const double s = std::fabs(x) < 1e-12 ? 1.0 : std::sin(kTwoPi * fc * x) / (kTwoPi * fc * x);
            table[(size_t)i] = (float)(2.0 * fc * s * w);
        }
        int size = 1;
        while (size < 4 * H + maxIn + 16) size <<= 1;
        ring.assign((size_t)size, 0.0f);
        mask = size - 1;
        reset();
    }
    void reset() { std::fill(ring.begin(), ring.end(), 0.0f); nIn = 0; t = 0.0; }
    double delayIn() const { return H; }                  // input samples between an input time and its output
    double ratioInPerOut() const { return ratio; }

    inline void push(float x) { ring[(size_t)(nIn & mask)] = x; ++nIn; }
    inline bool canPull() const { return t + H + 1 < (double)nIn; }
    inline float pull()
    {
        const long long c = (long long)std::floor(t);
        const double frac = t - (double)c;
        double y = 0.0;
        for (long long k = c - H + 1; k <= c + H; ++k)
        {
            if (k < 0) continue;
            const double x = (t - (double)k + H) * kPhases;      // table position of (t - k)
            const int i = (int)x;
            if (i < 0 || i >= 2 * H * kPhases) continue;
            const double f = x - i;
            const double kern = table[(size_t)i] + (table[(size_t)i + 1] - table[(size_t)i]) * f;
            y += kern * ring[(size_t)(k & mask)];
        }
        (void)frac;
        t += ratio;
        return (float)y;
    }

private:
    static constexpr int kPhases = 256;
    std::vector<float> table, ring;
    double ratio = 1.0, fc = 0.45, t = 0.0;
    int H = 24, mask = 0;
    long long nIn = 0;
};

// host rate -> inner rate -> process(in, out, n) -> host rate, mono, with a fixed latency (latency()). When the rates
// match it passes straight through (latency 0).
class RateBridge {
public:
    void prepare(double hostRate, double innerRate, int maxBlock)
    {
        same = std::fabs(hostRate - innerRate) < 0.5;
        maxB = maxBlock;
        if (same) { lat = 0; inner.assign((size_t)maxBlock, 0.0f); innerOut.assign((size_t)maxBlock, 0.0f); return; }
        a.prepare(hostRate, innerRate, maxBlock);
        const int innerMax = (int)std::ceil(maxBlock * innerRate / hostRate) + 4;
        b.prepare(innerRate, hostRate, innerMax);
        // output p is computable once the host input count passes p + D: D = B's delay (inner samples) in host
        // samples + A's delay
        const double D = b.delayIn() * (hostRate / innerRate) + a.delayIn();
        lat = (int)std::ceil(D) + 2;
        inner.assign((size_t)innerMax, 0.0f);
        innerOut.assign((size_t)innerMax, 0.0f);
        int size = 1;
        while (size < 2 * (lat + maxBlock) + 64) size <<= 1;
        fifo.assign((size_t)size, 0.0f);
        fmask = size - 1;
        reset();
    }
    void reset()
    {
        if (same) return;
        a.reset(); b.reset();
        std::fill(fifo.begin(), fifo.end(), 0.0f);
        rd = 0; wr = lat;   // the latency, as silence up front
    }
    int latency() const { return lat; }
    int maxInner() const { return (int)inner.size(); }

    // in -> out (may alias), n <= maxBlock. fn(in, out, n) is any callable: taken as a template so a capturing lambda
    // is called directly (a std::function can allocate on every call, and this runs on the audio thread)
    template <typename F>
    void process(const float* in, float* out, int n, F&& fn)
    {
        if (same) { fn(in, out, n); return; }
        for (int i = 0; i < n; ++i) a.push(in[i]);
        int m = 0;
        while (a.canPull() && m < (int)inner.size()) inner[(size_t)m++] = a.pull();
        if (m > 0)
        {
            fn(inner.data(), innerOut.data(), m);
            for (int i = 0; i < m; ++i) b.push(innerOut[(size_t)i]);
        }
        while (b.canPull()) { fifo[(size_t)(wr & fmask)] = b.pull(); ++wr; }
        for (int i = 0; i < n; ++i) out[i] = rd < wr ? fifo[(size_t)(rd++ & fmask)] : 0.0f;
    }

private:
    StreamResampler a, b;
    std::vector<float> inner, innerOut, fifo;
    long long rd = 0, wr = 0;
    int lat = 0, fmask = 0, maxB = 0;
    bool same = true;
};

// a whole buffer from one rate to another (allocates; not for the audio thread)
inline std::vector<float> resampleOffline(const std::vector<float>& x, double fin, double fout)
{
    if (std::fabs(fin - fout) < 0.5 || x.empty()) return x;
    StreamResampler r;
    r.prepare(fin, fout, (int)x.size() + 64);
    std::vector<float> y;
    const size_t want = (size_t)std::ceil(x.size() * fout / fin);
    y.reserve(want);
    for (float v : x) r.push(v);
    for (int i = 0; i < 4 * (int)r.delayIn() + 8; ++i) r.push(0.0f);   // flush the tail
    while (r.canPull() && y.size() < want) y.push_back(r.pull());
    return y;
}

} // namespace pump
