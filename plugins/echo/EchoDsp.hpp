// pump ECHO: a pitch shifter for the echoes (a candidate for common/dsp/Fx.hpp).
//
// AlignedPitchShifter: a delay-line pitch shifter with one sounding read head that sweeps through the line at the
// pitch ratio. When the head nears the end of its range it hands over to a second head about 40 ms away, over a
// sin^2 crossfade of up to 15 ms. The jump is not fixed: it is chosen within +-12 ms (periods down to ~42 Hz) to
// maximise the correlation of the line around the two heads, so they are in phase when they cross. (A fixed-window
// two-head shifter cancels whenever its heads happen to be half a period apart: a 220 Hz tone shifted up an octave
// with 50 ms windows comes out as 430 + 450 Hz with a hole at 440.) Tones stay whole; noisy material still gets a
// smooth crossfade. The search runs once per splice (coarse, then refined around the best lag). Mono.
#pragma once

#include "dsp/Fx.hpp"

namespace pump { namespace echo {

class AlignedPitchShifter {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        J = std::floor(0.040 * sr);
        S = std::floor(0.012 * sr);
        Xmax = std::floor(0.015 * sr);
        Lc = std::max(32, (int)(0.010 * sr));
        const double k = sr / 48000.0;
        lagStride = std::max(2, (int)std::lround(2.0 * k));
        sampleStride = std::max(4, (int)std::lround(4.0 * k));
        maxD = kMin + 2.0 * S + 2.0 * J + 2.0 * Xmax + Lc + 16.0;
        line.setMax((int)maxD + 8);
        reset();
    }
    void reset()
    {
        line.clear();
        m = 0;
        d[0] = d[1] = averageDelay();
        xf = xfLen = 1;
    }
    void setRatio(double r) { ratio = clampv(r, 0.25, 4.0); }
    // the sounding head's mean delay (for timing compensation)
    double averageDelay() const { return kMin + S + 0.5 * J + std::min(Xmax * std::fabs(ratio - 1.0), 0.5 * J); }

    // while the shifter isn't heard: keep its line current (the heads hold their delays), so it can fade in at once
    inline void write(float x) { line.push(x); }

    inline float process(float x)
    {
        line.push(x);
        const double dr = ratio - 1.0;   // the delay shrinks by this per sample (grows when negative)
        d[0] = clampv(d[0] - dr, 1.0, maxD);
        d[1] = clampv(d[1] - dr, 1.0, maxD);
        if (xf >= xfLen)
        {
            const double a = std::fabs(dr);
            if (a > 1e-9)
            {
                const double xs = std::min(Xmax, 0.5 * J / a);
                if (dr > 0.0 && d[m] <= kMin + S + xs * a) splice(+1, xs);
                else if (dr < 0.0 && d[m] >= kMin + S + J + xs * a) splice(-1, xs);
            }
        }
        if (xf >= xfLen) return line.read(d[m]);
        const double t = (xf + 0.5) / xfLen, s = std::sin(0.5 * kPi * t), gn = s * s;
        const float y = (float)((1.0 - gn) * line.read(d[1 - m]) + gn * line.read(d[m]));
        if (++xf >= xfLen) d[1 - m] = d[m];
        return y;
    }

private:
    // start the other head `dir` x (a jump near J) away, in phase with this one, and make it the sounding one
    void splice(int dir, double xs)
    {
        const int base = (int)std::lround(d[m]);
        auto corr = [&](int lag, int stride) {
            double ab = 0.0, bb = 1e-12;
            for (int k = 0; k < Lc; k += stride)
            {
                const double av = line.at(base + k), bv = line.at(base + dir * lag + k);
                ab += av * bv; bb += bv * bv;
            }
            return ab / std::sqrt(bb);
        };
        const int lo = (int)(J - S), hi = (int)(J + S);
        int best = (int)J;
        double bestC = -1e30;
        for (int lag = lo; lag <= hi; lag += lagStride)
        {
            const double c = corr(lag, sampleStride);
            if (c > bestC) { bestC = c; best = lag; }
        }
        const int c0 = best;
        bestC = -1e30;
        for (int lag = std::max(lo, c0 - lagStride); lag <= std::min(hi, c0 + lagStride); ++lag)
        {
            const double c = corr(lag, 1);
            if (c > bestC) { bestC = c; best = lag; }
        }
        const int n = 1 - m;
        d[n] = clampv(d[m] + dir * best, 1.0, maxD);
        m = n;   // the new head sounds; the old one fades out
        xfLen = std::max(1, (int)xs);
        xf = 0;
    }

    static constexpr double kMin = 2.0;
    fx::FracDelay line;
    double sr = 48000.0, J = 1920.0, S = 576.0, Xmax = 720.0, ratio = 1.0, maxD = 8192.0;
    double d[2] = { 1000.0, 1000.0 };
    int m = 0, xf = 1, xfLen = 1, Lc = 480, lagStride = 2, sampleStride = 4;
};

}} // namespace pump::echo
