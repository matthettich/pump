// pump TUBE: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace tube {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kMode = 0, kDrive, kBias, kSag, kVoltage, kStages, kFocus, kLowCut, kTone, kHighCut,
    kMix, kOutGain, kAutoGain, kOversampling, kBypass,
    kNumParams
};

enum Mode : int { kGlow = 0, kTriode, kPentode, kPushPull, kStarve, kFuzz, kNumModes };
static const char* const kModeNames[] = { "Glow", "Triode", "Pentode", "Push-Pull", "Starve", "Fuzz" };
enum Focus : int { kFocusFull = 0, kFocusLow, kFocusMid, kFocusHigh, kNumFocus };
static const char* const kFocusNames[] = { "Full", "Low", "Mid", "High" };
static const char* const kOsNames[] = { "1x", "2x", "4x", "8x" };
static const char* const kStageNames[] = { "1", "2", "3" };

// Filter end stops mean "off".
constexpr float kLowCutOff = 10.0f, kHighCutOff = 22000.0f;
inline bool lowCutActive(double f) { return f > kLowCutOff + 0.05; }
inline bool highCutActive(double f) { return f < kHighCutOff - 10.0; }

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    // stages and oversampling change the signal path (and the latency): not automatable
    v.push_back({ "mode", "Circuit", "Circuit", 0, kNumModes - 1, kTriode, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 36, 12, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bias", "Bias", "Bias", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sag", "Sag", "Sag", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voltage", "Voltage", "Voltage", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "stages", "Stages", "Stages", 1, 3, 1, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "focus", "Focus", "Focus", 0, kNumFocus - 1, 0, PUnit::None, PScale::Linear, A | kPInteger, kFocusNames, 1 });
    v.push_back({ "low_cut", "Low Cut", "Low Cut", kLowCutOff, 1000, kLowCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "tone", "Tone", "Tone", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "high_cut", "High Cut", "High Cut", 1000, kHighCutOff, kHighCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "auto_gain", "Auto Gain", "Auto Gain", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "oversampling", "Oversampling", "Oversampling", 0, 3, 2, PUnit::None, PScale::Linear, kPInteger, kOsNames, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. Circuits: 0 glow 1 triode 2 pentode 3 push-pull 4 starve 5 fuzz; focus: 0 full 1 low 2 mid 3 high.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Warm glow", { { "mode", 0 }, { "drive", 8 }, { "sag", 15 }, { "mix", 100 } });
    F("Preamp push", { { "mode", 1 }, { "drive", 16 }, { "bias", 15 }, { "sag", 30 } });
    F("Two-stage preamp", { { "mode", 1 }, { "drive", 14 }, { "stages", 2 }, { "sag", 35 }, { "low_cut", 60 }, { "high_cut", 12000 } });
    F("Pentode bite", { { "mode", 2 }, { "drive", 18 }, { "focus", 2 }, { "sag", 20 } });
    F("Power amp (hot bias)", { { "mode", 3 }, { "drive", 16 }, { "bias", 60 }, { "sag", 45 } });
    F("Power amp (cold bias)", { { "mode", 3 }, { "drive", 12 }, { "bias", -70 }, { "sag", 40 } });
    F("Starved and sputtering", { { "mode", 4 }, { "drive", 20 }, { "voltage", 35 }, { "sag", 60 } });
    F("Fuzz wall", { { "mode", 5 }, { "drive", 26 }, { "stages", 2 }, { "low_cut", 80 }, { "tone", -20 } });
    F("Parallel grit (mix 30%)", { { "mode", 2 }, { "drive", 28 }, { "stages", 2 }, { "mix", 30 }, { "low_cut", 120 } });
    S("Drums: room smash", { { "mode", 1 }, { "drive", 22 }, { "stages", 2 }, { "sag", 70 }, { "mix", 45 }, { "low_cut", 40 } });
    S("Drums: kick thump", { { "mode", 0 }, { "drive", 14 }, { "focus", 1 }, { "sag", 40 }, { "high_cut", 9000 } });
    S("Drums: snare crack", { { "mode", 2 }, { "drive", 16 }, { "focus", 3 }, { "sag", 15 }, { "low_cut", 90 } });
    S("Bass: growl", { { "mode", 1 }, { "drive", 18 }, { "focus", 2 }, { "stages", 2 }, { "mix", 60 } });
    S("Bass: octave fuzz", { { "mode", 5 }, { "drive", 22 }, { "bias", 60 }, { "low_cut", 40 }, { "mix", 55 } });
    S("Vocals: gentle tube", { { "mode", 0 }, { "drive", 9 }, { "focus", 2 }, { "low_cut", 70 }, { "sag", 10 } });
    S("Vocals: radio grit", { { "mode", 2 }, { "drive", 20 }, { "focus", 2 }, { "low_cut", 300 }, { "high_cut", 5000 } });
    S("Guitar: crunch", { { "mode", 1 }, { "drive", 24 }, { "stages", 2 }, { "low_cut", 90 }, { "tone", 15 } });
    S("Guitar: big amp", { { "mode", 3 }, { "drive", 22 }, { "stages", 2 }, { "bias", 20 }, { "sag", 55 }, { "high_cut", 7000 } });
    S("Keys: electric piano bark", { { "mode", 1 }, { "drive", 15 }, { "focus", 2 }, { "sag", 35 }, { "bias", 25 } });
    S("Synths: thick lead", { { "mode", 2 }, { "drive", 22 }, { "stages", 2 }, { "tone", -10 } });
    S("Bus: glue warmth", { { "mode", 0 }, { "drive", 6 }, { "sag", 20 }, { "mix", 70 } });
    C("Dying battery", { { "mode", 5 }, { "drive", 24 }, { "bias", -80 }, { "voltage", 20 }, { "sag", 80 } });
    C("Broken amp", { { "mode", 4 }, { "drive", 30 }, { "voltage", 15 }, { "stages", 3 }, { "sag", 90 }, { "high_cut", 4000 } });
    C("Crossover crackle", { { "mode", 3 }, { "drive", 6 }, { "bias", -100 }, { "sag", 0 } });
    C("Telephone tube", { { "mode", 2 }, { "drive", 24 }, { "low_cut", 450 }, { "high_cut", 3200 }, { "focus", 2 } });
    C("Three-stage scream", { { "mode", 1 }, { "drive", 30 }, { "stages", 3 }, { "low_cut", 100 }, { "high_cut", 8000 }, { "tone", -15 } });
    return v;
    }();
    return v;
}

}} // namespace pump::tube
