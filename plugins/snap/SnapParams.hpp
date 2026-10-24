// pump SNAP: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace snap {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kAttack = 0, kSustain, kAttackTime, kRelease, kLink, kFocus, kBand, kCrossover, kLookahead,
    kClip, kCeiling, kDelta, kMix, kOutGain, kBypass,
    kNumParams
};

enum Focus : int { kFocusFull = 0, kFocusLow, kFocusMid, kFocusHigh, kNumFocus };
static const char* const kFocusNames[] = { "Full", "Low", "Mid", "High" };
enum Band : int { kBandFull = 0, kBandLow, kBandHigh, kNumBands };
static const char* const kBandNames[] = { "Full", "Low", "High" };

constexpr float kMaxLookaheadMs = 5.0f;

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    v.push_back({ "attack", "Attack", "Attack", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sustain", "Sustain", "Sustain", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack_time", "Attack Time", "Time", 1, 100, 10, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 20, 2000, 200, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "link", "Stereo Link", "Link", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "focus", "Detector Focus", "Focus", 0, kNumFocus - 1, kFocusFull, PUnit::None, PScale::Linear, A | kPInteger, kFocusNames, 1 });
    v.push_back({ "band", "Band", "Band", 0, kNumBands - 1, kBandFull, PUnit::None, PScale::Linear, A | kPInteger, kBandNames, 1 });
    v.push_back({ "crossover", "Crossover", "Crossover", 40, 8000, 250, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    // lookahead changes the latency: not automatable
    v.push_back({ "lookahead", "Lookahead", "Lookahead", 0, kMaxLookaheadMs, 0, PUnit::ms, PScale::Linear, 0, nullptr, 1 });
    v.push_back({ "clip", "Clip", "Clip", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "ceiling", "Clip Ceiling", "Ceiling", -12, 0, -1, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "delta", "Delta", "Delta", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPNoUndo, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
}

// Factory presets. Focus: 0 full 1 low 2 mid 3 high; band: 0 full 1 low 2 high.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Punch up", { { "attack", 45 }, { "attack_time", 12 } });
    F("Snap and tighten", { { "attack", 40 }, { "sustain", -40 }, { "attack_time", 8 }, { "release", 160 } });
    F("Soften attacks", { { "attack", -50 }, { "attack_time", 6 } });
    F("More body", { { "sustain", 45 }, { "release", 300 } });
    F("Dry it out", { { "sustain", -60 }, { "release", 140 } });
    F("Lookahead punch (clipped)", { { "attack", 70 }, { "attack_time", 10 }, { "lookahead", 1.5f }, { "clip", 1 }, { "ceiling", -1 } });
    F("Parallel smack (mix 50%)", { { "attack", 100 }, { "sustain", -30 }, { "attack_time", 15 }, { "mix", 50 } });
    F("Lows only: tighten", { { "sustain", -50 }, { "release", 120 }, { "focus", 1 }, { "band", 1 }, { "crossover", 160 } });
    F("Highs only: crack", { { "attack", 60 }, { "attack_time", 4 }, { "focus", 3 }, { "band", 2 }, { "crossover", 2500 } });
    S("Drums: kick punch", { { "attack", 60 }, { "sustain", -15 }, { "attack_time", 9 }, { "release", 120 }, { "focus", 1 }, { "lookahead", 1 }, { "clip", 1 }, { "ceiling", -1 } });
    S("Drums: kick tighten", { { "sustain", -55 }, { "release", 90 }, { "focus", 1 }, { "band", 1 }, { "crossover", 180 } });
    S("Drums: snare crack", { { "attack", 55 }, { "sustain", 10 }, { "attack_time", 5 }, { "release", 180 }, { "focus", 3 }, { "lookahead", 0.8f } });
    S("Drums: snare body", { { "sustain", 45 }, { "release", 260 }, { "focus", 2 } });
    S("Drums: room bloom", { { "attack", -20 }, { "sustain", 60 }, { "release", 600 } });
    S("Drums: tame overheads", { { "attack", -35 }, { "attack_time", 3 }, { "focus", 3 }, { "link", 100 } });
    S("Drums: shorter hats", { { "sustain", -40 }, { "release", 80 }, { "focus", 3 } });
    S("Drums: bus punch", { { "attack", 30 }, { "sustain", -10 }, { "attack_time", 15 }, { "release", 220 }, { "lookahead", 1 }, { "clip", 1 }, { "ceiling", -0.5f } });
    S("Bass: pick attack", { { "attack", 40 }, { "attack_time", 4 }, { "band", 2 }, { "crossover", 700 }, { "focus", 3 } });
    S("Bass: tighter notes", { { "sustain", -35 }, { "release", 150 }, { "focus", 1 } });
    S("Guitar: soften strums", { { "attack", -30 }, { "attack_time", 6 }, { "focus", 3 } });
    S("Guitar: chug definition", { { "attack", 40 }, { "sustain", -30 }, { "attack_time", 7 }, { "release", 120 }, { "focus", 2 } });
    S("Vocals: less room", { { "sustain", -30 }, { "release", 120 }, { "focus", 2 } });
    S("Vocals: consonant clarity", { { "attack", 25 }, { "attack_time", 4 }, { "band", 2 }, { "crossover", 2500 }, { "focus", 3 } });
    S("Keys: piano attack", { { "attack", 30 }, { "attack_time", 10 }, { "release", 300 } });
    S("Synths: pluck snap", { { "attack", 45 }, { "sustain", -20 }, { "attack_time", 6 }, { "release", 150 } });
    S("Bus: soften the mix", { { "attack", -20 }, { "attack_time", 20 }, { "lookahead", 1 } });
    C("Gated tails", { { "sustain", -100 }, { "release", 40 } });
    C("Endless room", { { "sustain", 100 }, { "release", 2000 } });
    C("Click machine", { { "attack", 100 }, { "attack_time", 1 }, { "clip", 1 }, { "ceiling", -3 } });
    C("Swell", { { "attack", -100 }, { "sustain", 60 }, { "attack_time", 60 }, { "release", 800 } });
    return v;
}

}} // namespace pump::snap
