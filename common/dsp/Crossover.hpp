// pump DSP: Linkwitz-Riley crossovers (24 dB/oct) built from TPT state-variable filters, which stay clean while
// the crossover frequencies move, and a 4-band splitter whose bands sum back to a flat (allpass) response.
// Also a linear-phase 4-band splitter (FIR, exact reconstruction) for mastering use.
#pragma once

#include "Util.hpp"
#include "FFT.hpp"

namespace pump {

// Topology-preserving-transform SVF (A. Simper / Cytomic). One channel.
struct SVF {
    double g = 0.0, k = 1.41421356, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
    void set(double freq, double q, double sr)
    {
        g = std::tan(kPi * clampv(freq, 5.0, 0.49 * sr) / sr);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.0; }
    // outputs low, band, high
    inline void process(double v0, double& lp, double& bp, double& hp)
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
};

// One LR4 split point for one channel: low = LP2(LP2(x)), high = HP2(HP2(x)), both Butterworth (Q = 0.7071).
struct LR4Split {
    SVF lp1, lp2, hp1, hp2;
    void set(double f, double sr)
    {
        const double q = 0.70710678118654752;
        lp1.set(f, q, sr); lp2.set(f, q, sr); hp1.set(f, q, sr); hp2.set(f, q, sr);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    inline void process(double x, double& lo, double& hi)
    {
        double l, b, h;
        lp1.process(x, l, b, h);
        double l2, b2, h2;
        lp2.process(l, l2, b2, h2);
        lo = l2;
        hp1.process(x, l, b, h);
        hp2.process(h, l2, b2, h2);
        hi = h2;
    }
};

// The allpass that an LR4 split's low+high sum equals: 2nd order, Butterworth Q (x - 2k*bp of an SVF).
struct LR4Allpass {
    SVF f;
    void set(double freq, double sr) { f.set(freq, 0.70710678118654752, sr); }
    void reset() { f.reset(); }
    inline double process(double x)
    {
        double l, b, h;
        f.process(x, l, b, h);
        return x - 2.0 * f.k * b;
    }
};

// Up to 4 bands from 3 split points (f1 < f2 < f3), stereo. Tree: split at f2, then the low side at f1 and the
// high side at f3; each side gets the other side's allpass so all four bands share the same phase and their sum
// is an allpass (flat magnitude). numBands 1..4: fewer bands simply don't use the upper split points.
class Crossover4 {
public:
    void prepare(double sampleRate) { sr = sampleRate; reset(); setFrequencies(120.0, 1000.0, 6000.0); }
    void setBands(int n) { nb = clampv(n, 1, 4); }
    int bands() const { return nb; }
    void setFrequencies(double f1, double f2, double f3)
    {
        fr[0] = f1; fr[1] = f2; fr[2] = f3;
        for (int ch = 0; ch < 2; ++ch)
        {
            s1[ch].set(f1, sr); s2[ch].set(f2, sr); s3[ch].set(f3, sr);
            ap1[ch].set(f1, sr); ap3[ch].set(f3, sr); ap2[ch].set(f2, sr); ap2b[ch].set(f2, sr); ap3b[ch].set(f3, sr);
        }
    }
    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            s1[ch].reset(); s2[ch].reset(); s3[ch].reset(); ap1[ch].reset(); ap3[ch].reset(); ap2[ch].reset(); ap2b[ch].reset(); ap3b[ch].reset();
        }
    }
    // out[band] receives the band sample for channel ch.
    inline void process(int ch, double x, double* out)
    {
        switch (nb)
        {
        case 1: out[0] = x; break;
        case 2: s2[ch].process(x, out[0], out[1]); break;
        case 3: {
            // splits at f1 and f2 (bands: <f1, f1..f2, >f2)
            double lo, hi;
            s2[ch].process(x, lo, hi);
            double a, b;
            s1[ch].process(lo, a, b);
            out[0] = a; out[1] = b;
            out[2] = ap1[ch].process(hi);
            break;
        }
        default: {
            double lo, hi;
            s2[ch].process(x, lo, hi);
            double a, b, c, d;
            s1[ch].process(ap3[ch].process(lo), a, b);
            s3[ch].process(ap1[ch].process(hi), c, d);
            out[0] = a; out[1] = b; out[2] = c; out[3] = d;
            break;
        }
        }
    }
    // Magnitude-squared of band b at frequency f (Hz), for displays (exact LR4 shapes).
    double bandMagSq(int b, double f) const
    {
        auto lp = [](double f, double fc) { const double r = std::pow(f / fc, 4.0); return 1.0 / (1.0 + r); };   // |B_LP|^2 Butterworth 2nd
        auto hp = [](double f, double fc) { const double r = std::pow(f / fc, 4.0); return r / (1.0 + r); };
        auto LRlp = [&](double fc) { const double v = lp(f, fc); return v * v; };   // |LR4 LP|^2 = (|B|^2)^2
        auto LRhp = [&](double fc) { const double v = hp(f, fc); return v * v; };
        switch (nb)
        {
        case 1: return 1.0;
        case 2: return b == 0 ? LRlp(fr[1]) : LRhp(fr[1]);
        case 3:
            if (b == 0) return LRlp(fr[1]) * LRlp(fr[0]);
            if (b == 1) return LRlp(fr[1]) * LRhp(fr[0]);
            return LRhp(fr[1]);
        default:
            if (b == 0) return LRlp(fr[1]) * LRlp(fr[0]);
            if (b == 1) return LRlp(fr[1]) * LRhp(fr[0]);
            if (b == 2) return LRhp(fr[1]) * LRlp(fr[2]);
            return LRhp(fr[1]) * LRhp(fr[2]);
        }
    }
private:
    double sr = 48000.0, fr[3] = { 120, 1000, 6000 };
    int nb = 4;
    LR4Split s1[2], s2[2], s3[2];
    LR4Allpass ap1[2], ap3[2], ap2[2], ap2b[2], ap3b[2];
};

} // namespace pump
