// pump DSP: click-free bypass. Keeps a copy of the input delayed by the plugin's latency and crossfades between
// it and the processed signal, so bypassing never shifts timing or clicks.
#pragma once

#include "Util.hpp"

namespace pump {

class BypassMixer {
public:
    void prepare(double sr, int maxLatency, int maxBlock)
    {
        for (auto& d : dly) { d.setMaxDelay(maxLatency + 1); d.clear(); }
        dryL.assign((size_t)maxBlock, 0.0f); dryR.assign((size_t)maxBlock, 0.0f);
        fade.setRamp((int)(0.015 * sr));
        fade.reset(on ? 1.0 : 0.0);
        xfLen = std::max(1, (int)(0.010 * sr));
        xfPos = xfLen;
    }
    // A latency change while bypassed would make the dry signal jump (a click): crossfade from the old delay to
    // the new one over 10 ms instead.
    void setLatency(int samples)
    {
        const int old = dly[0].getDelay();
        for (auto& d : dly) d.setDelay(samples);
        if (dly[0].getDelay() != old) { xfOld = old; xfPos = 0; }
    }
    // call before processing (in-place buffers): remembers the dry input
    void capture(const float* L, const float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float l = dly[0].process(L[i]), r = dly[1].process(R[i]);
            if (xfPos < xfLen)
            {
                const float t = ((float)xfPos + 0.5f) / (float)xfLen;
                const float ol = dly[0].read(xfOld), orr = dly[1].read(xfOld);
                l = ol + (l - ol) * t; r = orr + (r - orr) * t;
                ++xfPos;
            }
            dryL[(size_t)i] = l; dryR[(size_t)i] = r;
        }
    }
    // call after processing: crossfades to the delayed dry signal when bypassed
    void apply(float* L, float* R, int n, bool bypassed)
    {
        on = bypassed;
        fade.setTarget(bypassed ? 1.0 : 0.0);
        if (!fade.isSmoothing() && fade.current() == 0.0) return;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float)fade.next();
            // fully bypassed means exactly the dry signal (L + (dry - L) * 1 stays NaN if L is NaN)
            if (t >= 1.0f) { L[i] = dryL[(size_t)i]; R[i] = dryR[(size_t)i]; continue; }
            L[i] += (dryL[(size_t)i] - L[i]) * t;
            R[i] += (dryR[(size_t)i] - R[i]) * t;
        }
    }
    bool fullyBypassed() const { return !fade.isSmoothing() && fade.current() >= 1.0; }
private:
    DelayLine dly[2];
    std::vector<float> dryL, dryR;
    LinearSmoother fade;
    bool on = false;
    int xfOld = 0, xfPos = 1, xfLen = 1;
};

} // namespace pump
