// pump DSP: "Alt-solo" audition — while the user holds Alt on a spectrum display, the output is
// replaced by just the region under the mouse: a band-pass (or the part below/above a frequency), crossfaded in and
// out over 10 ms and glided when the mouse moves, so sweeping it is smooth. Framework-free.
#pragma once

#include "Biquad.hpp"

namespace pump {

class Audition {
public:
    enum Region { kBand = 0, kBelow, kAbove };

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        fade.setRamp((int)(0.010 * sr));
        fade.reset(0.0);
        for (auto& c : st) for (auto& s : c) s.reset();
        designed = false;
    }
    // Call at block start. q is the band-pass width (0.3..12); for kBelow/kAbove q is ignored.
    void set(bool on, double freq, double q, Region region = kBand)
    {
        active = on;
        fade.setTarget(on ? 1.0 : 0.0);
        freq = clampv(freq, 10.0, 0.45 * sr);
        q = clampv(q, 0.3, 12.0);
        if (!designed || freq != curF || q != curQ || region != curR)
        {
            curF = freq; curQ = q; curR = region;
            AnalogSection a;
            const double w0 = kTwoPi * freq / sr;
            if (region == kBelow) { a.n2 = 0; a.n1 = 0; a.n0 = w0 * w0; a.d2 = 1; a.d1 = w0 / 0.7071; a.d0 = w0 * w0; }
            else if (region == kAbove) { a.n2 = 1; a.n1 = 0; a.n0 = 0; a.d2 = 1; a.d1 = w0 / 0.7071; a.d0 = w0 * w0; }
            else { a.n2 = 0; a.n1 = w0 / q; a.n0 = 0; a.d2 = 1; a.d1 = w0 / q; a.d0 = w0 * w0; }
            bestDesign(a, sr, target);
            if (!designed) cur = target;
            designed = true;
        }
    }
    bool busy() const { return active || fade.isSmoothing() || fade.current() > 0.0; }
    // In place. Glides the coefficients toward the target over the block.
    void process(float* L, float* R, int n)
    {
        if (!busy()) return;
        const int chunk = 16;
        for (int s0 = 0; s0 < n; s0 += chunk)
        {
            cur = BiquadCoeffs::lerp(cur, target, 0.25);
            const int s1 = std::min(n, s0 + chunk);
            for (int i = s0; i < s1; ++i)
            {
                const double t = fade.next();
                double x = L[i], y = R[i];
                double fx = st[0][1].process(cur, st[0][0].process(cur, x));
                double fy = st[1][1].process(cur, st[1][0].process(cur, y));
                L[i] = (float)(x + (fx - x) * t);
                R[i] = (float)(y + (fy - y) * t);
            }
        }
        if (!active && !fade.isSmoothing()) for (auto& c : st) for (auto& s : c) s.reset();
    }
private:
    double sr = 48000.0, curF = 1000.0, curQ = 2.0;
    Region curR = kBand;
    bool active = false, designed = false;
    BiquadCoeffs cur, target;
    BiquadState st[2][2];
    LinearSmoother fade;
};

} // namespace pump
