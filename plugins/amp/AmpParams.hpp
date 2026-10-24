// pump AMP: parameters and factory presets (shared by the DSP and the UI). The model and the cab impulse response
// are files, saved with the session as states (not parameters), so presets never change them.
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace amp {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kInput = 0, kGate, kNormalize, kChannels, kBass, kMid, kTreble, kPresence,
    kCab, kCabLowCut, kCabHighCut, kOutGain, kBypass,
    kNumParams
};

enum Channels : int { kMono = 0, kStereo, kNumChannelModes };
static const char* const kChannelNames[] = { "Mono", "Stereo" };

constexpr float kGateOff = -100.0f, kLowCutOff = 10.0f, kHighCutOff = 22000.0f;
inline bool gateActive(double t) { return t > kGateOff + 0.5; }
inline bool lowCutActive(double f) { return f > kLowCutOff + 0.05; }
inline bool highCutActive(double f) { return f < kHighCutOff - 10.0; }
constexpr double kTargetLoudness = -18.0;   // NORMALIZE brings every model's "typical" output to this (dB)

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    v.push_back({ "input", "Input Gain", "Input", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "gate", "Gate Threshold", "Gate", kGateOff, -20, kGateOff, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "normalize", "Normalize", "Normalize", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "channels", "Channels", "Channels", 0, kNumChannelModes - 1, kMono, PUnit::None, PScale::Linear, kPInteger, kChannelNames, 1 });
    v.push_back({ "bass", "Bass", "Bass", -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mid", "Mid", "Mid", -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "treble", "Treble", "Treble", -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "presence", "Presence", "Presence", -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "cab", "Cabinet", "Cab", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "cab_low_cut", "Cab Low Cut", "Low Cut", kLowCutOff, 500, kLowCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "cab_high_cut", "Cab High Cut", "High Cut", 1000, kHighCutOff, kHighCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
}

// Factory presets: input, gate and tone shapes that suit most models (the model and cab stay as they are).
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    F("Default", {});
    F("Tight high gain", { { "gate", -62 }, { "bass", -2 }, { "mid", 1.5f }, { "cab_low_cut", 80 } });
    F("Scooped", { { "bass", 3 }, { "mid", -6 }, { "treble", 3 } });
    F("Mid push", { { "mid", 5 }, { "bass", -1.5f }, { "presence", -1.5f } });
    F("Bright and open", { { "treble", 3 }, { "presence", 4 } });
    F("Dark and smooth", { { "treble", -4 }, { "presence", -5 }, { "cab_high_cut", 6500 } });
    F("Hotter input (+6 dB)", { { "input", 6 } });
    F("Cleaner input (-6 dB)", { { "input", -6 } });
    S("Guitar: rhythm (double-tracked)", { { "gate", -65 }, { "mid", 1 }, { "cab_low_cut", 90 }, { "cab_high_cut", 9000 } });
    S("Guitar: lead", { { "mid", 3 }, { "presence", 1.5f }, { "cab_low_cut", 100 } });
    S("Guitar: stereo amp", { { "channels", 1 } });
    S("Bass: amp", { { "bass", 2 }, { "treble", -2 }, { "presence", -3 } });
    S("Synths and keys: amp colour", { { "input", -6 }, { "cab_high_cut", 12000 } });
    S("Drums: smashed", { { "input", 6 }, { "gate", -55 }, { "cab_low_cut", 60 } });
    return v;
}

}} // namespace pump::amp
