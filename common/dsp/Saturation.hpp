// pump DSP: saturation curves and a dither/noise-shaping quantiser.
#pragma once

#include "Util.hpp"

namespace pump {

// Fast tanh (Pade 7/6 with clamp), accurate to ~1e-4 in [-4.97, 4.97] and saturating beyond.
inline float fastTanh(float x)
{
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
}

enum class SatMode : int { Warm = 0, Tape, Tube, Triode, Retro, Clip, Count };

inline const char* satModeName(SatMode m)
{
    switch (m)
    {
    case SatMode::Warm: return "Warm";
    case SatMode::Tape: return "Tape";
    case SatMode::Tube: return "Tube";
    case SatMode::Triode: return "Triode";
    case SatMode::Retro: return "Retro";
    case SatMode::Clip: return "Clip";
    default: return "?";
    }
}

// Stateless curve for a mode (input already scaled by drive). Output is roughly unity gain for small signals.
inline float satCurve(SatMode m, float x)
{
    switch (m)
    {
    case SatMode::Warm:   // gentle symmetric, rounded (odd harmonics, falling fast)
        return fastTanh(x);
    case SatMode::Tape: { // softer knee than tanh, asymmetric hint
        const float y = x / std::sqrt(1.0f + x * x * 0.75f);
        return 1.1547f * (y + 0.04f * y * y) / (1.0f + 0.04f);
    }
    case SatMode::Tube: { // asymmetric: even harmonics; positive side compresses earlier
        const float y = x >= 0.0f ? fastTanh(x * 1.15f) / 1.15f : fastTanh(x * 0.85f) / 0.85f;
        return y - 0.08f * y * y + 0.08f * 0.25f;   // remove the DC the square term adds at small levels (approx.)
    }
    case SatMode::Triode: { // Koren-like grid conduction: hard-ish on positive swings, soft on negative
        if (x > 0.0f) return x / (1.0f + 0.6f * x + 0.35f * x * x);
        const float y = fastTanh(x * 0.9f) / 0.9f;
        return y;
    }
    case SatMode::Retro: {  // transformer-ish: cubic soft clip with a little crossover hardness
        const float c = clampv(x, -1.5f, 1.5f);
        return c - (4.0f / 27.0f) * c * c * c;
    }
    case SatMode::Clip:     // hard clip at 0 dBFS-ish
        return clampv(x, -1.0f, 1.0f);
    default:
        return x;
    }
}

// Tape mode adds a gentle drive-dependent high-frequency loss and a slow "bias" state.
struct TapeState {
    float lp = 0.0f;
    float coef = 0.0f;
    void setup(double sr, float drive01) { coef = (float)std::exp(-kTwoPi * (16000.0 - 9000.0 * drive01) / sr); }
    inline float process(float x) { lp = x + (lp - x) * coef; return lp; }
    void reset() { lp = 0.0f; }
};

// TPDF dither with optional noise shaping, for quantising to 16 or 24 bits on output.
class Ditherer {
public:
    enum Mode { Off = 0, TPDF, Shaped };
    void setup(int bits, Mode m)
    {
        mode = m;
        q = (float)std::ldexp(1.0, -(clampv(bits, 8, 24) - 1));
        for (auto& e : err) std::fill(std::begin(e), std::end(e), 0.0f);
    }
    inline float process(int ch, float x)
    {
        if (mode == Off) return x;
        // Lipshitz "E-weighted" 9-tap shaping filter (44.1 kHz design; fine at 48 kHz)
        static constexpr float h[9] = { 2.412f, -3.370f, 3.937f, -4.174f, 3.353f, -2.205f, 1.281f, -0.569f, 0.0847f };
        float shaped = x;
        if (mode == Shaped)
            for (int k = 0; k < 9; ++k) shaped -= h[k] * err[ch][k];
        const float d = (rnd() - rnd()) * q;   // TPDF, +-1 LSB
        float y = std::round((shaped + d) / q) * q;
        y = clampv(y, -1.0f, 1.0f - q);
        if (mode == Shaped)
        {
            for (int k = 8; k > 0; --k) err[ch][k] = err[ch][k - 1];
            err[ch][0] = clampv(y - shaped, -4.0f * q, 4.0f * q);
        }
        return y;
    }
private:
    inline float rnd()
    {
        seed = seed * 1664525u + 1013904223u;
        return (float)(seed >> 8) * (1.0f / 16777216.0f);
    }
    Mode mode = Off;
    float q = 1.0f / 32768.0f;
    float err[2][9] = {};
    uint32_t seed = 0x12345u;
};

} // namespace pump
