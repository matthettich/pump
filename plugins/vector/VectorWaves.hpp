// pump VECTOR: the wave bank. 32 single-cycle waves, each kept at 11 band limits (1024 harmonics, 512, ... 1) so a
// note never plays harmonics above the Nyquist frequency. Built once (about 350 FFTs of 2048 points) from either a
// spectrum (the classic and the vowel waves) or a drawn cycle (sync, folds, the digital ones).
#pragma once

#include "dsp/FFT.hpp"
#include "VectorParams.hpp"
#include <array>
#include <cmath>
#include <vector>

namespace pump { namespace vector {

class WaveBank {
public:
    static constexpr int kN = 2048, kMips = 11;
    static const WaveBank& get() { static const WaveBank b; return b; }
    static int mipFor(double inc)
    {
        const double maxH = 0.5 / std::max(1e-9, std::fabs(inc));
        int m = 0;
        while (m < kMips - 1 && (1024 >> m) > maxH) ++m;
        return m;
    }
    inline double read(int w, int m, double p) const
    {
        const float* t = tab[(size_t)clampv(w, 0, kNumWaves - 1)][(size_t)clampv(m, 0, kMips - 1)].data();
        const double x = (p - std::floor(p)) * kN;
        const int i = (int)x;
        const double f = x - i;
        return t[i] + (t[i + 1] - t[i]) * f;
    }
    // the wave at full bandwidth at N points round the cycle (for the scanned string's hammer)
    void shape(int w, float* dst, int n) const { for (int i = 0; i < n; ++i) dst[i] = (float)read(w, 3, (double)i / n); }

private:
    std::array<std::array<std::vector<float>, kMips>, kNumWaves> tab;

    // a wave's spectrum (amplitude of the sine of each harmonic, and of the cosine) up to 1024
    struct Spec { std::vector<double> s, c; Spec() : s(1025, 0.0), c(1025, 0.0) {} };
    static double formant(double hz, double f1, double f2, double f3)
    {
        auto g = [](double x, double f, double bw) { const double d = (x - f) / bw; return std::exp(-0.5 * d * d); };
        return 0.05 + g(hz, f1, 90.0) + 0.7 * g(hz, f2, 120.0) + 0.4 * g(hz, f3, 180.0);
    }
    static uint32_t hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
    static double rnd(uint32_t a, uint32_t b) { return (hash(a * 7919u + b * 104729u + 17u) & 0xFFFFFF) / 16777216.0; }

    // a drawn cycle (kN points) -> spectrum
    static Spec fromCycle(const std::vector<float>& x, RealFFT& fft)
    {
        std::vector<float> out(kN);
        fft.forward(x.data(), out.data());
        Spec sp;
        for (int h = 1; h < kN / 2 && h <= 1024; ++h)
        {
            // ordered: re, im of bin h at 2h, 2h + 1; x = sum (re cos - im sin) * 2 / N
            sp.c[(size_t)h] = 2.0 * out[(size_t)(2 * h)] / kN;
            sp.s[(size_t)h] = -2.0 * out[(size_t)(2 * h + 1)] / kN;
        }
        return sp;
    }
    static Spec recipe(int w, RealFFT& fft)
    {
        Spec sp;
        auto saw = [&](double tilt) { for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = 1.0 / std::pow(h, tilt); };
        auto drawn = [&](auto fn) { std::vector<float> x(kN); for (int i = 0; i < kN; ++i) x[(size_t)i] = (float)fn((double)i / kN); return fromCycle(x, fft); };
        const double ref = 130.0;   // the vowel waves' formants are placed for a note near C3
        switch (w)
        {
        case 0: sp.s[1] = 1.0; break;
        case 1: for (int h = 1; h <= 1024; h += 2) sp.s[(size_t)h] = ((h / 2) % 2 ? -1.0 : 1.0) / (h * h); break;
        case 2: saw(1.0); break;
        case 3: for (int h = 1; h <= 1024; h += 2) sp.s[(size_t)h] = 1.0 / h; break;
        case 4: case 5: { const double d = w == 4 ? 0.25 : 0.1; for (int h = 1; h <= 1024; ++h) sp.c[(size_t)h] = std::sin(kPi * h * d) / h; break; }
        case 6: { const int hs[6] = { 1, 2, 3, 4, 6, 8 }; const double a[6] = { 1, 0.8, 0.6, 0.5, 0.4, 0.3 }; for (int k = 0; k < 6; ++k) sp.s[(size_t)hs[k]] = a[k]; break; }
        case 7: { const int hs[7] = { 1, 3, 5, 8, 10, 12, 16 }; const double a[7] = { 0.8, 1, 0.6, 0.4, 0.5, 0.3, 0.3 }; for (int k = 0; k < 7; ++k) sp.s[(size_t)hs[k]] = a[k]; break; }
        case 8: case 9: case 10: case 11: case 12: case 13:
        {
            static const double F[6][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 }, { 650, 960, 2420 } };
            const double* f = F[w - 8];
            for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = formant(h * ref, f[0], f[1], f[2]) / h * (w == 13 ? std::exp(-h * ref / 4000.0) : 1.0);
            break;
        }
        case 14: { const int hs[6] = { 1, 4, 7, 11, 16, 22 }; const double a[6] = { 1, 0.7, 0.55, 0.4, 0.3, 0.2 }; for (int k = 0; k < 6; ++k) sp.s[(size_t)hs[k]] = a[k]; break; }
        case 15: for (int k = 0; k < 8; ++k) { const int h = 1 + 4 * k; sp.s[(size_t)h] = std::sin(kPi * (k + 1) / 9.0) * 0.8 + (k == 0 ? 0.5 : 0.0); } break;
        case 16: for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = std::fabs(std::sin(kPi * h * 0.12)) / h; break;
        case 17: for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = (h & 1) ? 1.0 / std::pow(h, 0.8) : 0.15 / h; break;
        case 18: for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = (1.0 + 1.5 * std::exp(-0.5 * std::pow((h - 8.0) / 3.0, 2.0))) / std::pow(h, 0.75); break;
        case 19: for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = (0.8 + 0.2 * std::cos(h * 0.9)) / std::pow(h, 1.15); break;
        case 20: for (int h = 6; h <= 14; ++h) sp.s[(size_t)h] = 1.0 - std::fabs(h - 10.0) / 6.0; sp.s[1] = 0.3; break;
        case 21: for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = 0.5 * (1.0 + std::cos(kTwoPi * h * 0.2)) / h; break;
        case 22: case 23: { const double r = w == 22 ? 2.3 : 3.7; return drawn([&](double p) { const double q = p * r; return 2.0 * (q - std::floor(q)) - 1.0; }); }
        case 24: return drawn([](double p) { return std::sin(3.2 * std::sin(kTwoPi * p)); });
        case 25: return drawn([](double p) { const double t = p < 0.25 ? 4 * p : (p < 0.75 ? 2 - 4 * p : 4 * p - 4); double u = 6.0 * t + 1.0; u -= 4.0 * std::floor(u / 4.0); return u < 2.0 ? u - 1.0 : 3.0 - u; });
        case 26: for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = rnd(26, h) / std::sqrt((double)h); break;
        case 27: return drawn([](double p) { return std::round(std::sin(kTwoPi * p) * 2.0) / 2.0; });
        case 28: return drawn([](double p) { return 2.0 * rnd(28, (uint32_t)(p * 32.0)) - 1.0; });
        case 29: for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = h <= 64 ? 0.4 : 0.4 * 64.0 / h; break;
        case 30: for (int h = 1; h <= 1024; ++h) sp.s[(size_t)h] = (1.0 + 4.0 * std::exp(-0.5 * std::pow((h - 12.0) / 1.5, 2.0))) / h; break;
        default: saw(0.7); break;
        }
        return sp;
    }

    WaveBank()
    {
        RealFFT fft(kN);
        std::vector<float> in(kN), out(kN);
        for (int w = 0; w < kNumWaves; ++w)
        {
            const Spec sp = recipe(w, fft);
            double norm = 1.0;
            for (int m = 0; m < kMips; ++m)
            {
                const int H = 1024 >> m;
                // spectrum -> cycle (ordered layout: [dc, nyquist, re1, im1, ...]); harmonics above H are left out
                std::fill(in.begin(), in.end(), 0.0f);
                for (int h = 1; h <= H && h < kN / 2; ++h) { in[(size_t)(2 * h)] = (float)(sp.c[(size_t)h] * 0.5); in[(size_t)(2 * h + 1)] = (float)(-sp.s[(size_t)h] * 0.5); }
                fft.inverse(in.data(), out.data());
                if (m == 0)
                {
                    // every wave about as loud as the others (rms 0.45), but never peaking past 1.5
                    double pk = 1e-9, e = 0.0; for (float v : out) { pk = std::max(pk, (double)std::fabs(v)); e += (double)v * v; }
                    norm = std::min(0.45 / std::sqrt(e / kN + 1e-18), 1.5 / pk);
                }
                std::vector<float>& t = tab[(size_t)w][(size_t)m];
                t.assign((size_t)kN + 1, 0.0f);
                for (int i = 0; i < kN; ++i) t[(size_t)i] = (float)(out[(size_t)i] * norm);
                t[(size_t)kN] = t[0];
            }
        }
    }
};

}} // namespace pump::vector
