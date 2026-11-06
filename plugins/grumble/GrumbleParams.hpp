// pump GRUMBLE: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace grumble {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kBeastOn = 0, kBeastPitch, kBeastFormant, kBeastFry, kBeastLevel, kBeastPan,
    kSplitOn, kSplitPitch, kSplitFormant, kSplitWobble, kSplitLevel, kSplitPan,
    kGrowlOn, kGrowlPitch, kGrowlRough, kGrowlBreath, kGrowlSize, kGrowlLevel, kGrowlPan,
    kSwarmOn, kSwarmPitch, kSwarmSpread, kSwarmDensity, kSwarmGrain, kSwarmLevel, kSwarmWidth,
    kMorphX, kMorphY,
    kGate, kDrive, kTone, kCave, kMix, kOutGain, kBypass,
    kNumParams
};

// The four layers, in morph-pad corner order: BEAST top left, SPLIT top right, GROWL bottom left, SWARM bottom right.
enum Layer : int { kBeast = 0, kSplit, kGrowl, kSwarm, kNumLayers };
static const char* const kLayerNames[] = { "BEAST", "SPLIT", "GROWL", "SWARM" };
// each layer's on switch, level and pan (SWARM's "pan" is its width)
static const int kLayerOn[kNumLayers] = { kBeastOn, kSplitOn, kGrowlOn, kSwarmOn };
static const int kLayerLevel[kNumLayers] = { kBeastLevel, kSplitLevel, kGrowlLevel, kSwarmLevel };

constexpr float kGateOff = -90.0f;
inline bool gateActive(double db) { return db > kGateOff + 0.05; }

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, B = kPAutomatable | kPBoolean;
    v.push_back({ "beast_on", "Beast On", "Beast", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "beast_pitch", "Beast Pitch", "Pitch", -24, 12, -7, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "beast_formant", "Beast Formant", "Formant", -12, 12, -5, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "beast_fry", "Beast Fry", "Fry", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "beast_level", "Beast Level", "Level", 0, 100, 80, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "beast_pan", "Beast Pan", "Pan", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });

    v.push_back({ "split_on", "Split On", "Split", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "split_pitch", "Split Pitch", "Pitch", -24, 24, 5, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "split_formant", "Split Formant", "Formant", -12, 12, 4, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "split_wobble", "Split Wobble", "Wobble", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "split_level", "Split Level", "Level", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "split_pan", "Split Pan", "Pan", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });

    v.push_back({ "growl_on", "Growl On", "Growl", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "growl_pitch", "Growl Pitch", "Pitch", -24, 12, -12, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "growl_rough", "Growl Rough", "Rough", 0, 100, 55, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "growl_breath", "Growl Breath", "Breath", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "growl_size", "Growl Size", "Size", -12, 12, -4, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "growl_level", "Growl Level", "Level", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "growl_pan", "Growl Pan", "Pan", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });

    v.push_back({ "swarm_on", "Swarm On", "Swarm", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "swarm_pitch", "Swarm Pitch", "Pitch", -12, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "swarm_spread", "Swarm Spread", "Spread", 0, 24, 7, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "swarm_density", "Swarm Density", "Density", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "swarm_grain", "Swarm Grain", "Grain", 5, 150, 30, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "swarm_level", "Swarm Level", "Level", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "swarm_width", "Swarm Width", "Width", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });

    v.push_back({ "morph_x", "Morph X", "Morph X", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "morph_y", "Morph Y", "Morph Y", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });

    v.push_back({ "gate", "Gate", "Gate", kGateOff, -20, -60, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 24, 4, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tone", "Tone", "Tone", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "cave", "Cave", "Cave", 0, 100, 12, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. Each one sets every layer switch, so picking one never leaves a layer from the last.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto L = [](float b, float s, float g, float w) {
        return std::vector<PresetValue>{ { "beast_on", b }, { "split_on", s }, { "growl_on", g }, { "swarm_on", w } };
    };
    auto add = [&](const char* section, const char* n, std::vector<PresetValue> on, std::vector<PresetValue> p) {
        on.insert(on.end(), p.begin(), p.end());
        v.push_back({ n, std::move(on), section });
    };
    auto F = [&](const char* n, std::vector<PresetValue> on, std::vector<PresetValue> p) { add(nullptr, n, std::move(on), std::move(p)); };
    auto M = [&](const char* n, std::vector<PresetValue> on, std::vector<PresetValue> p) { add("MONSTERS", n, std::move(on), std::move(p)); };
    auto C = [&](const char* n, std::vector<PresetValue> on, std::vector<PresetValue> p) { add("CREATURES", n, std::move(on), std::move(p)); };
    auto V = [&](const char* n, std::vector<PresetValue> on, std::vector<PresetValue> p) { add("VOICES", n, std::move(on), std::move(p)); };

    F("Default", L(1, 0, 1, 0), {});
    F("Just deeper", L(1, 0, 0, 0), { { "beast_pitch", -5 }, { "beast_formant", -3 }, { "beast_fry", 0 }, { "beast_level", 100 }, { "drive", 0 }, { "cave", 0 }, { "morph_x", 0 }, { "morph_y", 100 } });
    F("Growl only", L(0, 0, 1, 0), { { "growl_level", 100 }, { "morph_x", 0 }, { "morph_y", 0 } });

    M("Ogre", L(1, 0, 1, 0), { { "beast_pitch", -9 }, { "beast_formant", -7 }, { "beast_fry", 45 }, { "growl_pitch", -12 }, { "growl_rough", 60 }, { "growl_size", -6 }, { "drive", 6 }, { "cave", 18 }, { "morph_x", 25 }, { "morph_y", 60 } });
    M("Demon", L(1, 1, 1, 0), { { "beast_pitch", -12 }, { "beast_formant", -6 }, { "beast_fry", 60 }, { "split_pitch", -5 }, { "split_formant", 5 }, { "split_wobble", 10 }, { "split_level", 45 }, { "growl_pitch", -24 }, { "growl_rough", 85 }, { "growl_breath", 35 }, { "growl_size", -8 }, { "drive", 10 }, { "cave", 30 }, { "morph_x", 35 }, { "morph_y", 55 } });
    M("Giant", L(1, 0, 1, 0), { { "beast_pitch", -14 }, { "beast_formant", -10 }, { "beast_fry", 20 }, { "growl_pitch", -19 }, { "growl_rough", 30 }, { "growl_size", -12 }, { "growl_breath", 10 }, { "drive", 3 }, { "tone", -35 }, { "cave", 45 }, { "morph_x", 20 }, { "morph_y", 70 } });
    M("Troll under the bridge", L(1, 0, 1, 0), { { "beast_pitch", -6 }, { "beast_formant", -4 }, { "beast_fry", 80 }, { "growl_pitch", -12 }, { "growl_rough", 95 }, { "growl_breath", 45 }, { "growl_size", -3 }, { "drive", 12 }, { "tone", -15 }, { "cave", 25 }, { "morph_x", 40 }, { "morph_y", 40 } });
    M("Dragon", L(1, 0, 1, 1), { { "beast_pitch", -10 }, { "beast_formant", -8 }, { "beast_fry", 50 }, { "growl_pitch", -17 }, { "growl_rough", 75 }, { "growl_breath", 70 }, { "growl_size", -9 }, { "swarm_pitch", -7 }, { "swarm_spread", 3 }, { "swarm_density", 80 }, { "swarm_grain", 70 }, { "swarm_level", 30 }, { "drive", 9 }, { "cave", 40 }, { "morph_x", 30 }, { "morph_y", 45 } });
    M("Zombie", L(1, 0, 1, 0), { { "beast_pitch", -3 }, { "beast_formant", -2 }, { "beast_fry", 90 }, { "growl_pitch", 0 }, { "growl_rough", 100 }, { "growl_breath", 80 }, { "growl_size", 0 }, { "drive", 14 }, { "tone", -25 }, { "cave", 10 }, { "morph_x", 45 }, { "morph_y", 35 } });

    C("Goblin", L(1, 1, 0, 0), { { "beast_pitch", 5 }, { "beast_formant", 6 }, { "beast_fry", 35 }, { "split_pitch", 12 }, { "split_formant", 8 }, { "split_wobble", 15 }, { "split_level", 35 }, { "drive", 8 }, { "tone", 30 }, { "cave", 8 }, { "morph_x", 35 }, { "morph_y", 90 } });
    C("Alien", L(0, 1, 1, 1), { { "split_pitch", 7 }, { "split_formant", -6 }, { "split_wobble", 70 }, { "split_level", 80 }, { "growl_pitch", 12 }, { "growl_rough", 15 }, { "growl_breath", 5 }, { "growl_size", 9 }, { "swarm_pitch", 12 }, { "swarm_spread", 12 }, { "swarm_density", 40 }, { "swarm_grain", 15 }, { "drive", 2 }, { "cave", 35 }, { "morph_x", 70 }, { "morph_y", 60 } });
    C("Insect", L(0, 1, 0, 1), { { "split_pitch", 12 }, { "split_formant", 10 }, { "split_wobble", 90 }, { "split_level", 40 }, { "swarm_pitch", 19 }, { "swarm_spread", 5 }, { "swarm_density", 95 }, { "swarm_grain", 8 }, { "swarm_level", 90 }, { "drive", 6 }, { "tone", 40 }, { "cave", 5 }, { "morph_x", 85 }, { "morph_y", 30 } });
    C("Swamp thing", L(1, 0, 1, 1), { { "beast_pitch", -8 }, { "beast_formant", -3 }, { "beast_fry", 70 }, { "growl_pitch", -12 }, { "growl_rough", 65 }, { "growl_breath", 90 }, { "growl_size", -2 }, { "swarm_pitch", -12 }, { "swarm_spread", 2 }, { "swarm_density", 60 }, { "swarm_grain", 90 }, { "swarm_level", 40 }, { "drive", 5 }, { "tone", -40 }, { "cave", 30 }, { "morph_x", 40 }, { "morph_y", 40 } });
    C("Ghost", L(0, 1, 1, 1), { { "split_pitch", -2 }, { "split_formant", 3 }, { "split_wobble", 35 }, { "split_level", 50 }, { "growl_pitch", 0 }, { "growl_rough", 0 }, { "growl_breath", 100 }, { "growl_size", 4 }, { "swarm_pitch", 12 }, { "swarm_spread", 5 }, { "swarm_density", 70 }, { "swarm_grain", 120 }, { "swarm_level", 45 }, { "drive", 0 }, { "tone", 20 }, { "cave", 85 }, { "morph_x", 60 }, { "morph_y", 40 } });
    C("Rat king", L(1, 1, 0, 1), { { "beast_pitch", 9 }, { "beast_formant", 9 }, { "beast_fry", 20 }, { "split_pitch", 14 }, { "split_formant", 12 }, { "split_wobble", 50 }, { "split_level", 50 }, { "swarm_pitch", 17 }, { "swarm_spread", 6 }, { "swarm_density", 75 }, { "swarm_grain", 12 }, { "swarm_level", 60 }, { "drive", 4 }, { "tone", 35 }, { "cave", 6 }, { "morph_x", 65 }, { "morph_y", 65 } });

    V("Electro gravel", L(1, 0, 1, 0), { { "beast_pitch", -4 }, { "beast_formant", -2 }, { "beast_fry", 55 }, { "growl_pitch", -12 }, { "growl_rough", 70 }, { "growl_breath", 20 }, { "growl_size", -2 }, { "growl_level", 35 }, { "drive", 8 }, { "cave", 0 }, { "morph_x", 30 }, { "morph_y", 75 } });
    V("Villain", L(1, 1, 0, 0), { { "beast_pitch", -5 }, { "beast_formant", -4 }, { "beast_fry", 25 }, { "split_pitch", -12 }, { "split_formant", -6 }, { "split_wobble", 0 }, { "split_level", 35 }, { "drive", 3 }, { "cave", 20 }, { "morph_x", 30 }, { "morph_y", 90 } });
    V("Two-headed", L(1, 1, 0, 0), { { "beast_pitch", -7 }, { "beast_formant", -5 }, { "beast_pan", -60 }, { "split_pitch", 7 }, { "split_formant", 5 }, { "split_wobble", 20 }, { "split_level", 80 }, { "split_pan", 60 }, { "drive", 4 }, { "cave", 15 }, { "morph_x", 50 }, { "morph_y", 100 } });
    V("Whisper", L(0, 0, 1, 0), { { "growl_pitch", 0 }, { "growl_rough", 0 }, { "growl_breath", 100 }, { "growl_size", 0 }, { "growl_level", 90 }, { "drive", 0 }, { "cave", 25 }, { "mix", 85 }, { "morph_x", 0 }, { "morph_y", 0 } });
    return v;
    }();
    return v;
}

}} // namespace pump::grumble
