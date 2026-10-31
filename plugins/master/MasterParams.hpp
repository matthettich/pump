// pump MASTER: parameters and factory presets (shared by the DSP and the UI; no framework code).
#pragma once

#include "PumpParams.hpp"
#include "../limit/LimitParams.hpp"   // the limiter's style names
#include <vector>

namespace pump { namespace master {

enum Param : int {
    kStyle = 0, kIntensity,
    kToneOn, kToneAmt, kDynOn, kDynAmt, kStereoOn, kStereoAmt, kLoudOn, kTarget, kCeiling,
    kCompare, kBypass, kLoudMode,
    // ADVANCED: offsets on top of the auto plan (0 = as auto set it) and the limiter's details
    kToneTrim0, kToneTrim1, kToneTrim2, kToneTrim3, kToneTrim4, kToneTrim5, kToneTrim6, kToneTrim7, kToneTrim8,
    kLiftTrim, kLiftAt, kRatioTrim, kGlueThrTrim, kGlueAttack, kGlueRelease,
    kMonoFreq, kLowMonoTrim, kWidthTrim,
    kGainTrim, kLimRelease, kLimStyle,
    // ADVANCED: eight free EQ bands after the tone stage (5 parameters each, band b at kBand0 + 5 b)
    kBand0, kNumParams = kBand0 + 8 * 5
};

constexpr int kNumUserBands = 8;
enum BandField : int { kBOn = 0, kBShape, kBFreq, kBGain, kBQ, kBFields };
inline int bandParam(int band, int field) { return kBand0 + band * kBFields + field; }
enum UserShape : int { kUBell = 0, kULowShelf, kUHighShelf, kULowCut, kUHighCut, kNumUserShapes };
static const char* const kUserShapeNames[] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" };

enum Style : int { kBalanced = 0, kWarm, kOpen, kPunch, kNumStyles };
static const char* const kStyleNames[] = { "Balanced", "Warm", "Open", "Punch" };
enum LoudMode : int { kGentle = 0, kToTarget, kNumLoudModes };
static const char* const kLoudModeNames[] = { "Gentle", "Target" };

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    v.push_back({ "style", "Style", "Style", 0, kNumStyles - 1, kBalanced, PUnit::None, PScale::Linear, A | kPInteger, kStyleNames, 1 });
    v.push_back({ "intensity", "Intensity", "Intensity", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tone_on", "Tone On", "Tone", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "tone_amount", "Tone Amount", "Tone Amt", 0, 200, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "dyn_on", "Dynamics On", "Dynamics", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "dyn_amount", "Dynamics Amount", "Dyn Amt", 0, 200, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "stereo_on", "Stereo On", "Stereo", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "stereo_amount", "Stereo Amount", "Stereo Amt", 0, 200, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "loud_on", "Loudness On", "Loudness", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "target", "Loudness Target", "Target", -20, -6, -12, PUnit::LU, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "ceiling", "True Peak Ceiling", "Ceiling", -3, 0, -1, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "compare", "Compare (level matched)", "Compare", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPNoUndo, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    v.push_back({ "loud_mode", "Loudness Mode", "Mode", 0, kNumLoudModes - 1, kGentle, PUnit::None, PScale::Linear, A | kPInteger, kLoudModeNames, 1 });
    // ADVANCED
    {
        static const char* const sym[9] = { "tone_trim_60", "tone_trim_125", "tone_trim_250", "tone_trim_500", "tone_trim_1k",
                                             "tone_trim_2k", "tone_trim_4k", "tone_trim_8k", "tone_trim_12k" };
        static const char* const nm[9] = { "Tone Trim 60 Hz", "Tone Trim 125 Hz", "Tone Trim 250 Hz", "Tone Trim 500 Hz", "Tone Trim 1 kHz",
                                           "Tone Trim 2 kHz", "Tone Trim 4 kHz", "Tone Trim 8 kHz", "Tone Trim 12 kHz" };
        static const char* const sh[9] = { "60", "125", "250", "500", "1k", "2k", "4k", "8k", "12k" };
        for (int i = 0; i < 9; ++i) v.push_back({ sym[i], nm[i], sh[i], -6, 6, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    }
    v.push_back({ "lift_trim", "Leveler Lift Trim", "Lift", -3, 3, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lift_at", "Leveler Starts At", "Lift At", -10, 10, 0, PUnit::LU, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "ratio_trim", "Glue Ratio Trim", "Ratio", -1, 3, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "glue_thr_trim", "Glue Threshold Trim", "Threshold", -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "glue_attack", "Glue Attack", "Attack", 1, 200, 20, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "glue_release", "Glue Release", "Release", 20, 2000, 250, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mono_freq", "Low Mono Below", "Mono Below", 40, 300, 120, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "low_mono_trim", "Low Mono Trim", "Low Mono", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "width_trim", "Width Trim", "Width", -3, 3, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "gain_trim", "Gain Trim", "Gain", -6, 6, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lim_release", "Limiter Release", "Release", 10, 1000, 100, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lim_style", "Limiter Style", "Lim Style", 0, limit::kNumStyles - 1, limit::kDynamic, PUnit::None, PScale::Linear, A | kPInteger, limit::kStyleNames, 1 });
    for (int b = 0; b < kNumUserBands; ++b)
    {
        // symbols, names and short names live as long as the table (static storage)
        static char text[kNumUserBands][5][3][32];
        static const float defF[kNumUserBands] = { 60, 150, 300, 600, 1200, 2500, 5000, 10000 };
        const char* field[5] = { "on", "shape", "freq", "gain", "q" };
        const char* fieldName[5] = { "On", "Shape", "Frequency", "Gain", "Q" };
        for (int f = 0; f < 5; ++f)
        {
            std::snprintf(text[b][f][0], 32, "band%d_%s", b + 1, field[f]);
            std::snprintf(text[b][f][1], 32, "Band %d %s", b + 1, fieldName[f]);
            std::snprintf(text[b][f][2], 32, "B%d %s", b + 1, fieldName[f]);
        }
        v.push_back({ text[b][0][0], text[b][0][1], text[b][0][2], 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
        v.push_back({ text[b][1][0], text[b][1][1], text[b][1][2], 0, kNumUserShapes - 1, kUBell, PUnit::None, PScale::Linear, A | kPInteger, kUserShapeNames, 1 });
        v.push_back({ text[b][2][0], text[b][2][1], text[b][2][2], 20, 20000, defF[b], PUnit::Hz, PScale::Log, A, nullptr, 1 });
        v.push_back({ text[b][3][0], text[b][3][1], text[b][3][2], -18, 18, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ text[b][4][0], text[b][4][1], text[b][4][2], 0.2f, 10, 1, PUnit::None, PScale::Log, A, nullptr, 1 });
    }
    return v;
    }();
    return v;
}

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    v.push_back({ "Default", {} });
    v.push_back({ "Light touch", { { "intensity", 25 } } });
    v.push_back({ "Medium", { { "intensity", 50 } } });
    v.push_back({ "Strong", { { "intensity", 80 } } });
    v.push_back({ "Warm", { { "style", kWarm } } });
    v.push_back({ "Open", { { "style", kOpen } } });
    v.push_back({ "Punch", { { "style", kPunch } } });
    v.push_back({ "Streaming -14", { { "target", -14 }, { "loud_mode", kToTarget } } });
    v.push_back({ "Loud -9", { { "target", -9 }, { "loud_mode", kToTarget }, { "intensity", 60 } } });
    v.push_back({ "Tone only", { { "dyn_on", 0 }, { "stereo_on", 0 }, { "loud_on", 0 } } });
    v.push_back({ "Loudness only", { { "tone_on", 0 }, { "dyn_on", 0 }, { "stereo_on", 0 } } });
    v.push_back({ "No dynamics", { { "dyn_on", 0 } } });
    return v;
    }();
    return v;
}

}} // namespace pump::master
