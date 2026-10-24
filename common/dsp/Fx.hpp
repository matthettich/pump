// pump DSP: building blocks for the creative effects (TUBE, SNAP, GATE, ECHO, SPRING, VERB, GRAIN, CRUSH). Header-only,
// framework-free (compiles to wasm like the rest of common/dsp).
//
//   Rng            xorshift32 noise source (deterministic per seed: tests and renders repeat exactly)
//   FracDelay      mono ring delay with cubic (Hermite) or linear fractional reads; any number of taps
//   Allpass        Schroeder allpass on a FracDelay (modulatable delay; the diffusers of the reverbs)
//   OnePole        TPT one-pole low/high pass (stable while the cutoff moves)
//   Svf            TPT state-variable filter (Simper): low, band, high at once; also shelf/peak helpers
//   DcBlocker      first-order DC blocker
//   Lfo            sine / triangle / square / ramp, with phase
//   Wander         smooth random modulation (a new random target every period, cosine-interpolated)
//   Glide          one-pole exponential smoother for continuously moving controls (delay times, pitch)
//   Crusher        sample-rate reduction (zero-order hold at a fractional rate) and bit reduction
//   PitchShifter   delay-line pitch shifter with correlation-aligned head splices (shimmer, pitched echoes)
//   equalPower     equal-power crossfade gains
#pragma once

#include "Util.hpp"
#include "Saturation.hpp"

namespace pump { namespace fx {

// ---- noise ---------------------------------------------------------------------------------------------------------
struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
    void seed(uint32_t v) { s = v ? v : 1u; }
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline double uni() { return (next() >> 8) * (1.0 / 16777216.0); }          // [0, 1)
    inline double bi() { return 2.0 * uni() - 1.0; }                              // [-1, 1)
    inline float bif() { return (float)bi(); }
};

// ---- delay lines -----------------------------------------------------------------------------------------------------
// push() writes the newest sample; read(d) then returns the input d samples ago (read(0) = the newest). Cubic reads
// need one sample on each side, so they clamp d to [1, max]; linear reads work down to 0.
class FracDelay {
public:
    void setMax(int samples)
    {
        int size = 4;
        while (size < samples + 8) size <<= 1;
        buf.assign((size_t)size, 0.0f);
        mask = size - 1;
        w = 0;
        maxD = samples;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    int maxDelay() const { return maxD; }
    inline void push(float x) { buf[(size_t)w] = x; w = (w + 1) & mask; }
    inline float at(int d) const { return buf[(size_t)((w - 1 - d) & mask)]; }   // d >= 0, integer
    inline float readLinear(double d) const
    {
        d = clampv(d, 0.0, (double)maxD);
        const int i = (int)d;
        const float f = (float)(d - i);
        const float a = at(i), b = at(i + 1);
        return a + (b - a) * f;
    }
    inline float read(double d) const
    {
        d = clampv(d, 1.0, (double)maxD);
        const int i = (int)d;
        const float f = (float)(d - i);
        const float ym1 = at(i - 1), y0 = at(i), y1 = at(i + 1), y2 = at(i + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }
private:
    std::vector<float> buf { 0, 0, 0, 0 };
    int mask = 3, w = 0, maxD = 0;
};

// Schroeder allpass: w = x + g w[n-D], y = w[n-D] - g w. Unity magnitude at every frequency for |g| < 1; D can be
// fractional and move (modulated diffusers).
class Allpass {
public:
    void setMax(int samples) { d.setMax(samples + 2); }
    void clear() { d.clear(); }
    inline float process(float x, double delay, float g)
    {
        const float wd = d.read(std::max(1.0, delay - 1.0));   // w[n - D] before w[n] is written
        const float wn = x + g * wd;
        d.push(wn);
        return wd - g * wn;
    }
    // integer-delay version (cheaper; for fixed diffusers)
    inline float processInt(float x, int delay, float g)
    {
        const float wd = d.at(std::max(0, delay - 1));
        const float wn = x + g * wd;
        d.push(wn);
        return wd - g * wn;
    }
    const FracDelay& line() const { return d; }
private:
    FracDelay d;
};

// ---- filters -----------------------------------------------------------------------------------------------------------
// TPT one-pole (zero-delay feedback): lp() and hp() share the state, so call one of them per sample.
struct OnePole {
    double G = 0.5, s = 0.0;
    void set(double fc, double sr)
    {
        const double g = std::tan(kPi * clampv(fc, 1.0, 0.49 * sr) / sr);
        G = g / (1.0 + g);
    }
    void reset(double v = 0.0) { s = v; }
    inline double lp(double x) { const double v = (x - s) * G; const double y = v + s; s = y + v; return y; }
    inline double hp(double x) { return x - lp(x); }
};

// TPT state-variable filter (A. Simper). Stays well-behaved under fast modulation.
struct Svf {
    double g = 0.1, k = 1.41421356, a1 = 0.0, a2 = 0.0, a3 = 0.0, ic1 = 0.0, ic2 = 0.0;
    void set(double freq, double q, double sr)
    {
        g = std::tan(kPi * clampv(freq, 5.0, 0.49 * sr) / sr);
        k = 1.0 / std::max(q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.0; }
    inline void process(double v0, double& lp, double& bp, double& hp)
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    inline double lowpass(double x) { double l, b, h; process(x, l, b, h); return l; }
    inline double highpass(double x) { double l, b, h; process(x, l, b, h); return h; }
    inline double bandpass(double x) { double l, b, h; process(x, l, b, h); return b * k; }   // 0 dB at the centre
    // peaking EQ around the cutoff: gain (linear) applied to the band
    inline double peak(double x, double gain) { double l, b, h; process(x, l, b, h); return x + (gain - 1.0) * k * b; }
    // low shelf below / high shelf above the cutoff (gain linear; a gentle 12 dB/oct shelf)
    inline double lowShelf(double x, double gain) { double l, b, h; process(x, l, b, h); return x + (gain - 1.0) * l; }
    inline double highShelf(double x, double gain) { double l, b, h; process(x, l, b, h); return x + (gain - 1.0) * h; }
};

struct DcBlocker {
    double R = 0.9995, x1 = 0.0, y1 = 0.0;
    void set(double fc, double sr) { R = std::exp(-kTwoPi * fc / sr); }
    void reset() { x1 = y1 = 0.0; }
    inline double process(double x) { const double y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
};

// ---- modulation ----------------------------------------------------------------------------------------------------------
struct Lfo {
    double phase = 0.0, inc = 0.0;
    void setRate(double hz, double sr) { inc = hz / sr; }
    void reset(double p = 0.0) { phase = p - std::floor(p); }
    inline void advance(int n = 1) { phase += inc * n; phase -= std::floor(phase); }
    inline double sine() const { return std::sin(kTwoPi * phase); }
    inline double tri() const { return 1.0 - 4.0 * std::fabs(phase - 0.5); }   // -1 at 0, +1 at 0.5
    inline double ramp() const { return 2.0 * phase - 1.0; }
    inline double square() const { return phase < 0.5 ? 1.0 : -1.0; }
};

// Smooth random wandering in [-1, 1]: picks a new target every 1/rate seconds (period jittered +-30 %) and glides to
// it on a raised-cosine curve, so the motion has no corners (no zipper, no pitch steps in modulated delays).
struct Wander {
    Rng rng;
    double from = 0.0, to = 0.0, t = 1.0, step = 0.001, rate = 1.0, sr = 48000.0;
    void setup(double sampleRate, uint32_t seed) { sr = sampleRate; rng.seed(seed); from = to = 0.0; t = 1.0; }
    void setRate(double hz) { rate = std::max(0.001, hz); }
    inline double next()
    {
        t += step;
        if (t >= 1.0)
        {
            from = to; to = rng.bi(); t = 0.0;
            step = rate * (0.7 + 0.6 * rng.uni()) / sr;
        }
        const double c = 0.5 - 0.5 * std::cos(kPi * t);
        return from + (to - from) * c;
    }
    // advance n samples at once (control-rate use) and return the value
    inline double advance(int n) { double v = 0.0; for (int i = 0; i < n; ++i) v = next(); return v; }
};

// One-pole exponential glide toward a moving target (time constant in seconds).
struct Glide {
    double y = 0.0, c = 0.0;
    void setTime(double seconds, double sr) { c = tauCoef(seconds, sr); }
    void reset(double v) { y = v; }
    inline double next(double target) { y = target + (y - target) * c; return y; }
};

// ---- lo-fi -------------------------------------------------------------------------------------------------------------------
// Zero-order hold at `rate` x the host rate (rate <= 1) and quantisation to `bits` (0 = off). Holding gives the stepped,
// aliasing sound of a low-rate converter; quantisation the grain of a short word.
struct Crusher {
    double phase = 1.0, held = 0.0, ratio = 1.0, q = 0.0;
    void set(double rateRatio, double bits) { ratio = clampv(rateRatio, 0.001, 1.0); q = bits > 0.5 ? std::ldexp(1.0, (int)std::lround(bits) - 1) : 0.0; }
    void reset() { phase = 1.0; held = 0.0; }
    inline double process(double x)
    {
        phase += ratio;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            held = q > 0.0 ? std::round(x * q) / q : x;
        }
        return held;
    }
};

// ---- pitch ---------------------------------------------------------------------------------------------------------------------
// Delay-line pitch shifter with one sounding read head that sweeps through the line at the pitch ratio. When the
// head nears the end of its range it hands over to a second head about 40 ms away, over a sin^2 crossfade of up to
// 15 ms. The jump is chosen within +-12 ms (periods down to ~42 Hz) to maximise the correlation around the two heads,
// so they are in phase when they cross: a fixed-window two-head shifter cancels whenever its heads land half a period
// apart (a 220 Hz tone an octave up came out as 430 + 450 Hz with a hole at 440). Tones stay whole; noisy material
// still gets a smooth crossfade. The search runs once per splice (coarse, then refined). Mono; one per channel.
class PitchShifter {
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
    FracDelay line;
    double sr = 48000.0, J = 1920.0, S = 576.0, Xmax = 720.0, ratio = 1.0, maxD = 8192.0;
    double d[2] = { 1000.0, 1000.0 };
    int m = 0, xf = 1, xfLen = 1, Lc = 480, lagStride = 2, sampleStride = 4;
};

inline double semitonesToRatio(double st) { return std::pow(2.0, st / 12.0); }

// equal-power crossfade: t = 0 -> (1, 0), t = 1 -> (0, 1)
inline void equalPower(double t, double& a, double& b)
{
    t = clampv(t, 0.0, 1.0);
    a = std::cos(0.5 * kPi * t);
    b = std::sin(0.5 * kPi * t);
}

// Sum of squares of a stereo block (for meters and level matching).
inline double energy(const float* L, const float* R, int n)
{
    double e = 0.0;
    for (int i = 0; i < n; ++i) e += (double)L[i] * L[i] + (double)R[i] * R[i];
    return e;
}

}} // namespace pump::fx
