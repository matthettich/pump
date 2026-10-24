// pump DSP: pitch tracking and musical scales. Shared by VOX (its built-in carrier can follow the voice) and the
// pitch correction plugin. Header-only, framework-free (compiles to wasm like the rest of common/dsp).
//
//   Scale tables   twelve-bit masks per scale (bit n = n semitones above the key), names, nearest in-scale note,
//                  stepping by scale degrees (diatonic harmonies), note names
//   Tracker        a YIN fundamental-frequency tracker (de Cheveigné & Kawahara, 2002) on a decimated copy of the
//                  input: one estimate per hop (about 5 ms) with a confidence (1 - the aperiodicity at the chosen
//                  lag) and a voiced / unvoiced decision. Octave jumps against a stable previous estimate are only
//                  accepted when the new lag is clearly better.
#pragma once

#include "Util.hpp"
#include <cstdint>
#include <cstdio>
#include <vector>

namespace pump { namespace pitch {

// ---- scales ------------------------------------------------------------------------------------------------------------
enum ScaleId : int {
    kChromatic = 0, kMajor, kMinor, kHarmonicMinor, kMelodicMinor, kDorian, kPhrygian, kLydian, kMixolydian,
    kLocrian, kMajorPentatonic, kMinorPentatonic, kBlues, kWholeTone, kNumScales
};
static const char* const kScaleNames[] = {
    "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
    "Locrian", "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone"
};
static const uint16_t kScaleMasks[kNumScales] = {
    0xFFF, 0xAB5, 0x5AD, 0x9AD, 0xAAD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x295, 0x4A9, 0x4E9, 0x555
};
static const char* const kKeyNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

inline int pitchClass(int note) { return ((note % 12) + 12) % 12; }
inline bool inScale(int note, int key, uint16_t mask) { return (mask >> pitchClass(note - key)) & 1u; }

// The in-scale note nearest to `midi` (fractional). Equidistant neighbours go to the one the pitch is moving
// toward... which we don't know here, so to the lower one; callers that track a held note add hysteresis.
inline int nearestInScale(double midi, int key, uint16_t mask)
{
    if ((mask & 0xFFFu) == 0) mask = 0xFFF;
    const int c = (int)std::floor(midi + 0.5);
    for (int d = 0; d <= 6; ++d)
    {
        const bool dn = inScale(c - d, key, mask), up = inScale(c + d, key, mask);
        if (dn && up) return std::fabs(midi - (c - d)) <= std::fabs((c + d) - midi) ? c - d : c + d;
        if (dn) return c - d;
        if (up) return c + d;
    }
    return c;
}

// Like nearestInScale, but keeps `held` until the pitch is more than `hyst` semitones past the midpoint toward
// another note (a voice wavering between two notes doesn't flip the target back and forth).
inline int nearestWithHold(double midi, int key, uint16_t mask, int held, double hyst)
{
    const int n = nearestInScale(midi, key, mask);
    if (held < 0 || n == held || !inScale(held, key, mask)) return n;
    // the boundary between `held` and the candidate lies halfway between them
    const double mid = 0.5 * (n + held);
    if (n > held ? midi > mid + hyst : midi < mid - hyst) return n;
    return held;
}

// `steps` scale degrees above (or below) an in-scale `note` (chromatic: semitones).
inline int scaleStep(int note, int steps, int key, uint16_t mask)
{
    if ((mask & 0xFFFu) == 0) mask = 0xFFF;
    int n = note;
    const int dir = steps >= 0 ? 1 : -1;
    for (int s = 0; s != steps; s += dir)
    {
        int k = 0;
        do { n += dir; } while (!inScale(n, key, mask) && ++k < 12);
    }
    return n;
}

inline double midiToHz(double m) { return 440.0 * std::exp2((m - 69.0) / 12.0); }
inline double hzToMidi(double hz) { return 69.0 + 12.0 * std::log2(std::max(hz, 1e-6) / 440.0); }
// "A4", "C#3"
inline const char* noteName(int note, char* buf, int len)
{
    std::snprintf(buf, (size_t)len, "%s%d", kKeyNames[pitchClass(note)], clampv((int)std::floor(note / 12.0) - 1, -9, 99));
    return buf;
}

// ---- tracker -----------------------------------------------------------------------------------------------------------
// YIN on a copy decimated to about `analysisRate` (a sixth-order low pass first). The window holds the longest period
// (minHz) twice. Each hop: the difference function d(tau), its cumulative-mean-normalised form d'(tau), the first dip
// under the threshold (or the deepest one), parabolic refinement, and a voiced decision from the depth of the dip and
// the window's level. Cost at the defaults (60..1000 Hz at ~12 kHz, 5.3 ms hops): about 8 M multiply-adds per second.
class Tracker {
public:
    struct Result { double hz = 0.0; float confidence = 0.0f; bool voiced = false; float levelDb = -120.0f; };

    void prepare(double sampleRate, double minHz = 60.0, double maxHz = 1000.0, double analysisRate = 12000.0,
                 double hopSeconds = 0.0053)
    {
        sr = sampleRate;
        decim = std::max(1, (int)std::lround(sr / analysisRate));
        ar = sr / decim;
        minLag = std::max(2, (int)std::floor(ar / maxHz));
        maxLag = std::max(minLag + 4, (int)std::ceil(ar / minHz) + 1);
        W = maxLag;
        len = W + maxLag + 2;
        buf.assign((size_t)len * 2, 0.0f);
        d.assign((size_t)maxLag + 2, 0.0f);
        dn.assign((size_t)maxLag + 2, 1.0f);
        hop = std::max(8, (int)std::lround(hopSeconds * ar));
        // sixth-order Butterworth low pass at 0.8 x the analysis Nyquist (three sections), and a 40 Hz high pass
        const double fc = std::min(0.4 * ar, 0.45 * sr);
        static const double qs[3] = { 0.5176380902, 0.7071067812, 1.9318516526 };
        for (int s = 0; s < 3; ++s) designLowpass(lp[s], fc, qs[s]);
        hpR = std::exp(-kTwoPi * 40.0 / sr);
        reset();
    }
    void reset()
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
        for (auto& s : lp) s.z1 = s.z2 = 0.0;
        hpX = hpY = 0.0;
        w = 0; phase = 0; sinceHop = 0;
        last = Result();
        prevLag = 0.0;
        stable = 0;
    }
    void setThreshold(double t) { thresh = clampv(t, 0.02, 0.6); }
    void setMinLevel(double db) { minLevelDb = db; }

    // Feed one sample; returns true when a new estimate is ready (result()).
    inline bool push(float x)
    {
        double v = x;
        // DC / rumble high pass
        const double y = v - hpX + hpR * hpY; hpX = v; hpY = y; v = y;
        for (auto& s : lp) v = s.run(v);
        if (++phase < decim) return false;
        phase = 0;
        buf[(size_t)w] = (float)v;
        buf[(size_t)(w + len)] = (float)v;   // mirrored, so a window is always contiguous
        w = (w + 1) % len;
        if (++sinceHop < hop) return false;
        sinceHop = 0;
        analyse();
        return true;
    }
    const Result& result() const { return last; }
    // how far behind the newest input an estimate's centre lies (samples at the host rate)
    int latencySamples() const { return (int)((0.5 * (W + maxLag)) * decim); }
    double analysisRate() const { return ar; }
    int hopSamples() const { return hop * decim; }

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    void designLowpass(Biquad& b, double fc, double q) const
    {
        const double wc = kTwoPi * clampv(fc, 10.0, 0.49 * sr) / sr, c = std::cos(wc), al = std::sin(wc) / (2.0 * q), a0 = 1.0 + al;
        b.b0 = (1.0 - c) * 0.5 / a0; b.b1 = (1.0 - c) / a0; b.b2 = b.b0; b.a1 = -2.0 * c / a0; b.a2 = (1.0 - al) / a0;
    }

    void analyse()
    {
        // the newest W + maxLag samples, oldest first
        const float* x = buf.data() + w;   // buf[w .. w + len) is the ring in order (mirrored copy)
        const float* s = x + (len - (W + maxLag));
        double e = 0.0;
        for (int j = 0; j < W + maxLag; ++j) e += (double)s[j] * s[j];
        const float levelDb = (float)(10.0 * std::log10(e / (W + maxLag) + 1e-30));
        // difference function
        d[0] = 0.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double acc = 0.0;
            const float* a = s; const float* b = s + tau;
            for (int j = 0; j < W; ++j) { const double t = (double)a[j] - b[j]; acc += t * t; }
            d[(size_t)tau] = (float)acc;
        }
        // cumulative mean normalised difference
        double run = 0.0;
        dn[0] = 1.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            run += d[(size_t)tau];
            dn[(size_t)tau] = run > 1e-20 ? (float)(d[(size_t)tau] * tau / run) : 1.0f;
        }
        // first dip under the threshold (then down to its bottom), else the deepest
        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
            if (dn[(size_t)tau] < thresh)
            {
                while (tau + 1 < maxLag && dn[(size_t)(tau + 1)] < dn[(size_t)tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            best = minLag;
            for (int tau = minLag + 1; tau < maxLag; ++tau) if (dn[(size_t)tau] < dn[(size_t)best]) best = tau;
        }
        // keep a stable pitch through an octave slip unless the new lag is clearly better
        if (prevLag > 0.0 && stable >= 3)
        {
            const double r = best / prevLag;
            for (double f : { 0.5, 2.0 })
                if (std::fabs(r - f) < 0.06 * f)
                {
                    const int alt = (int)std::lround(prevLag);
                    int a = alt;
                    for (int k = std::max(minLag, alt - 2); k <= std::min(maxLag - 1, alt + 2); ++k) if (dn[(size_t)k] < dn[(size_t)a]) a = k;
                    if (dn[(size_t)a] < dn[(size_t)best] + 0.08f) best = a;
                }
        }
        // parabolic refinement
        double lag = best;
        if (best > 1 && best < maxLag)
        {
            const double y0 = dn[(size_t)(best - 1)], y1 = dn[(size_t)best], y2 = dn[(size_t)(best + 1)];
            const double den = y0 - 2.0 * y1 + y2;
            if (std::fabs(den) > 1e-12) lag = best + clampv(0.5 * (y0 - y2) / den, -0.5, 0.5);
        }
        Result r;
        r.levelDb = levelDb;
        r.confidence = (float)clampv(1.0 - (double)dn[(size_t)best], 0.0, 1.0);
        r.voiced = dn[(size_t)best] < voicedMax && levelDb > minLevelDb;
        r.hz = ar / lag;
        if (r.voiced)
        {
            stable = (prevLag > 0.0 && std::fabs(lag / prevLag - 1.0) < 0.06) ? stable + 1 : 0;
            prevLag = lag;
        }
        else stable = 0;
        last = r;
    }

    double sr = 48000.0, ar = 12000.0;
    int decim = 4, minLag = 12, maxLag = 200, W = 200, len = 402, hop = 64;
    std::vector<float> buf, d, dn;
    Biquad lp[3];
    double hpR = 0.995, hpX = 0.0, hpY = 0.0;
    int w = 0, phase = 0, sinceHop = 0;
    double thresh = 0.15, voicedMax = 0.35f, minLevelDb = -60.0;
    double prevLag = 0.0;
    int stable = 0;
    Result last;
};

}} // namespace pump::pitch
