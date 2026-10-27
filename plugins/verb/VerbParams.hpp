// pump VERB: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pump { namespace verb {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kMode = 0, kPredelay, kSync, kNote, kSize, kDecay, kDiffusion, kBalance,
    kBassMult, kCrossover, kHighDamp, kDampFreq, kLowCut, kHighCut,
    kModDepth, kModRate, kWidth, kAge, kEnvTime, kPitch, kShimmer, kDuck, kFreeze,
    kMix, kOutGain, kBypass,
    kNumParams
};

enum Mode : int {
    kHall = 0, kRoom, kChamber, kPlate,          // modern studio
    kVintageHall, kVintagePlate,                 // late-70s digital
    kClassicHall, kClassicPlate,                 // 80s flagship digital
    kRack,                                       // 90s budget multi-effect
    kGated, kReverse, kShimmerMode,              // special
    kNumModes
};
static const char* const kModeNames[] = {
    "Hall", "Room", "Chamber", "Plate", "Vintage Hall", "Vintage Plate", "Classic Hall", "Classic Plate",
    "90s Rack", "Gated", "Reverse", "Shimmer"
};
// the families, for the mode keys and the status line
inline bool isVintage(int m) { return m == kVintageHall || m == kVintagePlate; }
inline bool isClassic(int m) { return m == kClassicHall || m == kClassicPlate; }
inline bool hasAge(int m) { return isVintage(m) || isClassic(m) || m == kRack; }
inline bool isBurst(int m) { return m == kGated || m == kReverse; }
inline bool hasModulation(int m) { return !isBurst(m); }

// pre-delay sync: the note divisions up to a dotted quarter (Tempo.hpp's names)
constexpr int kMaxSyncNote = kNote4D;
constexpr double kMaxPredelayMs = 500.0, kMaxSyncedMs = 1000.0;

// Filter end stops mean "off".
constexpr float kLowCutOff = 10.0f, kHighCutOff = 22000.0f;
inline bool lowCutActive(double f) { return f > kLowCutOff + 0.05; }
inline bool highCutActive(double f) { return f < kHighCutOff - 10.0; }

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    v.push_back({ "mode", "Mode", "Mode", 0, kNumModes - 1, kHall, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
    v.push_back({ "predelay", "Pre-Delay", "Pre-Delay", 0, (float)kMaxPredelayMs, 20, PUnit::ms, PScale::Power, A, nullptr, 2.0f });
    v.push_back({ "predelay_sync", "Pre-Delay Sync", "Sync", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "predelay_note", "Pre-Delay Note", "Note", 0, kMaxSyncNote, kNote32, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "size", "Size", "Size", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "Decay", 0.1f, 30.0f, 2.2f, PUnit::Seconds, PScale::Log, A, nullptr, 1 });
    v.push_back({ "diffusion", "Diffusion", "Diffusion", 0, 100, 80, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "early_late", "Early / Late", "Early/Late", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bass_mult", "Bass Multiplier", "Bass Mult", 0.5f, 2.0f, 1.2f, PUnit::None, PScale::Log, A, nullptr, 1 });
    v.push_back({ "crossover", "Crossover", "Crossover", 50, 2000, 600, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "high_damp", "High Damping", "High Damp", 0.1f, 1.0f, 0.55f, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "damp_freq", "Damping Frequency", "Damp Freq", 1000, 20000, 6000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "low_cut", "Low Cut", "Low Cut", kLowCutOff, 1000, kLowCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "high_cut", "High Cut", "High Cut", 1000, kHighCutOff, kHighCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mod_depth", "Modulation Depth", "Mod Depth", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mod_rate", "Modulation Rate", "Mod Rate", 0.05f, 5.0f, 0.7f, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "width", "Width", "Width", 0, 150, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "age", "Age", "Age", 0, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "env_time", "Gate / Reverse Time", "Time", 50, 1500, 320, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "shimmer_pitch", "Shimmer Pitch", "Pitch", -24, 24, 12, PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    v.push_back({ "shimmer_amount", "Shimmer Amount", "Amount", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "duck", "Duck", "Duck", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "freeze", "Freeze", "Freeze", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. Modes: 0 hall 1 room 2 chamber 3 plate 4 vintage hall 5 vintage plate 6 classic hall
// 7 classic plate 8 90s rack 9 gated 10 reverse 11 shimmer. Notes (predelay_note): 2 = 1/32, 5 = 1/16, 8 = 1/8.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Concert hall", { { "mode", 0 }, { "size", 68 }, { "decay", 2.8f }, { "predelay", 28 }, { "bass_mult", 1.35f }, { "crossover", 450 },
                        { "high_damp", 0.5f }, { "damp_freq", 5000 }, { "low_cut", 40 }, { "mix", 28 } });
    F("Small room", { { "mode", 1 }, { "size", 34 }, { "decay", 0.6f }, { "predelay", 4 }, { "early_late", -25 }, { "high_damp", 0.6f },
                      { "mix", 22 } });
    F("Live room", { { "mode", 1 }, { "size", 62 }, { "decay", 1.1f }, { "predelay", 8 }, { "bass_mult", 1.1f }, { "mix", 25 } });
    F("Stone chamber", { { "mode", 2 }, { "size", 55 }, { "decay", 1.9f }, { "predelay", 12 }, { "high_damp", 0.7f }, { "damp_freq", 7500 },
                         { "mix", 26 } });
    F("Bright plate", { { "mode", 3 }, { "decay", 1.7f }, { "predelay", 12 }, { "high_damp", 0.8f }, { "damp_freq", 9000 }, { "bass_mult", 0.9f },
                        { "low_cut", 90 }, { "mix", 25 } });
    F("Vintage hall", { { "mode", 4 }, { "size", 60 }, { "decay", 2.6f }, { "predelay", 24 }, { "mod_depth", 55 }, { "mod_rate", 1.1f },
                        { "age", 40 }, { "mix", 28 } });
    F("Vintage plate", { { "mode", 5 }, { "decay", 1.8f }, { "predelay", 10 }, { "mod_depth", 45 }, { "age", 35 }, { "low_cut", 80 }, { "mix", 25 } });
    F("Classic hall", { { "mode", 6 }, { "size", 70 }, { "decay", 3.2f }, { "predelay", 30 }, { "bass_mult", 1.5f }, { "crossover", 400 },
                        { "mod_depth", 60 }, { "mod_rate", 0.5f }, { "age", 20 }, { "mix", 28 } });
    F("Classic plate", { { "mode", 7 }, { "decay", 2.1f }, { "predelay", 14 }, { "mod_depth", 55 }, { "mod_rate", 0.6f }, { "age", 15 },
                         { "low_cut", 70 }, { "mix", 26 } });
    F("90s rack hall", { { "mode", 8 }, { "size", 75 }, { "decay", 2.4f }, { "predelay", 20 }, { "diffusion", 70 }, { "age", 30 }, { "mix", 25 } });
    F("Gated drums", { { "mode", 9 }, { "env_time", 300 }, { "decay", 4.0f }, { "predelay", 0 }, { "low_cut", 100 }, { "mix", 35 } });
    F("Reverse swell", { { "mode", 10 }, { "env_time", 650 }, { "decay", 0.7f }, { "predelay", 0 }, { "mix", 40 } });
    F("Shimmer pad", { { "mode", 11 }, { "size", 70 }, { "decay", 6.5f }, { "predelay", 40 }, { "shimmer_amount", 55 }, { "low_cut", 120 },
                       { "mix", 40 } });
    S("Drums: tight room", { { "mode", 1 }, { "size", 40 }, { "decay", 0.45f }, { "predelay", 2 }, { "early_late", -30 }, { "low_cut", 120 },
                             { "mix", 20 } });
    S("Drums: snare plate", { { "mode", 3 }, { "decay", 1.4f }, { "predelay", 15 }, { "low_cut", 200 }, { "high_damp", 0.65f }, { "mix", 30 } });
    S("Drums: big gated snare", { { "mode", 9 }, { "env_time", 380 }, { "decay", 6.0f }, { "size", 70 }, { "low_cut", 160 }, { "mix", 40 } });
    S("Vocals: plate", { { "mode", 3 }, { "decay", 1.8f }, { "predelay_sync", 1 }, { "predelay_note", 5 }, { "low_cut", 150 },
                         { "high_cut", 10000 }, { "duck", 30 }, { "mix", 25 } });
    S("Vocals: ballad hall", { { "mode", 0 }, { "size", 62 }, { "decay", 2.6f }, { "predelay", 60 }, { "low_cut", 120 }, { "duck", 25 }, { "mix", 25 } });
    S("Guitar: vintage room", { { "mode", 4 }, { "size", 30 }, { "decay", 0.9f }, { "predelay", 6 }, { "age", 45 }, { "mod_depth", 40 }, { "mix", 25 } });
    S("Bass: short ambience", { { "mode", 1 }, { "size", 30 }, { "decay", 0.5f }, { "low_cut", 180 }, { "width", 70 }, { "mix", 14 } });
    S("Keys: warm chamber", { { "mode", 2 }, { "size", 60 }, { "decay", 2.2f }, { "predelay", 18 }, { "high_damp", 0.45f }, { "damp_freq", 4500 },
                              { "mix", 28 } });
    S("Synths: classic wash", { { "mode", 6 }, { "size", 85 }, { "decay", 5.5f }, { "mod_depth", 75 }, { "mod_rate", 0.4f }, { "bass_mult", 1.3f },
                                { "low_cut", 90 }, { "mix", 35 } });
    S("Strings: concert hall", { { "mode", 0 }, { "size", 75 }, { "decay", 3.4f }, { "predelay", 35 }, { "bass_mult", 1.25f }, { "high_damp", 0.45f },
                                 { "mix", 30 } });
    S("Bus: glue room", { { "mode", 1 }, { "size", 45 }, { "decay", 0.7f }, { "predelay", 3 }, { "low_cut", 150 }, { "high_cut", 9000 }, { "mix", 12 } });
    C("Cathedral", { { "mode", 0 }, { "size", 100 }, { "decay", 12.0f }, { "predelay", 70 }, { "bass_mult", 1.4f }, { "high_damp", 0.4f },
                     { "damp_freq", 4000 }, { "mix", 40 } });
    C("Octave-down shimmer", { { "mode", 11 }, { "shimmer_pitch", -12 }, { "decay", 5.0f }, { "shimmer_amount", 45 }, { "high_cut", 7000 },
                               { "mix", 40 } });
    C("Fifth shimmer", { { "mode", 11 }, { "shimmer_pitch", 7 }, { "decay", 7.0f }, { "shimmer_amount", 60 }, { "size", 80 }, { "mix", 40 } });
    C("Worn-out machine", { { "mode", 4 }, { "decay", 3.5f }, { "age", 100 }, { "mod_depth", 80 }, { "mod_rate", 1.8f }, { "high_cut", 7000 },
                            { "mix", 35 } });
    C("Ghost reverse", { { "mode", 10 }, { "env_time", 1100 }, { "decay", 1.0f }, { "predelay_sync", 1 }, { "predelay_note", 8 },
                         { "low_cut", 200 }, { "mix", 45 } });
    return v;
    }();
    return v;
}

}} // namespace pump::verb
