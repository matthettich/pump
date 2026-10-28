// pump's modulation: the sources (two LFOs, velocity, note, mod wheel, aftertouch, random per note) and the pieces a
// plugin's connections are made of. A connection ("slot") is three parameters: a source, a target (one of the plugin's
// own continuous parameters, or an LFO's rate) and an amount (-100..100 % of the target's range). An instrument has a
// fixed pool of slots (sessions and automation keep their places); the window shows the ones in use and adds or
// removes them. Framework-free: used by the plugins and the web build alike.
#pragma once
#include "Util.hpp"
#include <cmath>
#include <cstdint>

namespace pump { namespace mod {

// an instrument's pool of connections (each three parameters; the window shows those in use)
constexpr int kSlots = 32;

enum Source : int { kSrcNone = 0, kSrcLfo1, kSrcLfo2, kSrcVelocity, kSrcNote, kSrcWheel, kSrcAftertouch, kSrcRandom, kNumSources };
static const char* const kSourceNames[kNumSources] = { "None", "LFO 1", "LFO 2", "Velocity", "Note", "Mod wheel", "Aftertouch", "Random" };
static const char* const kSourceKeys[kNumSources] = { "-", "LFO 1", "LFO 2", "VELOCITY", "NOTE", "WHEEL", "AFTERTOUCH", "RANDOM" };
// what a source means, for the window
static const char* const kSourceAbout[kNumSources] = {
    "", "", "",
    "How hard each note is played (0 soft .. 1 hard), per note",
    "Each note's pitch: C4 is 0, two octaves up or down is +-1, per note",
    "The mod wheel (MIDI CC 1), 0 .. 1",
    "Channel or key pressure, 0 .. 1",
    "A new random value (-1 .. 1) for each note"
};
// sources that differ from note to note
inline bool perVoice(int s) { return s == kSrcVelocity || s == kSrcNote || s == kSrcRandom; }
// sources that only go up (0..1); the others swing both ways (-1..1)
inline bool unipolar(int s) { return s == kSrcVelocity || s == kSrcWheel || s == kSrcAftertouch; }

enum Shape : int { kSine = 0, kTri, kSaw, kSquare, kSampleHold, kDrift, kNumShapes };
static const char* const kShapeNames[kNumShapes] = { "Sine", "Triangle", "Saw", "Square", "Sample and hold", "Drift" };
static const char* const kShapeKeys[kNumShapes] = { "SINE", "TRI", "SAW", "SQR", "S&H", "DRIFT" };

// An LFO: phase 0..1 in cycles. S&H holds a new random value each cycle; DRIFT glides smoothly through random points
// (Catmull-Rom), so it never has corners.
struct Lfo {
    double phase = 0.0;
    void reset(double p = 0.0) { phase = p - std::floor(p); }
    void seed(uint32_t s) { rng = s ? s : 1u; for (int k = 0; k < 4; ++k) next(); }
    // advance by dp cycles (a new random point each time a cycle ends)
    void advance(double dp)
    {
        phase += dp;
        while (phase >= 1.0) { phase -= 1.0; next(); }
        if (phase < 0.0) phase = 0.0;
    }
    // set the phase directly (locked to the song); a cycle boundary crossed still draws the next random point
    void lockTo(double p)
    {
        p -= std::floor(p);
        if (p < phase - 0.5) next();
        phase = p;
    }
    double value(int shape) const { return valueAt(shape, phase); }
    double valueAt(int shape, double u) const
    {
        u -= std::floor(u);
        switch (shape)
        {
        case kSine: return std::sin(kTwoPi * u);
        case kTri: return 1.0 - 4.0 * std::fabs(u - 0.5) ;
        case kSaw: return 2.0 * u - 1.0;
        case kSquare: return u < 0.5 ? 1.0 : -1.0;
        case kSampleHold: return p[2];
        default:
        {
            // Catmull-Rom through p[1] (u = 0) and p[2] (u = 1)
            const double a = p[0], b = p[1], c = p[2], d = p[3];
            const double u2 = u * u, u3 = u2 * u;
            const double v = 0.5 * (2.0 * b + (-a + c) * u + (2.0 * a - 5.0 * b + 4.0 * c - d) * u2 + (-a + 3.0 * b - 3.0 * c + d) * u3);
            return clampv(v, -1.0, 1.0);
        }
        }
    }

private:
    void next() { p[0] = p[1]; p[1] = p[2]; p[2] = p[3]; p[3] = rnd(); }
    double rnd() { rng = rng * 1664525u + 1013904223u; return (double)(rng >> 8) * (2.0 / 16777216.0) - 1.0; }
    uint32_t rng = 0x2545F491u;
    double p[4] = { 0.0, 0.0, 0.0, 0.0 };
};

}} // namespace pump::mod
