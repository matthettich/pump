// pump SPRING: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace spring {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kModel = 0, kDwell, kDecay, kTension, kDrip, kTone, kLowCut, kPreDelay, kWobble, kWidth, kGrit,
    kMix, kOutGain, kBypass,
    kNumParams
};

enum Model : int { kTwoSpring = 0, kThreeSpring, kShortTank, kLongTank, kDigital, kLofi, kNumModels };
static const char* const kModelNames[] = { "Two-spring tank", "Three-spring tank", "Short tank", "Long tank", "Digital", "Lo-fi digital" };
inline bool isDigital(int model) { return model == kDigital || model == kLofi; }

// LOW CUT's bottom end stop means "off".
constexpr float kLowCutOff = 20.0f;
inline bool lowCutActive(double f) { return f > kLowCutOff + 0.05; }

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    // the model switch crossfades two voices (the old tank rings out while the new one takes the input): automatable
    v.push_back({ "model", "Model", "Model", 0, kNumModels - 1, kTwoSpring, PUnit::None, PScale::Linear, A | kPInteger, kModelNames, 1 });
    v.push_back({ "dwell", "Dwell", "Dwell", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "Decay", 0.3f, 8.0f, 2.5f, PUnit::Seconds, PScale::Log, A, nullptr, 1 });
    v.push_back({ "tension", "Tension", "Tension", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drip", "Drip", "Drip", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tone", "Tone", "Tone", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "low_cut", "Low Cut", "Low Cut", kLowCutOff, 1000, 100, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "pre_delay", "Pre-Delay", "Pre-Delay", 0, 250, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "wobble", "Wobble", "Wobble", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "width", "Width", "Width", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "grit", "Grit", "Grit", 0, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
}

// Factory presets. Models: 0 two-spring 1 three-spring 2 short 3 long 4 digital 5 lo-fi digital.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Classic two-spring", { { "model", 0 }, { "dwell", 55 }, { "decay", 2.8f }, { "drip", 55 }, { "mix", 35 } });
    F("Deep three-spring", { { "model", 1 }, { "decay", 3.5f }, { "tension", -15 }, { "drip", 45 }, { "width", 85 } });
    F("Short and boxy", { { "model", 2 }, { "decay", 1.4f }, { "tension", 25 }, { "drip", 60 }, { "low_cut", 160 } });
    F("Long and lush", { { "model", 3 }, { "decay", 5.0f }, { "tension", -20 }, { "drip", 40 }, { "wobble", 25 }, { "width", 90 } });
    F("Drip machine", { { "model", 0 }, { "dwell", 70 }, { "drip", 100 }, { "decay", 2.6f }, { "tension", -30 }, { "mix", 35 } });
    F("Tight and bright", { { "model", 1 }, { "tension", 70 }, { "tone", 30 }, { "decay", 1.8f } });
    F("Loose and dark", { { "model", 3 }, { "tension", -80 }, { "tone", -40 }, { "decay", 4.0f } });
    F("Hot dwell", { { "model", 0 }, { "dwell", 92 }, { "drip", 70 }, { "decay", 2.2f }, { "mix", 32 } });
    F("Early digital spring", { { "model", 4 }, { "grit", 20 }, { "decay", 2.0f } });
    F("Budget rack spring", { { "model", 4 }, { "grit", 55 }, { "decay", 1.6f }, { "drip", 40 } });
    F("Toy box spring", { { "model", 5 }, { "grit", 70 }, { "decay", 1.2f }, { "mix", 35 } });
    S("Guitar: surf drip", { { "model", 0 }, { "dwell", 80 }, { "drip", 90 }, { "decay", 3.0f }, { "tension", -20 }, { "mix", 40 }, { "low_cut", 150 } });
    S("Guitar: clean amp spring", { { "model", 0 }, { "dwell", 45 }, { "decay", 2.2f }, { "mix", 25 }, { "low_cut", 120 } });
    S("Guitar: three-spring wash", { { "model", 1 }, { "dwell", 60 }, { "decay", 4.0f }, { "wobble", 30 }, { "mix", 38 }, { "width", 90 } });
    S("Vocals: vintage spring", { { "model", 1 }, { "dwell", 40 }, { "drip", 35 }, { "decay", 1.8f }, { "low_cut", 220 }, { "pre_delay", 30 }, { "mix", 18 }, { "tone", -10 } });
    S("Vocals: slap spring", { { "model", 2 }, { "pre_delay", 90 }, { "decay", 1.2f }, { "drip", 40 }, { "low_cut", 250 }, { "mix", 22 } });
    S("Drums: snare spring", { { "model", 2 }, { "dwell", 65 }, { "drip", 70 }, { "decay", 1.5f }, { "low_cut", 250 }, { "mix", 25 } });
    S("Drums: dub splash", { { "model", 0 }, { "dwell", 85 }, { "drip", 85 }, { "decay", 4.0f }, { "low_cut", 300 }, { "wobble", 30 }, { "mix", 45 } });
    S("Bass: small tank", { { "model", 2 }, { "decay", 0.9f }, { "low_cut", 300 }, { "tone", -20 }, { "mix", 15 } });
    S("Keys: organ tank", { { "model", 1 }, { "decay", 2.5f }, { "drip", 30 }, { "mix", 28 } });
    S("Keys: electric piano spring", { { "model", 0 }, { "decay", 2.0f }, { "drip", 40 }, { "width", 80 }, { "mix", 25 } });
    S("Synths: wide springs", { { "model", 1 }, { "width", 100 }, { "wobble", 25 }, { "decay", 3.2f }, { "mix", 30 } });
    S("Bus: spring glue", { { "model", 1 }, { "dwell", 35 }, { "decay", 1.4f }, { "low_cut", 220 }, { "drip", 30 }, { "mix", 12 } });
    C("Crash and boing", { { "model", 3 }, { "decay", 6.0f }, { "dwell", 70 }, { "drip", 80 }, { "wobble", 40 }, { "mix", 40 } });
    C("Seasick springs", { { "model", 1 }, { "wobble", 100 }, { "decay", 4.0f }, { "tension", -40 }, { "mix", 35 } });
    C("Broken effects unit", { { "model", 5 }, { "grit", 100 }, { "decay", 2.5f }, { "wobble", 50 }, { "mix", 40 } });
    C("Endless boing", { { "model", 3 }, { "decay", 8.0f }, { "drip", 100 }, { "tension", -60 }, { "mix", 50 } });
    C("Pre-delayed drip", { { "model", 0 }, { "pre_delay", 180 }, { "drip", 80 }, { "decay", 3.0f }, { "mix", 35 } });
    return v;
}

}} // namespace pump::spring
