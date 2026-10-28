// pump ROOM: parameters and factory presets (shared by the DSP and the UI). The profile itself (the correction made
// with the pump ROOM app) is a state, saved with the session; presets never change it.
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace roomfx {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kAmount = 0, kPhase, kTimeAlign, kOutGain, kBypass,
    kNumParams
};

enum Phase : int { kMinimum = 0, kLinear, kNumPhases };
static const char* const kPhaseNames[] = { "Minimum", "Linear" };

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    v.push_back({ "amount", "Amount", "Amount", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    // phase and time alignment change the latency or the timing: not automatable
    v.push_back({ "phase", "Phase", "Phase", 0, kNumPhases - 1, kMinimum, PUnit::None, PScale::Linear, kPInteger, kPhaseNames, 1 });
    v.push_back({ "time_align", "Time Alignment", "Align", 0, 1, 1, PUnit::None, PScale::Linear, kPBoolean, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    F("Default", {});
    F("Half the correction", { { "amount", 50 } });
    F("Mixing (linear phase)", { { "phase", (float)kLinear } });
    F("Tracking (no extra latency)", { { "phase", (float)kMinimum } });
    F("Correction only (no time alignment)", { { "time_align", 0 } });
    return v;
    }();
    return v;
}

}} // namespace pump::roomfx
