// pump VERB: reverb building blocks (candidates for common/dsp/Fx.hpp). Header-only, framework-free.
//
//   nearestPrime        delay lengths that share no factor (their echoes rarely coincide)
//   hadamard / householder   in-place orthogonal feedback mixing (fast Walsh-Hadamard: N log N adds)
//   Absorber            a delay line's three-band absorption: two first-order shelves (TPT one-poles) whose gains at
//                       DC, mid and Nyquist give the line exactly the RT60s asked for each band (absorberMag = its
//                       magnitude response, for displays)
//   rtGain              the gain that makes a delay of L samples decay at a given RT60
//   Converter           a gain-ranging (floating) converter: `bits` relative to a range chosen in 6 dB steps by the
//                       signal's envelope, so its grain follows the level (0 ranges = a plain fixed-point converter)
//   truncMantissa       keeps `bits` of a float's mantissa (truncating toward zero: relative, level-following
//                       quantisation that never adds energy, so a feedback loop stays stable and never limit-cycles)
//   ImagePattern        early reflections of a shoebox room: first- and second-order image sources at two
//                       receivers (L / R), times relative to the first reflection, energy-normalised
//   HeadDelay           a stereo integer delay whose time changes by crossfading to a new read head (no pitch glide)
//   AlignedPitchShifter a delay-line pitch shifter whose read head hands over to a second one at the lag that best
//                       correlates with it (copied from ECHO's EchoDsp.hpp): a fixed two-head shifter cancels tones
//                       whenever its heads land half a period apart, which in a shimmer loop eats the octave
#pragma once

#include "dsp/Fx.hpp"
#include <complex>
#include <cstring>

namespace pump { namespace verb {

inline bool isPrime(int n)
{
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (int d = 3; d * d <= n; d += 2)
        if (n % d == 0) return false;
    return true;
}
// the prime nearest to n (searching outward; ties go up)
inline int nearestPrime(int n)
{
    if (n <= 2) return 2;
    for (int k = 0; k < n; ++k)
    {
        if (isPrime(n + k)) return n + k;
        if (isPrime(n - k)) return n - k;
    }
    return n;
}

// Orthonormal Hadamard mix of N = 8 or 16 values (fast Walsh-Hadamard transform, scaled by 1/sqrt(N)).
inline void hadamard(float* x, int n)
{
    for (int h = 1; h < n; h <<= 1)
        for (int i = 0; i < n; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[j], b = x[j + h];
                x[j] = a + b; x[j + h] = a - b;
            }
    const float s = n == 16 ? 0.25f : (n == 8 ? 0.35355339f : 1.0f / std::sqrt((float)n));
    for (int i = 0; i < n; ++i) x[i] *= s;
}
// Householder reflection I - 2/N 11^T: each line keeps most of itself (1 - 2/N), so echoes build up more slowly
// and stay more distinct (the ringier, sparser sound of a small machine).
inline void householder(float* x, int n)
{
    float s = 0.0f;
    for (int i = 0; i < n; ++i) s += x[i];
    s *= 2.0f / (float)n;
    for (int i = 0; i < n; ++i) x[i] -= s;
}

// The gain per pass for a delay of `samples` to decay 60 dB in `rt60` seconds.
inline double rtGain(double samples, double rt60, double sr)
{
    return std::exp(-6.907755278982137 * samples / (std::max(1e-3, rt60) * sr));
}

// Three-band absorption: a low shelf at f1 (ratio rL below it) then a high shelf at f2 (ratio rH above it), times gM.
// DC gain = gM * rL, Nyquist gain = gM * rH, and the magnitude between them is monotonic per shelf, so it never
// exceeds the larger end gain (stable in a lossy loop).
struct AbsorberCoefs {
    float G1 = 0.5f, G2 = 0.5f;   // TPT one-pole coefficients at the crossover and the damping frequency
    void set(double f1, double f2, double sr)
    {
        const double g1 = std::tan(kPi * clampv(f1, 5.0, 0.49 * sr) / sr), g2 = std::tan(kPi * clampv(f2, 5.0, 0.49 * sr) / sr);
        G1 = (float)(g1 / (1.0 + g1));
        G2 = (float)(g2 / (1.0 + g2));
    }
};
struct Absorber {
    float s1 = 0.0f, s2 = 0.0f;
    float gM = 1.0f, rL = 1.0f, rH = 1.0f;
    void reset() { s1 = s2 = 0.0f; }
    inline float process(float x, const AbsorberCoefs& c)
    {
        float v = (x - s1) * c.G1;
        const float lo = v + s1;
        s1 = lo + v;
        const float y = x + (rL - 1.0f) * lo;
        v = (y - s2) * c.G2;
        const float lo2 = v + s2;
        s2 = lo2 + v;
        return gM * (lo2 + rH * (y - lo2));
    }
    // the gains for a delay of L samples: RT60 low / mid / high
    void design(double L, double rtLow, double rtMid, double rtHigh, double sr)
    {
        const double m = rtGain(L, rtMid, sr);
        gM = (float)m;
        rL = (float)(rtGain(L, rtLow, sr) / m);
        rH = (float)(rtGain(L, rtHigh, sr) / m);
    }
};
// |H(f)| of an Absorber designed as above (bilinear one-poles, prewarped at their corners)
inline double absorberMag(double f, double f1, double f2, double gM, double rL, double rH, double sr)
{
    using C = std::complex<double>;
    const C z1 = std::polar(1.0, -kTwoPi * clampv(f, 1.0, 0.4999 * sr) / sr);
    auto lp = [&](double fc) {
        const double g = std::tan(kPi * clampv(fc, 5.0, 0.49 * sr) / sr);
        return g * (1.0 + z1) / ((1.0 + g) + (g - 1.0) * z1);
    };
    const C h1 = 1.0 + (rL - 1.0) * lp(f1);
    const C l2 = lp(f2);
    const C h2 = l2 + rH * (1.0 - l2);
    return std::abs(gM * h1 * h2);
}

// Gain-ranging converter. The range (0..maxRange doublings of gain ahead of a `bits` converter) follows a peak
// envelope (instant attack, ~50 ms release), so quiet passages get finer steps: the noise floats with the level.
struct Converter {
    float env = 0.0f, rel = 0.9996f;
    void setup(double sr) { rel = (float)std::exp(-1.0 / (0.05 * sr)); env = 0.0f; }
    void reset() { env = 0.0f; }
    // q = 2^(bits-1)
    inline float process(float x, float q, int maxRange)
    {
        const float a = std::fabs(x);
        env = a > env ? a : env * rel;
        int e = 0;
        std::frexp(std::max(env, 1e-9f), &e);
        const int r = clampv(-e, 0, maxRange);
        const float s = q * (float)(1 << r);
        return std::floor(x * s + 0.5f) / s;
    }
};

// Keep `bits` mantissa bits (mask from mantissaMask), truncating toward zero.
inline uint32_t mantissaMask(int bits) { bits = clampv(bits, 1, 23); return ~((1u << (23 - bits)) - 1u); }
inline float truncMantissa(float x, uint32_t mask)
{
    uint32_t u;
    std::memcpy(&u, &x, 4);
    u &= mask;
    std::memcpy(&x, &u, 4);
    return x;
}

// Early reflections of a shoebox room (dimensions in metres, wall reflectance 0..1). The source sits off-centre near
// one end, the two receivers (L / R) 2 x 0.6 m apart further down the room. Taps are sorted by time, the first
// reflection (of either side) at t = 0; each side's energy sums to 1.
struct ErTap { float t = 0.0f, g = 0.0f; };
struct ImagePattern {
    static constexpr int kMax = 24;
    ErTap tap[2][kMax];
    int n[2] = { 0, 0 };
    float span = 0.0f;   // seconds to the last tap
    void build(double lx, double ly, double lz, double refl, uint32_t seed)
    {
        const double sx = 0.28 * lx, sy = 0.42 * ly, sz = 0.45 * lz;
        const double rx = 0.66 * lx, rz = 0.40 * lz, half = std::min(0.6, 0.2 * ly);
        const double ry[2] = { 0.5 * ly - half, 0.5 * ly + half };
        fx::Rng rng(seed);
        auto img = [](int k, double L, double s) { return (k % 2 == 0) ? k * L + s : (k + 1) * L - s; };
        double tMin = 1e9;
        for (int side = 0; side < 2; ++side)
        {
            n[side] = 0;
            const double direct = std::sqrt((rx - sx) * (rx - sx) + (ry[side] - sy) * (ry[side] - sy) + (rz - sz) * (rz - sz));
            for (int kx = -2; kx <= 2; ++kx)
                for (int ky = -2; ky <= 2; ++ky)
                    for (int kz = -2; kz <= 2; ++kz)
                    {
                        const int order = std::abs(kx) + std::abs(ky) + std::abs(kz);
                        if (order < 1 || order > 2 || n[side] >= kMax) continue;
                        const double x = img(kx, lx, sx) - rx, y = img(ky, ly, sy) - ry[side], z = img(kz, lz, sz) - rz;
                        const double d = std::sqrt(x * x + y * y + z * z);
                        // a little scattering: +-3 % in time, +-1.5 dB in level
                        const double t = (d - direct) / 343.0 * (1.0 + 0.03 * rng.bi());
                        const double g = std::pow(refl, order) * direct / d * std::pow(10.0, 0.075 * rng.bi());
                        tap[side][n[side]].t = (float)std::max(0.0, t);
                        tap[side][n[side]].g = (float)(rng.uni() < 0.5 ? -g : g);
                        ++n[side];
                        tMin = std::min(tMin, std::max(0.0, t));
                    }
        }
        span = 0.0f;
        for (int side = 0; side < 2; ++side)
        {
            double e = 0.0;
            for (int k = 0; k < n[side]; ++k) { tap[side][k].t -= (float)tMin; e += (double)tap[side][k].g * tap[side][k].g; }
            std::sort(tap[side], tap[side] + n[side], [](const ErTap& a, const ErTap& b) { return a.t < b.t; });
            const float s = (float)(1.0 / std::sqrt(std::max(1e-12, e)));
            for (int k = 0; k < n[side]; ++k) tap[side][k].g *= s;
            if (n[side] > 0) span = std::max(span, tap[side][n[side] - 1].t);
        }
    }
};

// Stereo integer delay; a new time crossfades from the old read head to the new one (raised cosine). A change that
// arrives during a fade waits for it to finish.
class HeadDelay {
public:
    void setMax(int samples, int fadeSamples)
    {
        for (auto& l : line) l.setMax(samples + 2);
        maxD = samples;
        fadeLen = std::max(1, fadeSamples);
        fadePos = fadeLen;
    }
    void clear() { for (auto& l : line) l.clear(); }
    void reset(int delay) { cur = next = target = clampv(delay, 0, maxD); fadePos = fadeLen; }
    void setTarget(int delay) { target = clampv(delay, 0, maxD); }
    int current() const { return cur; }
    inline void process(float xl, float xr, float& yl, float& yr)
    {
        line[0].push(xl); line[1].push(xr);
        if (fadePos >= fadeLen && target != cur) { next = target; fadePos = 0; }
        yl = line[0].at(cur); yr = line[1].at(cur);
        if (fadePos < fadeLen)
        {
            const float t = 0.5f - 0.5f * std::cos((float)kPi * ((float)fadePos + 0.5f) / (float)fadeLen);
            yl += (line[0].at(next) - yl) * t;
            yr += (line[1].at(next) - yr) * t;
            if (++fadePos == fadeLen) cur = next;
        }
    }
private:
    fx::FracDelay line[2];
    int cur = 0, next = 0, target = 0, maxD = 0, fadeLen = 1, fadePos = 1;
};

// One sounding head sweeps the line at the pitch ratio; near the end of its range it hands over (sin^2 crossfade up
// to 15 ms) to a second head about 40 ms away, the jump chosen within +-12 ms to maximise the correlation around the
// two heads, so they are in phase when they cross. Mono.
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

}} // namespace pump::verb
