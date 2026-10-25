// pump TABLE: wavetables. A table is a stack of single-cycle frames (2048 samples); each frame is kept as
// band-limited copies ("mips"), one per octave of playback pitch, each holding only the harmonics that stay below
// Nyquist for the pitches it serves (so nothing aliases). Factory tables are generated from formulas; a user table
// comes from a .wav file of 2048-sample frames (the common format). Building allocates: never on the audio thread.
#pragma once

#include "dsp/FFT.hpp"
#include "dsp/Fx.hpp"
#include "dsp/Wav.hpp"
#include <memory>
#include <string>

namespace pump { namespace table {

constexpr int kFrameSize = 2048, kHarmonics = kFrameSize / 2, kMips = 11, kMinMipSize = 256;

// mip m keeps harmonics 1 .. (1024 >> m) (at least 1), stored with at least 8 samples per period of its highest
// harmonic (256 .. 2048 samples), read with 4-point Hermite interpolation: the interpolation's own error stays far
// below the signal (linear interpolation of a table holding harmonics near its Nyquist makes audible images)
inline int mipHarmonics(int m) { return std::max(1, kHarmonics >> m); }
inline int mipSize(int m) { return std::min(kFrameSize, std::max(kMinMipSize, 8 * mipHarmonics(m))); }

struct Table {
    std::string name;
    int frames = 0;
    // data[frame][mip] -> mipSize(mip) samples, stored from index 1 with guards: [x(n-1), x0 .. x(n-1), x0, x1]
    std::vector<std::vector<std::vector<float>>> data;
    // the first frames as drawn by the window (mip 0, decimated to 128 points)
    std::vector<std::vector<float>> preview;

    // the mip to use for a phase increment (cycles per sample)
    static int mipFor(double inc)
    {
        const double maxH = 0.5 / std::max(1e-9, inc);   // harmonics below Nyquist
        int m = 0;
        while (m < kMips - 1 && mipHarmonics(m) > maxH) ++m;
        return m;
    }
    // one sample at phase p (0..1), position pos (0..1 over the frames), from mip m; linear in phase and position
    inline float read(double p, double pos, int m) const
    {
        const double fpos = clampv(pos, 0.0, 1.0) * (frames - 1);
        const int f0 = (int)fpos, f1 = std::min(frames - 1, f0 + 1);
        const float t = (float)(fpos - f0);
        const int n = mipSize(m);
        const double x = p * n;
        const int i = (int)x;
        const float fr = (float)(x - i);
        const float* a = data[(size_t)f0][(size_t)m].data() + i;
        const float* b = data[(size_t)f1][(size_t)m].data() + i;
        auto herm = [fr](const float* y) {   // y[0..3] = x(i-1), x(i), x(i+1), x(i+2)
            const float c1 = 0.5f * (y[2] - y[0]), c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3], c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
            return ((c3 * fr + c2) * fr + c1) * fr + y[1];
        };
        const float va = herm(a);
        if (t <= 0.0f || f0 == f1) return va;
        const float vb = herm(b);
        return va + (vb - va) * t;
    }
};

// Builds a table from time-domain frames (each kFrameSize samples): FFT, then the mips by inverse FFT of the
// truncated spectrum. Normalises the whole table to a common peak and removes DC.
inline std::shared_ptr<Table> buildTable(const std::string& name, const std::vector<std::vector<float>>& frames)
{
    auto t = std::make_shared<Table>();
    t->name = name;
    t->frames = (int)frames.size();
    t->data.resize(frames.size());
    RealFFT fwd; fwd.setup(kFrameSize);
    std::vector<RealFFT> inv(kMips);
    for (int m = 0; m < kMips; ++m) inv[(size_t)m].setup(mipSize(m));
    AlignedBuffer in((size_t)kFrameSize), spec((size_t)kFrameSize), mspec((size_t)kFrameSize), mout((size_t)kFrameSize);
    float peak = 1e-9f;
    for (size_t f = 0; f < frames.size(); ++f)
    {
        for (int i = 0; i < kFrameSize; ++i) in[(size_t)i] = frames[f][(size_t)i];
        fwd.forward(in.data(), spec.data());
        t->data[f].resize(kMips);
        for (int m = 0; m < kMips; ++m)
        {
            const int n = mipSize(m), H = std::min(mipHarmonics(m), n / 2 - 1);
            for (int i = 0; i < n; ++i) mspec[(size_t)i] = 0.0f;
            for (int h = 1; h <= H; ++h) { mspec[(size_t)(2 * h)] = spec[(size_t)(2 * h)]; mspec[(size_t)(2 * h + 1)] = spec[(size_t)(2 * h + 1)]; }
            inv[(size_t)m].inverse(mspec.data(), mout.data());
            std::vector<float>& d = t->data[f][(size_t)m];
            d.resize((size_t)n + 3);
            // the unscaled inverse of the (truncated) size-N spectrum at size n gives N x the signal at those points
            const float k = 1.0f / (float)kFrameSize;
            for (int i = 0; i < n; ++i) d[(size_t)i + 1] = mout[(size_t)i] * k;
            d[0] = d[(size_t)n]; d[(size_t)n + 1] = d[1]; d[(size_t)n + 2] = d[2];
            if (m == 0) for (int i = 1; i <= n; ++i) peak = std::max(peak, std::fabs(d[(size_t)i]));
        }
    }
    const float g = 1.0f / peak;
    for (auto& fr : t->data) for (auto& mp : fr) for (float& v : mp) v *= g;
    // previews: up to 64 frames, 128 points each
    const int pf = std::min(64, t->frames);
    for (int k = 0; k < pf; ++k)
    {
        const int f = t->frames == 1 ? 0 : (int)std::lround((double)k * (t->frames - 1) / std::max(1, pf - 1));
        std::vector<float> p(128);
        for (int i = 0; i < 128; ++i) p[(size_t)i] = t->data[(size_t)f][0][(size_t)(1 + i * mipSize(0) / 128)];
        t->preview.push_back(std::move(p));
    }
    return t;
}

// ---- factory tables ----------------------------------------------------------------------------------------------
enum FactoryTable : int { kAnalog = 0, kHarmonic, kFormant, kSync, kPwm, kFm, kFold, kDigital, kOddEven, kSpectral, kNumFactory };
static const char* const kFactoryNames[] = { "Analog", "Harmonic", "Formant", "Sync", "PWM", "FM", "Fold", "Digital", "Odd / even", "Spectral" };
constexpr int kFactoryFrames = 32;

inline std::vector<std::vector<float>> factoryFrames(int which)
{
    const int F = kFactoryFrames, N = kFrameSize;
    std::vector<std::vector<float>> out((size_t)F, std::vector<float>((size_t)N, 0.0f));
    fx::Rng rng(0xBADA55u + (uint32_t)which * 977u);
    // additive helper: frame from harmonic amplitudes (sine phase)
    auto additive = [&](std::vector<float>& d, const std::vector<double>& amp) {
        for (size_t h = 1; h < amp.size(); ++h)
            if (amp[h] != 0.0)
                for (int i = 0; i < N; ++i) d[(size_t)i] += (float)(amp[h] * std::sin(kTwoPi * (double)h * i / N));
    };
    for (int f = 0; f < F; ++f)
    {
        const double t = (double)f / (F - 1);
        std::vector<float>& d = out[(size_t)f];
        switch (which)
        {
        case kAnalog:   // sine -> triangle -> saw -> square -> narrow pulse
        {
            for (int i = 0; i < N; ++i)
            {
                const double p = (double)i / N;
                const double sine = std::sin(kTwoPi * p), tri = 1.0 - 4.0 * std::fabs(p - 0.5) * 1.0, saw = 2.0 * p - 1.0, sq = p < 0.5 ? 1.0 : -1.0;
                const double pulse = p < 0.12 ? 1.0 : -0.27;
                const double seg = t * 4.0;
                const int k = std::min(3, (int)seg);
                const double u = seg - k;
                const double a[5] = { sine, -tri, saw, sq, pulse };
                d[(size_t)i] = (float)(a[k] + (a[k + 1] - a[k]) * u);
            }
            break;
        }
        case kHarmonic:  // one partial, then more and more (1/n)
        {
            const int H = 1 + (int)std::lround(t * t * 63.0);
            std::vector<double> amp(65, 0.0);
            for (int h = 1; h <= H; ++h) amp[(size_t)h] = 1.0 / h;
            additive(d, amp);
            break;
        }
        case kFormant:   // a buzz through vowel formants: A E I O U
        {
            static const double fm[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 280, 2250, 2900 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const double seg = t * 4.0;
            const int k = std::min(3, (int)seg);
            const double u = seg - k, f0 = 110.0;
            std::vector<double> amp(200, 0.0);
            for (int h = 1; h < 200; ++h)
            {
                const double fr = h * f0;
                double a = 0.0;
                for (int j = 0; j < 3; ++j)
                {
                    const double c = fm[k][j] + (fm[k + 1][j] - fm[k][j]) * u, bw = 60.0 + 0.06 * c;
                    a += (j == 0 ? 1.0 : 0.6 / j) / (1.0 + std::pow((fr - c) / bw, 2.0));
                }
                amp[(size_t)h] = a / std::sqrt((double)h);
            }
            additive(d, amp);
            break;
        }
        case kSync:      // a saw hard-synced at 1x .. 8x
        {
            const double r = 1.0 + 7.0 * t;
            for (int i = 0; i < N; ++i) { const double p = (double)i / N * r; d[(size_t)i] = (float)(2.0 * (p - std::floor(p)) - 1.0); }
            break;
        }
        case kPwm:       // pulse width 50 % -> 3 %
        {
            const double w = 0.5 - 0.47 * t;
            for (int i = 0; i < N; ++i) d[(size_t)i] = (float)(((double)i / N < w ? 1.0 : -1.0) - (2.0 * w - 1.0));
            break;
        }
        case kFm:        // two-operator FM, ratio 2, index 0 -> 8
        {
            const double I = 8.0 * t;
            for (int i = 0; i < N; ++i) { const double p = (double)i / N; d[(size_t)i] = (float)std::sin(kTwoPi * p + I * std::sin(kTwoPi * 2.0 * p)); }
            break;
        }
        case kFold:      // a sine folded back on itself harder and harder
        {
            const double g = 1.0 + 7.0 * t;
            for (int i = 0; i < N; ++i)
            {
                double x = g * std::sin(kTwoPi * i / N);
                x = std::asin(std::sin(0.5 * kPi * x)) / (0.5 * kPi);   // triangle fold
                d[(size_t)i] = (float)x;
            }
            break;
        }
        case kDigital:   // a sine stepped to fewer and fewer levels, then bit patterns
        {
            const double levels = std::max(2.0, std::round(64.0 * std::pow(1.0 / 32.0, t)));
            for (int i = 0; i < N; ++i)
            {
                const double s = std::sin(kTwoPi * i / N) + 0.3 * std::sin(kTwoPi * 3.0 * i / N) * t;
                d[(size_t)i] = (float)(std::round(s * levels * 0.5) / (levels * 0.5));
            }
            break;
        }
        case kOddEven:   // odd harmonics only (square-ish) -> even only (an octave-up saw)
        {
            std::vector<double> amp(129, 0.0);
            for (int h = 1; h <= 128; ++h) amp[(size_t)h] = (h % 2 ? (1.0 - t) : t) / h;
            amp[1] = std::max(amp[1], 0.3);
            additive(d, amp);
            break;
        }
        case kSpectral:  // random spectra, smooth to rough
        default:
        {
            std::vector<double> amp(129, 0.0);
            fx::Rng r2(0xC0DEu + (uint32_t)f * 31u);
            for (int h = 1; h <= 128; ++h) amp[(size_t)h] = std::pow(r2.uni(), 1.0 + 3.0 * (1.0 - t)) / std::pow((double)h, 1.2 - 0.6 * t);
            amp[1] = 1.0;
            additive(d, amp);
            break;
        }
        }
    }
    return out;
}

// all factory tables, built once and shared by every instance
inline const std::vector<std::shared_ptr<Table>>& factory()
{
    static const std::vector<std::shared_ptr<Table>> v = [] {
        std::vector<std::shared_ptr<Table>> t;
        for (int k = 0; k < kNumFactory; ++k) t.push_back(buildTable(kFactoryNames[k], factoryFrames(k)));
        return t;
    }();
    return v;
}

// a user table from a .wav file: consecutive 2048-sample frames (up to 256); a file that isn't a multiple of 2048
// is read as one cycle and resampled to a single frame
inline std::shared_ptr<Table> loadUserTable(const std::string& path, std::string& error)
{
    WavData w = readWav(path, (size_t)kFrameSize * 256);
    if (!w.ok()) { error = w.error.empty() ? "empty file" : w.error; return nullptr; }
    const std::vector<float>& x = w.channels[0];
    std::vector<std::vector<float>> frames;
    if (x.size() >= (size_t)kFrameSize && x.size() % kFrameSize == 0)
        for (size_t o = 0; o + kFrameSize <= x.size(); o += kFrameSize) frames.emplace_back(x.begin() + (long)o, x.begin() + (long)o + kFrameSize);
    else
    {
        std::vector<float> f((size_t)kFrameSize);
        for (int i = 0; i < kFrameSize; ++i)
        {
            const double p = (double)i / kFrameSize * x.size();
            const size_t k = (size_t)p;
            const double fr = p - k;
            f[(size_t)i] = (float)(x[k % x.size()] + (x[(k + 1) % x.size()] - x[k % x.size()]) * fr);
        }
        frames.push_back(std::move(f));
    }
    std::string name = path;
    const size_t s = name.find_last_of("/\\");
    if (s != std::string::npos) name = name.substr(s + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    error.clear();
    return buildTable(name, frames);
}

}} // namespace pump::table
