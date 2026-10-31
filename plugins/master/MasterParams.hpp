// pump MASTER: parameters and factory presets (shared by the DSP and the UI; no framework code).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace master {

enum Param : int {
    kStyle = 0, kIntensity,
    kToneOn, kToneAmt, kDynOn, kDynAmt, kStereoOn, kStereoAmt, kLoudOn, kTarget, kCeiling,
    kCompare, kBypass, kLoudMode,
    kNumParams
};

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
