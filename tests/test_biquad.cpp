// Measures the matched EQ design against its analog prototype (max dB error from 20 Hz to 20 kHz, or to
// 0.97 * Nyquist when that is lower), next to plain bilinear-transform designs of the same prototype.
#include "../common/dsp/Biquad.hpp"
#include <cstdio>

using namespace pump;

static int fails = 0;

// BLT of an analog section with prewarping at its own centre (what RBJ cookbook filters do).
static BiquadCoeffs bilinear(const AnalogSection& a, double wc)
{
    BiquadCoeffs c;
    const double K = (a.firstOrder() ? 1.0 : 1.0) * wc / std::tan(0.5 * std::min(wc, 3.1));   // prewarp
    if (a.firstOrder())
    {
        const double b0 = a.n1 * K + a.n0, b1 = -a.n1 * K + a.n0, a0 = a.d1 * K + a.d0, a1 = -a.d1 * K + a.d0;
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.a1 = a1 / a0;
        return c;
    }
    const double K2 = K * K;
    const double a0 = a.d2 * K2 + a.d1 * K + a.d0;
    c.b0 = (a.n2 * K2 + a.n1 * K + a.n0) / a0;
    c.b1 = 2.0 * (a.n0 - a.n2 * K2) / a0;
    c.b2 = (a.n2 * K2 - a.n1 * K + a.n0) / a0;
    c.a1 = 2.0 * (a.d0 - a.d2 * K2) / a0;
    c.a2 = (a.d2 * K2 - a.d1 * K + a.d0) / a0;
    return c;
}

struct Err { double matched, blt; };

static Err measure(const BandSpec& b, double sr)
{
    const AnalogBand an = analogPrototype(b, sr);
    DigitalBand dg; dg.design(an, sr);
    // bilinear version
    double maxM = 0, maxB = 0;
    const double fTop = std::min(20000.0, 0.97 * 0.5 * sr);
    for (int i = 0; i <= 400; ++i)
    {
        const double f = 20.0 * std::pow(fTop / 20.0, i / 400.0);
        const double w = kTwoPi * f / sr;
        const double ref = 10 * std::log10(std::max(an.magSq(w), 1e-30));
        if (ref < -30) continue;   // deep stopband: inaudible, not meaningful
        const double m = 10 * std::log10(std::max(dg.magSq(w), 1e-30));
        double bl = an.gain * an.gain;
        for (int k = 0; k < an.n; ++k)
        {
            const AnalogSection& s = an.sec[k];
            const double wc = s.firstOrder() ? (s.d0 / s.d1) : std::sqrt(s.d0 / s.d2);
            bl *= bilinear(s, std::min(wc, 0.95 * kPi)).magSq(w);
        }
        const double bdb = 10 * std::log10(std::max(bl, 1e-30));
        maxM = std::max(maxM, std::fabs(m - ref));
        maxB = std::max(maxB, std::fabs(bdb - ref));
    }
    return { maxM, maxB };
}

int main()
{
    const double rates[] = { 44100, 48000, 96000 };
    const double freqs[] = { 30, 100, 400, 1000, 3000, 6000, 10000, 14000, 17000, 19000 };
    struct Case { const char* name; Shape s; double g; double q; int slope; };
    const Case cases[] = {
        { "Bell +12 Q1", Shape::Bell, 12, 1.0, 12 }, { "Bell -12 Q1", Shape::Bell, -12, 1.0, 12 },
        { "Bell +6 Q0.3", Shape::Bell, 6, 0.3, 12 }, { "Bell -18 Q8", Shape::Bell, -18, 8.0, 12 },
        { "Bell +3 Q4", Shape::Bell, 3, 4.0, 12 },
        { "LowShelf +9 12", Shape::LowShelf, 9, 0.707, 12 }, { "HighShelf +9 12", Shape::HighShelf, 9, 0.707, 12 },
        { "HighShelf -12 24", Shape::HighShelf, -12, 0.707, 24 }, { "LowShelf +6 6", Shape::LowShelf, 6, 0.707, 6 },
        { "HighShelf +6 6", Shape::HighShelf, 6, 0.707, 6 }, { "Tilt +6", Shape::TiltShelf, 6, 0.707, 12 },
        { "LowCut 24", Shape::LowCut, 0, 0.707, 24 }, { "LowCut 96", Shape::LowCut, 0, 0.707, 96 },
        { "HighCut 12", Shape::HighCut, 0, 0.707, 12 }, { "HighCut 48", Shape::HighCut, 0, 0.707, 48 },
        { "LowCut 18 Q2", Shape::LowCut, 0, 2.0, 18 }, { "BandPass", Shape::BandPass, 0, 2.0, 12 },
    };
    for (double sr : rates)
    {
        printf("== %.0f Hz: worst-case dB error vs analog (matched | bilinear) ==\n", sr);
        for (const Case& c : cases)
        {
            double wm = 0, wb = 0, fm = 0;
            for (double f : freqs)
            {
                if (f > 0.47 * sr) continue;
                BandSpec b; b.shape = c.s; b.freq = f; b.gainDb = c.g; b.q = c.q; b.slope = c.slope;
                Err e = measure(b, sr);
                if (e.matched > wm) { wm = e.matched; fm = f; }
                wb = std::max(wb, e.blt);
            }
            // the matched design is never meaningfully worse than the bilinear one, and stays within 4.5 dB of the
            // analog curve all the way to 0.97 Nyquist (today's worst: under 4 dB, a steep cut near the top)
            const bool ok = wm <= wb + 0.1 && wm < 4.5;
            if (!ok) ++fails;
            printf("  %s%-18s %7.3f dB (at %5.0f Hz)   | %7.3f dB\n", ok ? "" : "FAIL: ", c.name, wm, fm, wb);
        }
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
