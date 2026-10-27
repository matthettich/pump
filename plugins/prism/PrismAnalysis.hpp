// pump PRISM: the analysis. A sound is taken apart into partials: frame by frame (an FFT every ~5 ms), its pitch is
// found (subharmonic summation over the spectrum's peaks, smoothed), then every harmonic's level and exact frequency
// (the strongest peak near k x f0, parabolic interpolation), and what is left between them, the noise, as the level
// in twelve bands. Playback (PrismEngine) rebuilds the sound from these numbers, so it can be played at any pitch,
// stretched, frozen, morphed into another sound, and its partials reshaped.
// Not on the audio thread: it allocates and takes tens of milliseconds a second of sound.
#pragma once

#include "dsp/FFT.hpp"
#include "dsp/Util.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace pump { namespace prism {

constexpr int kK = 128;        // partials kept per frame
constexpr int kBands = 12;     // noise bands
inline double bandLo(int b) { return 80.0 * std::pow(16000.0 / 80.0, b / (double)kBands); }
inline double bandHi(int b) { return bandLo(b + 1); }

struct Analysis {
    std::string name;
    double sr = 48000.0, hopS = 0.005, seconds = 0.0, f0med = 220.0;
    int frames = 0;
    std::vector<float> amp, ratio;     // [frame * kK + k - 1]: level of harmonic k, its frequency / (k x f0)
    std::vector<float> f0;             // per frame (Hz)
    std::vector<float> noise;          // [frame * kBands + b]: noise level in band b
    std::vector<float> energy;         // per frame: the harmonics' total level (for the display)
    std::vector<float> overview;       // 1024 peaks of the waveform, for the display
    float maxAmp = 1e-6f;
};

inline std::shared_ptr<Analysis> analyse(const std::vector<float>& x, double sr, const std::string& name)
{
    auto A = std::make_shared<Analysis>();
    A->name = name; A->sr = sr;
    const int N = sr > 60000.0 ? 8192 : 4096, hop = N / 16;
    A->hopS = hop / sr;
    const int n = (int)x.size();
    A->seconds = n / sr;
    A->frames = std::max(1, (n + hop - 1) / hop);
    const int F = A->frames;
    A->amp.assign((size_t)F * kK, 0.0f); A->ratio.assign((size_t)F * kK, 1.0f);
    A->f0.assign((size_t)F, 0.0f); A->noise.assign((size_t)F * kBands, 0.0f); A->energy.assign((size_t)F, 0.0f);
    // the overview
    A->overview.assign(1024, 0.0f);
    for (int i = 0; i < n; ++i) { float& o = A->overview[(size_t)std::min(1023, (int)((int64_t)i * 1024 / std::max(1, n)))]; o = std::max(o, std::fabs(x[(size_t)i])); }

    RealFFT fft(N);
    AlignedBuffer in((size_t)N), out((size_t)N);
    std::vector<float> win((size_t)N), mag((size_t)N / 2 + 1);
    double wsum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double t = kTwoPi * i / N;
        win[(size_t)i] = (float)(0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t));
        wsum += win[(size_t)i];
    }
    const double magScale = 2.0 / wsum, binHz = sr / N;
    std::vector<float> rawF0((size_t)F, 0.0f), frameLv((size_t)F, 0.0f);
    std::vector<std::vector<float>> mags((size_t)F);
    // pass 1: spectra and a pitch per frame
    for (int f = 0; f < F; ++f)
    {
        const int c = f * hop - N / 2;
        double e = 0.0;
        for (int i = 0; i < N; ++i) { const int j = c + i; const float v = (j >= 0 && j < n) ? x[(size_t)j] : 0.0f; in[(size_t)i] = v * win[(size_t)i]; e += (double)v * v; }
        frameLv[(size_t)f] = (float)std::sqrt(e / N);
        fft.forward(in.data(), out.data());
        auto& m = mags[(size_t)f];
        m.resize((size_t)N / 2 + 1);
        for (int k = 0; k <= N / 2; ++k)
        {
            const float re = k == N / 2 ? out[1] : out[(size_t)(2 * k)], im = (k == 0 || k == N / 2) ? 0.0f : out[(size_t)(2 * k + 1)];
            m[(size_t)k] = std::sqrt(re * re + im * im) * (float)magScale;
        }
        // subharmonic summation: the f0 whose harmonics hold the most (log-compressed) energy
        auto at = [&](double hz) { const double b = hz / binHz; const int i = (int)b; if (i < 1 || i >= N / 2 - 1) return 0.0;
            return (double)std::max(m[(size_t)i], std::max(m[(size_t)i + 1], m[(size_t)i - 1])); };
        double best = -1.0, bf = 0.0;
        for (double cand = 40.0; cand < 1600.0; cand *= std::exp2(1.0 / 48.0))
        {
            double s = 0.0;
            for (int h = 1; h <= 12; ++h) { const double hz = cand * h; if (hz > sr * 0.45) break; s += std::log1p(at(hz) * 1000.0) * std::pow(0.88, h - 1); }
            if (s > best) { best = s; bf = cand; }
        }
        // an octave too low wins when its odd harmonics are empty: prefer the octave up if it explains as much
        for (int it = 0; it < 2; ++it)
        {
            const double up = bf * 2.0;
            if (up > 1600.0) break;
            double s1 = 0.0, s2 = 0.0;
            for (int h = 1; h <= 8; ++h) { s1 += std::log1p(at(bf * (2 * h - 1)) * 1000.0); s2 += std::log1p(at(up * h) * 1000.0); }
            if (s1 < 0.35 * s2) bf = up; else break;
        }
        rawF0[(size_t)f] = (float)bf;
    }
    // smooth the pitch track (a 7-frame median), and quiet frames take the overall pitch
    std::vector<float> sorted;
    for (int f = 0; f < F; ++f) if (frameLv[(size_t)f] > 1e-4f) sorted.push_back(rawF0[(size_t)f]);
    std::sort(sorted.begin(), sorted.end());
    A->f0med = sorted.empty() ? 220.0 : sorted[sorted.size() / 2];
    for (int f = 0; f < F; ++f)
    {
        float w[7]; int c = 0;
        for (int d = -3; d <= 3; ++d) { const int g = clampv(f + d, 0, F - 1); w[c++] = frameLv[(size_t)g] > 1e-4f ? rawF0[(size_t)g] : (float)A->f0med; }
        std::sort(w, w + 7);
        A->f0[(size_t)f] = w[3];
    }
    // pass 2: the harmonics and the noise
    for (int f = 0; f < F; ++f)
    {
        const auto& m = mags[(size_t)f];
        const double f0 = A->f0[(size_t)f];
        std::vector<char> taken((size_t)N / 2 + 1, 0);
        double tot = 0.0;
        for (int k = 1; k <= kK; ++k)
        {
            const double fk = k * f0;
            if (fk > sr * 0.45) break;
            const int lo = std::max(1, (int)((fk - 0.35 * f0) / binHz)), hi = std::min(N / 2 - 1, (int)((fk + 0.35 * f0) / binHz) + 1);
            int pb = -1; float pm = 0.0f;
            for (int b = lo; b <= hi; ++b) if (m[(size_t)b] > pm && m[(size_t)b] >= m[(size_t)b - 1] && m[(size_t)b] >= m[(size_t)b + 1]) { pm = m[(size_t)b]; pb = b; }
            if (pb < 0) continue;
            const double a = std::log(m[(size_t)pb - 1] + 1e-12f), b0 = std::log(m[(size_t)pb] + 1e-12f), c2 = std::log(m[(size_t)pb + 1] + 1e-12f);
            const double den = a - 2.0 * b0 + c2, d = std::fabs(den) > 1e-12 ? clampv(0.5 * (a - c2) / den, -0.5, 0.5) : 0.0;
            const double peakHz = (pb + d) * binHz, peakAmp = std::exp(b0 - 0.25 * (a - c2) * d);
            A->amp[(size_t)f * kK + k - 1] = (float)peakAmp;
            A->ratio[(size_t)f * kK + k - 1] = (float)(peakHz / k);   // for now the frequency / k; made a ratio below
            tot += peakAmp * peakAmp;
            for (int q = pb - 3; q <= pb + 3; ++q) if (q >= 0 && q <= N / 2) taken[(size_t)q] = 1;
        }
        // the pitch, refined from the harmonics themselves (their frequency / k, weighted by level), then each
        // harmonic's own detuning against it
        {
            double ws = 0.0, wf = 0.0;
            for (int k = 1; k <= std::min(kK, 16); ++k) { const double a = A->amp[(size_t)f * kK + k - 1]; if (a <= 0.0) continue; const double w = a * a * k; ws += w; wf += w * A->ratio[(size_t)f * kK + k - 1]; }
            const double fr = ws > 0.0 ? wf / ws : f0;
            if (std::fabs(fr / f0 - 1.0) < 0.03) A->f0[(size_t)f] = (float)fr;
            const double ff = A->f0[(size_t)f];
            for (int k = 1; k <= kK; ++k)
            {
                float& r = A->ratio[(size_t)f * kK + k - 1];
                r = A->amp[(size_t)f * kK + k - 1] > 0.0f ? (float)clampv(r / ff, 0.8, 1.25) : 1.0f;
            }
        }
        A->energy[(size_t)f] = (float)std::sqrt(tot);
        A->maxAmp = std::max(A->maxAmp, A->energy[(size_t)f]);
        for (int b = 0; b < kBands; ++b)
        {
            const int lo = std::max(1, (int)(bandLo(b) / binHz)), hi = std::min(N / 2, (int)(bandHi(b) / binHz));
            double e = 0.0; int c = 0;
            for (int q = lo; q < hi; ++q) if (!taken[(size_t)q]) { e += (double)m[(size_t)q] * m[(size_t)q]; ++c; }
            // the level a white noise of this density would need: per-bin magnitude -> per-band amplitude
            A->noise[(size_t)f * kBands + b] = c > 0 ? (float)std::sqrt(e / c * (hi - lo) / (N * 0.5) * 1.5) : 0.0f;
        }
    }
    // the overall pitch, from the refined track
    {
        std::vector<float> v;
        for (int f = 0; f < F; ++f) if (frameLv[(size_t)f] > 1e-4f) v.push_back(A->f0[(size_t)f]);
        if (!v.empty()) { std::sort(v.begin(), v.end()); A->f0med = v[v.size() / 2]; }
    }
    return A;
}

}} // namespace pump::prism
