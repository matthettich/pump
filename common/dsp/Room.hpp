// pump DSP: room measurement (pump ROOM).
//
// Exponential sine sweeps, deconvolution of what the microphone heard into an impulse response, the arrival time of
// the direct sound, the magnitude response on a log-frequency grid with fractional-octave smoothing, and measurement
// microphone calibration files.
//
// Framework-free, header-only, no exceptions (it also compiles to WebAssembly: wasm/room_wasm.cpp).
//
// Conventions:
//  - A response is the transfer from the digital output to the digital input (what the sweep's samples became at the
//    microphone), in dB. 0 dB = the microphone's samples are as loud as the samples that were played.
//  - A calibration file lists the microphone's own deviation from flat (frequency, dB [, phase]); the corrected
//    response is the measured one minus the file's curve (the usual convention of measurement mic files).
//  - Arrival times are in samples from the start of the sweep in the output stream. They include the whole system
//    latency (driver, converters, browser), so only differences between channels measured in one run mean anything.
#pragma once

#include "FFT.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pump {
namespace room {

constexpr double kSpeedOfSound = 343.0;   // m/s, air at about 20 C

inline int nextPow2(int n)
{
    int p = 32;
    while (p < n && p < (1 << 30)) p <<= 1;
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------
// the sweep

struct SweepSpec {
    double sampleRate = 48000.0;
    double f1 = 10.0;          // start frequency (Hz)
    double f2 = 0.0;           // end frequency; 0 = 0.45 x the sample rate (21.6 kHz at 48 kHz)
    double seconds = 3.0;      // length of the sweep itself
    double levelDb = -12.0;    // peak level in dBFS

    double end() const { return f2 > 0.0 ? std::min(f2, 0.49 * sampleRate) : 0.45 * sampleRate; }
    // the band in which the response is trustworthy: an octave above the start (below it the fade-in takes over)
    // up to just under the end
    double validLow() const { return std::max(2.0 * f1, 15.0); }
    double validHigh() const { return std::min(end() / 1.06, 0.49 * sampleRate); }
};

// Exponential (logarithmic) sine sweep from f1 to f2: equal time per octave, pink spectrum. A half-Hann fade-in
// over the first half octave and a 5 ms fade-out keep both ends free of clicks. Starts and ends at zero.
inline std::vector<float> makeSweep(const SweepSpec& s)
{
    const double fs = s.sampleRate, f1 = std::max(1.0, s.f1), f2 = std::max(f1 * 2.0, s.end());
    const int n = std::max(64, (int)std::lround(s.seconds * fs));
    const double T = n / fs, R = std::log(f2 / f1);
    const double amp = std::pow(10.0, s.levelDb / 20.0);
    const double fadeIn = T * std::log(std::sqrt(2.0)) / R;            // time to sweep from f1 to f1 x sqrt(2)
    const int nIn = std::max(1, (int)std::lround(fadeIn * fs));
    const int nOut = std::max(1, std::min(n / 4, (int)std::lround(0.005 * fs)));
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double ph = 2.0 * kPi * f1 * T / R * (std::exp(t * R / T) - 1.0);
        double g = 1.0;
        if (i < nIn) g = 0.5 - 0.5 * std::cos(kPi * i / nIn);
        if (i >= n - nOut) g *= 0.5 - 0.5 * std::cos(kPi * (n - 1 - i) / nOut);
        x[(size_t)i] = (float)(amp * g * std::sin(ph));
    }
    return x;
}

// ---------------------------------------------------------------------------------------------------------------------
// deconvolution

// The impulse response that turned `sweep` into `rec` (rec = ir * sweep + noise), by spectral division. Inside the
// sweep's band [fLo, fHi] the division is exact (a regulariser a billionth of the sweep's peak power, far below its
// weakest in-band bin); outside it the regulariser rises to a hundredth over a third of an octave, so bins the sweep
// never reached go to zero instead of amplifying noise. Without a band (fs <= 0) one regulariser of 1e-5 is used.
// The result has nextPow2(nRec + nSweep) samples: index 0 is zero lag, the causal response follows; the sweep's
// harmonic distortion products land at negative lags, i.e. at the end of the buffer, out of the way.
// Returns false if the input is empty.
inline bool deconvolve(const float* rec, int nRec, const float* sweep, int nSweep, std::vector<float>& ir,
                       double fs = 0.0, double fLo = 0.0, double fHi = 0.0)
{
    if (rec == nullptr || sweep == nullptr || nRec <= 0 || nSweep <= 0)
        return false;
    const int N = nextPow2(nRec + nSweep);
    RealFFT fft(N);
    AlignedBuffer a((size_t)N), b((size_t)N), A((size_t)N), B((size_t)N);
    std::memcpy(a.data(), rec, sizeof(float) * (size_t)nRec);
    std::memcpy(b.data(), sweep, sizeof(float) * (size_t)nSweep);
    fft.forward(a.data(), A.data());
    fft.forward(b.data(), B.data());

    // |S|^2 per bin and its largest value (ordered layout: [re0, reNyq, re1, im1, ...])
    double maxP = std::max((double)B[0] * B[0], (double)B[1] * B[1]);
    for (int k = 1; k < N / 2; ++k)
        maxP = std::max(maxP, (double)B[2 * k] * B[2 * k] + (double)B[2 * k + 1] * B[2 * k + 1]);
    if (!(maxP > 0.0))
        return false;
    const bool banded = fs > 0.0 && fLo > 0.0 && fHi > fLo;
    const double binHz = banded ? fs / N : 0.0;
    auto lambdaAt = [&](int k) {
        if (!banded) return 1e-5 * maxP;
        const double f = std::max(k * binHz, 1e-3);
        // octaves outside the band (0 inside), eased over a third of an octave
        const double out = f < fLo ? std::log2(fLo / f) : (f > fHi ? std::log2(f / fHi) : 0.0);
        const double t = std::min(1.0, out * 3.0);
        const double w = 0.5 - 0.5 * std::cos(kPi * t);
        return maxP * (1e-9 + 1e-2 * w);
    };
    const double scale = 1.0 / N;   // pffft's inverse is unscaled

    A[0] = (float)(A[0] * B[0] / ((double)B[0] * B[0] + lambdaAt(0)));
    A[1] = (float)(A[1] * B[1] / ((double)B[1] * B[1] + lambdaAt(N / 2)));
    for (int k = 1; k < N / 2; ++k)
    {
        const double ar = A[2 * k], ai = A[2 * k + 1], br = B[2 * k], bi = B[2 * k + 1];
        const double d = br * br + bi * bi + lambdaAt(k);
        A[2 * k] = (float)((ar * br + ai * bi) / d);       // R x conj(S) / (|S|^2 + lambda)
        A[2 * k + 1] = (float)((ai * br - ar * bi) / d);
    }
    fft.inverse(A.data(), a.data());
    ir.assign((size_t)N, 0.0f);
    for (int i = 0; i < N; ++i)
        ir[(size_t)i] = (float)(a[(size_t)i] * scale);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// arrival of the direct sound

struct Arrival {
    double index = -1.0;    // sub-sample position of the direct sound's peak (samples)
    double peakIndex = -1;  // position of the largest peak (a strong reflection can beat the direct sound)
    float peak = 0.0f;      // |ir| at the largest peak
    float level = 0.0f;     // |ir| at the direct sound
    int polarity = 0;       // +1 / -1: the sign of the direct sound's peak
    bool valid() const { return index >= 0.0; }
};

// The value of a band-limited signal between its samples (Hann-windowed sinc, 32 samples each side).
inline double interpolateAt(const float* x, int n, double t)
{
    const int c = (int)std::floor(t), half = 32;
    double s = 0.0;
    for (int k = c - half + 1; k <= c + half; ++k)
    {
        if (k < 0 || k >= n) continue;
        const double d = t - k;
        const double sinc = std::fabs(d) < 1e-12 ? 1.0 : std::sin(kPi * d) / (kPi * d);
        const double w = 0.5 + 0.5 * std::cos(kPi * d / (half + 0.5));
        s += x[k] * sinc * w;
    }
    return s;
}

// The direct sound is the first peak that reaches half (-6 dB) of the largest one between `from` and `to`. Its
// position is found between samples on the band-limited waveform (1/64-sample steps, then a parabola), good to a
// few hundredths of a sample.
inline Arrival findArrival(const float* ir, int n, int from, int to)
{
    Arrival a;
    from = std::max(0, from);
    to = std::min(n, to);
    if (ir == nullptr || to - from < 3)
        return a;
    int imax = from;
    float m = 0.0f;
    for (int i = from; i < to; ++i)
        if (std::fabs(ir[i]) > m) { m = std::fabs(ir[i]); imax = i; }
    if (!(m > 0.0f))
        return a;
    int i = from;
    while (i < to && std::fabs(ir[i]) < 0.5f * m) ++i;
    while (i + 1 < to && std::fabs(ir[i + 1]) > std::fabs(ir[i])) ++i;   // climb to the top of that peak
    double pos = i;
    {
        const double sgn = ir[i] >= 0.0f ? 1.0 : -1.0;
        const int steps = 64;
        double best = -1e30;
        int bj = 0;
        double v[2 * 64 + 1];
        for (int j = -steps; j <= steps; ++j)
        {
            v[j + steps] = sgn * interpolateAt(ir, n, i + (double)j / steps);
            if (v[j + steps] > best) { best = v[j + steps]; bj = j; }
        }
        pos = i + (double)bj / steps;
        if (bj > -steps && bj < steps)
        {
            const double y0 = v[bj + steps - 1], y1 = v[bj + steps], y2 = v[bj + steps + 1];
            const double den = y0 - 2.0 * y1 + y2;
            if (den < 0.0) pos += std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5) / steps;
        }
    }
    a.index = pos;
    a.peakIndex = imax;
    a.peak = m;
    a.level = std::fabs(ir[i]);
    a.polarity = ir[i] >= 0.0f ? 1 : -1;
    return a;
}

// ---------------------------------------------------------------------------------------------------------------------
// magnitude response

// The power spectrum of a windowed stretch of an impulse response, kept so the response can be read at any
// smoothing without another FFT.
class Spectrum {
public:
    // Window: from `preMs` before the direct sound (a half-Hann rise over that time) to `lengthMs` after it, the
    // last fifth faded out with a half-Hann. Longer windows include more of the room (what you hear at the
    // listening position); short ones show the speaker with fewer reflections but lose the low end.
    bool compute(const float* ir, int n, double arrival, double sampleRate, double lengthMs = 500.0, double preMs = 2.0)
    {
        fs = sampleRate;
        P.clear(); cum.clear();
        if (ir == nullptr || n <= 0 || arrival < 0.0 || !(fs > 0.0))
            return false;
        const int pre = std::max(1, (int)std::lround(preMs * 0.001 * fs));
        const int post = std::max(16, (int)std::lround(lengthMs * 0.001 * fs));
        const int start = (int)std::floor(arrival) - pre;
        const int len = pre + post;
        N = std::min(1 << 22, nextPow2(std::max(len, 65536)));
        AlignedBuffer x((size_t)N), X((size_t)N);
        const int fadeOut = std::max(1, post / 5);
        for (int i = 0; i < len && i < N; ++i)
        {
            const int j = start + i;
            if (j < 0 || j >= n) continue;
            double w = 1.0;
            if (i < pre) w = 0.5 - 0.5 * std::cos(kPi * i / pre);
            const int fromEnd = len - 1 - i;
            if (fromEnd < fadeOut) w *= 0.5 - 0.5 * std::cos(kPi * fromEnd / fadeOut);
            x[(size_t)i] = (float)(ir[j] * w);
        }
        RealFFT fft(N);
        fft.forward(x.data(), X.data());
        const int K = N / 2 + 1;
        P.assign((size_t)K, 0.0);
        P[0] = (double)X[0] * X[0];
        P[(size_t)(K - 1)] = (double)X[1] * X[1];
        for (int k = 1; k < K - 1; ++k)
            P[(size_t)k] = (double)X[2 * k] * X[2 * k] + (double)X[2 * k + 1] * X[2 * k + 1];
        cum.assign((size_t)K + 1, 0.0);
        for (int k = 0; k < K; ++k)
            cum[(size_t)k + 1] = cum[(size_t)k] + P[(size_t)k];
        return true;
    }

    bool ready() const { return !P.empty(); }
    double sampleRate() const { return fs; }

    // Power at frequency f, averaged over a band `octaves` wide centred on it (0 = no smoothing: interpolated
    // between bins). Returned in dB.
    double dbAt(double f, double octaves) const
    {
        if (P.empty() || !(f > 0.0)) return -300.0;
        const double binHz = fs / N;
        const int K = (int)P.size();
        double p;
        const double lo = f * std::pow(2.0, -0.5 * octaves) / binHz, hi = f * std::pow(2.0, 0.5 * octaves) / binHz;
        if (octaves > 0.0 && hi - lo >= 1.0)
        {
            // average over the band, with fractional bins at both edges (continuous in f)
            const double a = std::clamp(lo, 0.0, (double)K - 1), b = std::clamp(hi, 0.0, (double)K - 1);
            p = (integral(b) - integral(a)) / std::max(b - a, 1e-9);
        }
        else
        {
            const double k = std::clamp(f / binHz, 0.0, (double)K - 1);
            const int k0 = std::min((int)k, K - 2);
            const double t = k - k0;
            p = P[(size_t)k0] * (1.0 - t) + P[(size_t)k0 + 1] * t;
        }
        return 10.0 * std::log10(std::max(p, 1e-30));
    }

private:
    // integral of the piecewise-constant power from bin -0.5 to bin x (bin k covers [k - 0.5, k + 0.5))
    double integral(double x) const
    {
        const int K = (int)P.size();
        const double y = std::clamp(x + 0.5, 0.0, (double)K);
        const int k = std::min((int)y, K - 1);
        return cum[(size_t)k] + P[(size_t)k] * (y - k);
    }

    double fs = 48000.0;
    int N = 0;
    std::vector<double> P, cum;
};

// A log-spaced frequency grid: `perOctave` points per octave from fLo to fHi (both included).
inline std::vector<double> logGrid(double fLo, double fHi, int perOctave)
{
    std::vector<double> g;
    if (!(fLo > 0.0) || !(fHi > fLo) || perOctave <= 0) return g;
    const int n = (int)std::ceil(std::log2(fHi / fLo) * perOctave);
    g.reserve((size_t)n + 1);
    for (int i = 0; i <= n; ++i)
        g.push_back(std::min(fHi, fLo * std::pow(2.0, (double)i / perOctave)));
    return g;
}

// ---------------------------------------------------------------------------------------------------------------------
// microphone calibration files

// A locale-proof number reader (strtod follows the C locale, which a host can set to a decimal comma).
// Reads [sign] digits [. digits] [e [sign] digits]; `decimalComma` also takes ',' as the decimal point.
inline bool readNumber(const char*& p, const char* end, double& out, bool decimalComma = false)
{
    const char* s = p;
    double sign = 1.0;
    if (s < end && (*s == '+' || *s == '-')) { if (*s == '-') sign = -1.0; ++s; }
    double v = 0.0;
    int digits = 0;
    while (s < end && *s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); ++s; ++digits; }
    if (s < end && (*s == '.' || (decimalComma && *s == ',')))
    {
        ++s;
        double scale = 0.1;
        while (s < end && *s >= '0' && *s <= '9') { v += (*s - '0') * scale; scale *= 0.1; ++s; ++digits; }
    }
    if (digits == 0) return false;
    if (s < end && (*s == 'e' || *s == 'E'))
    {
        const char* e = s + 1;
        int es = 1, ev = 0, ed = 0;
        if (e < end && (*e == '+' || *e == '-')) { if (*e == '-') es = -1; ++e; }
        while (e < end && *e >= '0' && *e <= '9' && ev < 400) { ev = ev * 10 + (*e - '0'); ++e; ++ed; }
        if (ed > 0) { v *= std::pow(10.0, es * ev); s = e; }
    }
    out = sign * v;
    p = s;
    return std::isfinite(out);
}

struct CalCurve {
    std::vector<double> f, db;
    double sensitivityDb = 0.0;   // "Sens Factor" when the file has one (not applied: levels here are relative)
    bool hasSensitivity = false;
    int skippedLines = 0;         // lines that looked like data but weren't usable

    bool empty() const { return f.size() < 2; }
    double lowest() const { return f.empty() ? 0.0 : f.front(); }
    double highest() const { return f.empty() ? 0.0 : f.back(); }

    // the mic's deviation at frequency x: linear in log-frequency between points, held flat past both ends
    double at(double x) const
    {
        if (f.size() < 2 || !(x > 0.0)) return f.size() == 1 ? db[0] : 0.0;
        if (x <= f.front()) return db.front();
        if (x >= f.back()) return db.back();
        const size_t i = (size_t)(std::upper_bound(f.begin(), f.end(), x) - f.begin());
        const double t = std::log(x / f[i - 1]) / std::log(f[i] / f[i - 1]);
        return db[i - 1] + t * (db[i] - db[i - 1]);
    }
};

enum CalError { kCalOk = 0, kCalEmpty = -1, kCalTooFew = -2, kCalTooLarge = -3 };

// Reads a calibration file: one point per line, "frequency  dB  [phase]", separated by spaces, tabs, commas or
// semicolons (with semicolons a decimal comma is accepted too). Header and comment lines are skipped, a
// "Sens Factor = x dB" line is noted. Points are sorted; duplicate frequencies are averaged. Needs at least 3 points.
inline int parseCal(const char* text, size_t len, CalCurve& out)
{
    out = CalCurve();
    if (text == nullptr || len == 0) return kCalEmpty;
    if (len > (size_t)8 << 20) return kCalTooLarge;
    std::vector<std::pair<double, double>> pts;
    const char* p = text;
    const char* end = text + len;
    while (p < end)
    {
        const char* eol = p;
        while (eol < end && *eol != '\n' && *eol != '\r') ++eol;
        const char* s = p;
        p = eol;
        while (p < end && (*p == '\n' || *p == '\r')) ++p;

        while (s < eol && (*s == ' ' || *s == '\t' || *s == '\xEF' || *s == '\xBB' || *s == '\xBF')) ++s;   // also a UTF-8 BOM
        if (s >= eol) continue;

        // "Sens Factor =-1.378dB, SERNO: 7000000" (and quoted variants)
        {
            std::string line(s, eol);
            std::string lower = line;
            for (char& c : lower) c = (char)std::tolower((unsigned char)c);
            const size_t k = lower.find("sens factor");
            if (k != std::string::npos)
            {
                const char* q = s + k + 11;
                while (q < eol && !(*q == '-' || *q == '+' || (*q >= '0' && *q <= '9'))) ++q;
                double v;
                if (readNumber(q, eol, v) && std::fabs(v) < 100.0) { out.sensitivityDb = v; out.hasSensitivity = true; }
                continue;
            }
        }
        const char c0 = *s;
        if (!(c0 == '+' || c0 == '-' || c0 == '.' || (c0 >= '0' && c0 <= '9')))
            continue;   // a header or comment ("*", "\"", "#", "//", words)

        const bool semis = std::find(s, eol, ';') != eol;
        const char* q = s;
        double v[2];
        int got = 0;
        bool bad = false;
        while (got < 2 && q < eol)
        {
            while (q < eol && (*q == ' ' || *q == '\t' || *q == ';' || (!semis && *q == ','))) ++q;
            if (q >= eol) break;
            if (!readNumber(q, eol, v[got], semis)) { bad = true; break; }
            ++got;
        }
        if (bad || got < 2 || !(v[0] > 0.0) || v[0] > 1e6 || std::fabs(v[1]) > 60.0)
        {
            ++out.skippedLines;
            continue;
        }
        pts.push_back({ v[0], v[1] });
        if (pts.size() > 200000) return kCalTooLarge;
    }
    if (pts.empty()) return kCalEmpty;
    std::stable_sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < pts.size();)
    {
        size_t j = i;
        double sum = 0.0;
        while (j < pts.size() && pts[j].first == pts[i].first) { sum += pts[j].second; ++j; }
        out.f.push_back(pts[i].first);
        out.db.push_back(sum / (double)(j - i));
        i = j;
    }
    if (out.f.size() < 3) { out.f.clear(); out.db.clear(); return kCalTooFew; }
    return (int)out.f.size();
}

// ---------------------------------------------------------------------------------------------------------------------
// levels of the recording (for the "is the level right?" checks)

inline double rmsDb(const float* x, int n)
{
    if (x == nullptr || n <= 0) return -300.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double)x[i] * x[i];
    return 10.0 * std::log10(std::max(s / n, 1e-30));
}

inline double peakDb(const float* x, int n)
{
    float m = 0.0f;
    for (int i = 0; i < n; ++i) m = std::max(m, std::fabs(x[i]));
    return 20.0 * std::log10(std::max((double)m, 1e-15));
}

// ---------------------------------------------------------------------------------------------------------------------
// one measured channel: the recording of its sweep turned into an impulse response, arrival and spectrum

struct Measurement {
    double sampleRate = 48000.0;
    std::vector<float> ir;       // causal part, from the sweep's start in the output stream
    Arrival arrival;
    Spectrum spectrum;
    double windowMs = 0.0;       // the spectrum's window (recomputed when it changes)
    double signalDb = -300.0;    // RMS of the recording while the sweep played (dBFS)
    double peakDbFS = -300.0;    // peak of the recording (dBFS)

    // rec: the recording from the moment the sweep started playing, `n` samples (the sweep plus a tail long enough
    // for the latency and the room's decay). `searchSamples`: how far into the response to look for the direct
    // sound (latency + distance; the rest of the tail is the room).
    bool analyse(const float* rec, int n, const std::vector<float>& sweep, const SweepSpec& spec, int searchSamples)
    {
        const double fs = spec.sampleRate;
        sampleRate = fs;
        std::vector<float> full;
        if (!deconvolve(rec, n, sweep.data(), (int)sweep.size(), full, fs, std::max(1.0, spec.f1), spec.end()))
            return false;
        // keep the causal part the recording can hold (its length minus the sweep: beyond that the response is
        // only partly captured)
        const int keep = std::max(1, std::min((int)full.size(), n - (int)sweep.size() + (int)(0.05 * fs)));
        ir.assign(full.begin(), full.begin() + keep);
        arrival = findArrival(ir.data(), (int)ir.size(), 0, std::min(searchSamples, (int)ir.size()));
        signalDb = rmsDb(rec, std::min(n, (int)sweep.size()));
        peakDbFS = peakDb(rec, n);
        windowMs = 0.0;
        return arrival.valid();
    }

    // A measurement read back from a file: its response, the arrival as it was found then.
    bool load(const float* x, int n, double fs, double arrivalIndex)
    {
        sampleRate = fs;
        if (x == nullptr || n <= 0) return false;
        ir.assign(x, x + n);
        if (arrivalIndex >= 0.0 && arrivalIndex < n)
        {
            const int i = (int)std::lround(arrivalIndex);
            arrival = findArrival(ir.data(), n, std::max(0, i - 2), std::min(n, i + 3));
            arrival.index = arrivalIndex;
        }
        else
            arrival = findArrival(ir.data(), n, 0, n);
        windowMs = 0.0;
        return arrival.valid();
    }

    bool ensureSpectrum(double lengthMs)
    {
        if (spectrum.ready() && windowMs == lengthMs) return true;
        windowMs = lengthMs;
        return spectrum.compute(ir.data(), (int)ir.size(), arrival.index, sampleRate, lengthMs);
    }

    // The response in dB on `freqs` (cal: the mic's file, subtracted; may be null).
    void response(const std::vector<double>& freqs, double smoothOct, const CalCurve* cal, std::vector<float>& out)
    {
        out.assign(freqs.size(), -300.0f);
        if (!spectrum.ready()) return;
        for (size_t i = 0; i < freqs.size(); ++i)
        {
            double d = spectrum.dbAt(freqs[i], smoothOct);
            if (cal != nullptr && !cal->empty()) d -= cal->at(freqs[i]);
            out[i] = (float)d;
        }
    }
};

} // namespace room
} // namespace pump
