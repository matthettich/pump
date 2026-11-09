// pump STRETCH: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace stretch {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kStretch = 0, kWindow, kFreeze, kTone, kMix, kOutGain, kBypass,
    kNumParams
};

// The window sizes, in samples at 44.1 / 48 kHz (doubled from 88.2 kHz, quadrupled from 176.4 kHz so the times stay
// the same): 43 ms .. 0.68 s at 48 kHz.
constexpr int kNumWindows = 5;
constexpr int kWindowSamples[kNumWindows] = { 2048, 4096, 8192, 16384, 32768 };
static const char* const kWindowNames[] = { "~45 ms (grainy)", "~90 ms", "~0.18 s", "~0.35 s", "~0.7 s (smoothest)" };

// STRETCH is 0..1 along the speed range: the read head runs 1.5 x .. 50 x slower.
constexpr double kStretchMin = 1.5, kStretchMax = 50.0;
inline double stretchFactor(double amount) { return kStretchMin * std::pow(kStretchMax / kStretchMin, amount < 0.0 ? 0.0 : (amount > 1.0 ? 1.0 : amount)); }

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    v.push_back({ "stretch", "Stretch", "Stretch", 0, 1, 0.5f, PUnit::None, PScale::Linear, A, nullptr, 1 });   // 0..1 = x1.5 .. x50
    v.push_back({ "window", "Window", "Window", 0, kNumWindows - 1, 2, PUnit::None, PScale::Linear, A | kPInteger, kWindowNames, 1 });
    v.push_back({ "freeze", "Freeze", "Freeze", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "tone", "Tone", "Tone", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. Stretch is 0..1: 0.2 = x3, 0.4 = x6, 0.54 = x10, 0.74 = x20, 0.85 = x30, 1 = x50.
// Windows: 0 ~45 ms, 1 ~90 ms, 2 ~0.18 s, 3 ~0.35 s, 4 ~0.7 s.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    F("Default", {});
    F("Slow wash", { { "stretch", 0.54f }, { "window", 3 }, { "mix", 80 } });
    F("Glass pad", { { "stretch", 0.74f }, { "window", 4 }, { "tone", 35 }, { "mix", 100 } });
    F("Drone freeze", { { "stretch", 0.5f }, { "window", 4 }, { "freeze", 1 }, { "tone", -10 }, { "mix", 100 } });
    F("Dark bed", { { "stretch", 0.66f }, { "window", 3 }, { "tone", -55 }, { "mix", 60 } });
    F("Short grainy smear", { { "stretch", 0.2f }, { "window", 0 }, { "mix", 70 } });
    F("Half-speed haze", { { "stretch", 0.1f }, { "window", 2 }, { "mix", 50 } });
    F("Vocal cloud", { { "stretch", 0.5f }, { "window", 2 }, { "tone", 15 }, { "mix", 55 } });
    F("Bright shimmer smear", { { "stretch", 0.4f }, { "window", 1 }, { "tone", 60 }, { "mix", 45 } });
    F("Endless x50", { { "stretch", 1.0f }, { "window", 4 }, { "tone", -20 }, { "mix", 100 } });
    return v;
    }();
    return v;
}

}} // namespace pump::stretch
