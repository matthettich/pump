// pump DSP: dynamics building blocks — gain computers, envelope followers, sliding-window extremes,
// lookahead smoothing.
#pragma once

#include "Util.hpp"

namespace pump {

// Static compressor/expander curve, in dB: returns the gain change (<= 0 for compression) for a level.
// knee = full knee width in dB (quadratic soft knee). ratio >= 1 compresses above the threshold.
struct GainComputer {
    double threshold = -18.0, ratio = 2.0, knee = 6.0, range = -120.0;   // range: deepest allowed gain change

    inline double compress(double levelDb) const
    {
        const double slope = 1.0 - 1.0 / std::max(ratio, 1.0);
        const double over = levelDb - threshold;
        double g;
        if (knee > 0.0 && std::fabs(over) <= 0.5 * knee)
            g = -slope * (over + 0.5 * knee) * (over + 0.5 * knee) / (2.0 * knee);
        else
            g = over > 0.0 ? -slope * over : 0.0;
        return std::max(g, range);
    }
    // Downward expansion below the threshold (ratio > 1 = stronger), limited by range.
    inline double expand(double levelDb) const
    {
        const double slope = std::max(ratio, 1.0) - 1.0;
        const double under = threshold - levelDb;
        double g;
        if (knee > 0.0 && std::fabs(under) <= 0.5 * knee)
            g = -slope * (under + 0.5 * knee) * (under + 0.5 * knee) / (2.0 * knee);
        else
            g = under > 0.0 ? -slope * under : 0.0;
        return std::max(g, range);
    }
};

// Smooth branching attack/release follower (works on any signal: linear level or dB gain reduction).
class AttackRelease {
public:
    void setup(double attackMs, double releaseMs, double sr)
    {
        ca = tauCoef(attackMs * 0.001, sr);
        cr = tauCoef(releaseMs * 0.001, sr);
    }
    void setAttack(double ms, double sr) { ca = tauCoef(ms * 0.001, sr); }
    void setRelease(double ms, double sr) { cr = tauCoef(ms * 0.001, sr); }
    void reset(double v = 0.0) { y = v; }
    // "rising" means moving toward more action: for a level follower x > y; for a gain-reduction follower in dB
    // (negative = more reduction) pass the negated value or use processGR.
    inline double process(double x)
    {
        const double c = x > y ? ca : cr;
        y = x + (y - x) * c;
        return y;
    }
    // Gain reduction in dB (<= 0): attack when the reduction deepens.
    inline double processGR(double grDb)
    {
        const double c = grDb < y ? ca : cr;
        y = grDb + (y - grDb) * c;
        return y;
    }
    double value() const { return y; }
private:
    double ca = 0.0, cr = 0.0, y = 0.0;
};

// Sliding-window maximum over the last `len` samples (monotonic deque, O(1) amortised).
class SlidingMax {
public:
    void setup(int maxLen) { cap = maxLen + 2; vals.assign((size_t)cap, 0.0f); idx.assign((size_t)cap, 0); setLength(maxLen); reset(); }
    void setLength(int len) { L = clampv(len, 1, cap - 2); }
    void reset() { head = tail = 0; t = 0; }
    inline float process(float x)
    {
        while (tail > head && vals[(size_t)((tail - 1) % cap)] <= x) --tail;
        vals[(size_t)(tail % cap)] = x; idx[(size_t)(tail % cap)] = t; ++tail;
        while (idx[(size_t)(head % cap)] <= t - L) ++head;
        ++t;
        return vals[(size_t)(head % cap)];
    }
private:
    std::vector<float> vals;
    std::vector<int64_t> idx;
    int cap = 4, L = 1;
    int64_t head = 0, tail = 0, t = 0;
};

// Sliding-window minimum (same structure).
class SlidingMin {
public:
    void setup(int maxLen) { cap = maxLen + 2; vals.assign((size_t)cap, 1.0f); idx.assign((size_t)cap, 0); setLength(maxLen); reset(); }
    void setLength(int len) { L = clampv(len, 1, cap - 2); }
    void reset() { head = tail = 0; t = 0; }
    inline float process(float x)
    {
        while (tail > head && vals[(size_t)((tail - 1) % cap)] >= x) --tail;
        vals[(size_t)(tail % cap)] = x; idx[(size_t)(tail % cap)] = t; ++tail;
        while (idx[(size_t)(head % cap)] <= t - L) ++head;
        ++t;
        return vals[(size_t)(head % cap)];
    }
private:
    std::vector<float> vals;
    std::vector<int64_t> idx;
    int cap = 4, L = 1;
    int64_t head = 0, tail = 0, t = 0;
};

// Running mean over a window (double accumulator with periodic re-summing to avoid drift).
class MovingAverage {
public:
    void setup(int maxLen) { buf.assign((size_t)std::max(1, maxLen), 0.0); setLength(maxLen); }
    void setLength(int len)
    {
        len = clampv(len, 1, (int)buf.size());
        if (len == L) return;
        L = len; reset(fillValue);
    }
    void reset(double v)
    {
        fillValue = v;
        std::fill(buf.begin(), buf.begin() + L, v);
        sum = v * L; pos = 0; n = 0;
    }
    inline double process(double x)
    {
        sum += x - buf[(size_t)pos];
        buf[(size_t)pos] = x;
        if (++pos >= L) pos = 0;
        if (++n >= 65536) { n = 0; sum = 0.0; for (int i = 0; i < L; ++i) sum += buf[(size_t)i]; }
        return sum / L;
    }
private:
    std::vector<double> buf { 0.0 };
    double sum = 0.0, fillValue = 0.0;   // detectors start silent; LookaheadGain resets its boxes to 1
    int L = 1, pos = 0, n = 0;
};

// Lookahead gain smoother for brickwall limiting: the needed gain (<= 1) is held for the lookahead window
// (sliding minimum) and then smoothed by a raised-cosine-weighted average over the same window, so the gain has
// fully reached any peak's value by the time the (delayed) audio gets there, with no step.
class LookaheadGain {
public:
    void setup(int maxLen)
    {
        minf.setup(maxLen + 1);
        box1.setup(maxLen);
        box2.setup(maxLen);
        setLength(maxLen);
    }
    void setLength(int len)
    {
        L = std::max(2, len);
        const int a = std::max(1, L / 2), b = std::max(1, L - a + 1);
        minf.setLength(L + 1);
        box1.setLength(a);
        box2.setLength(b);
        reset();
    }
    void reset() { minf.reset(); box1.reset(1.0); box2.reset(1.0); }
    // Two cascaded box filters (a triangular window) of total length L: smooth and still reaches the minimum in time.
    inline float process(float needed)
    {
        const float m = minf.process(needed);
        return (float)box2.process(box1.process(m));
    }
    int length() const { return L; }
private:
    SlidingMin minf;
    MovingAverage box1, box2;
    int L = 2;
};

} // namespace pump
