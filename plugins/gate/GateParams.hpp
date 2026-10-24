// pump GATE: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pump { namespace gate {

constexpr int kMaxSteps = 32;

// Parameter indices. Symbols (first column of params()) never change once released; append only. The 32 step
// levels follow the globals in order (step01 .. step32).
enum Param : int {
    kSteps = 0, kRate, kSwing, kLength, kAttack, kRelease, kSmooth, kDepth, kMode, kFilterLow, kFilterHigh,
    kResonance, kOffset, kMix, kOutGain, kBypass,
    kStep1,
    kNumParams = kStep1 + kMaxSteps
};

enum Mode : int { kVolume = 0, kFilter, kBoth, kNumModes };
static const char* const kModeNames[] = { "Volume", "Filter", "Both" };

static const char* const kStepSymbols[kMaxSteps] = {
    "step01", "step02", "step03", "step04", "step05", "step06", "step07", "step08",
    "step09", "step10", "step11", "step12", "step13", "step14", "step15", "step16",
    "step17", "step18", "step19", "step20", "step21", "step22", "step23", "step24",
    "step25", "step26", "step27", "step28", "step29", "step30", "step31", "step32"
};
static const char* const kStepNames[kMaxSteps] = {
    "Step 1", "Step 2", "Step 3", "Step 4", "Step 5", "Step 6", "Step 7", "Step 8",
    "Step 9", "Step 10", "Step 11", "Step 12", "Step 13", "Step 14", "Step 15", "Step 16",
    "Step 17", "Step 18", "Step 19", "Step 20", "Step 21", "Step 22", "Step 23", "Step 24",
    "Step 25", "Step 26", "Step 27", "Step 28", "Step 29", "Step 30", "Step 31", "Step 32"
};

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    v.push_back({ "steps", "Steps", "Steps", 1, kMaxSteps, 16, PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    v.push_back({ "rate", "Rate", "Rate", 0, kNumNotes - 1, kNote16, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "swing", "Swing", "Swing", 0, 75, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "length", "Gate Length", "Length", 5, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "Attack", 0, 50, 1, PUnit::ms, PScale::Power, A, nullptr, 3 });
    v.push_back({ "release", "Release", "Release", 0, 500, 20, PUnit::ms, PScale::Power, A, nullptr, 3 });
    v.push_back({ "smooth", "Smooth", "Smooth", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "depth", "Depth", "Depth", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mode", "Mode", "Mode", 0, kNumModes - 1, kVolume, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
    v.push_back({ "filter_low", "Filter Low", "Low", 20, 5000, 250, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "filter_high", "Filter High", "High", 200, 20000, 20000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "resonance", "Resonance", "Reso", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "stereo_offset", "Stereo Offset", "Offset", 0, 16, 0, PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    for (int k = 0; k < kMaxSteps; ++k)
        v.push_back({ kStepSymbols[k], kStepNames[k], kStepNames[k], 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    return v;
}

// A pattern written as text: 'x' = 100 %, '.' = 0 %, '1'..'9' = 10..90 % (spaces are ignored). It sets STEPS to its
// length and repeats to fill all 32 steps, so a longer STEPS setting plays it again.
inline std::vector<PresetValue> pattern(const char* text, std::vector<PresetValue> rest)
{
    std::vector<float> lv;
    for (const char* c = text; *c; ++c)
    {
        if (*c == 'x') lv.push_back(100.0f);
        else if (*c == '.') lv.push_back(0.0f);
        else if (*c >= '1' && *c <= '9') lv.push_back(10.0f * (float)(*c - '0'));
    }
    if (lv.empty()) lv.push_back(100.0f);
    std::vector<PresetValue> v = std::move(rest);
    v.push_back({ "steps", (float)std::min<size_t>(lv.size(), kMaxSteps) });
    for (int k = 0; k < kMaxSteps; ++k) v.push_back({ kStepSymbols[k], lv[(size_t)k % lv.size()] });
    return v;
}

// Factory presets. Rates are indices into kNoteNames (kNote16 = 1/16, ...); modes: 0 volume 1 filter 2 both.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Trance 16ths", pattern("xx.x xx.x x.xx x.x.", { { "length", 70 }, { "release", 12 } }));
    F("Straight 8ths", pattern("xxxx xxxx", { { "rate", kNote8 }, { "length", 55 }, { "release", 18 } }));
    F("Half-time chop", pattern("xx.. x... xx.. x.x.", { { "rate", kNote8 }, { "length", 85 }, { "release", 35 } }));
    F("Stutter", pattern("xxxx xxxx .... xxxx x.x. x.x. xxxx xx..", { { "rate", kNote32 }, { "length", 45 }, { "attack", 0 }, { "release", 3 } }));
    F("Triplet bounce", pattern("x.x xx. x.x x.x", { { "rate", kNote8T }, { "length", 70 }, { "release", 20 } }));
    F("Dotted eighths", pattern("x..x ..x. .x.. x.x.", { { "length", 80 }, { "release", 25 } }));
    F("Sidechain pump", pattern(".47x", { { "length", 100 }, { "attack", 35 }, { "release", 4 }, { "smooth", 35 }, { "depth", 85 } }));
    F("Filter ripple", pattern("x.5. x.x5 .x5. x.7.", { { "mode", kFilter }, { "length", 70 }, { "smooth", 30 }, { "resonance", 60 },
                                                       { "filter_low", 350 }, { "filter_high", 9000 } }));
    F("Stereo ping-pong", pattern("x.x. x.x. x.x. x.x.", { { "stereo_offset", 1 }, { "length", 80 }, { "release", 20 } }));
    F("Swung 16ths", pattern("xxxx xxxx xxxx xxxx", { { "swing", 33 }, { "length", 60 }, { "release", 15 } }));
    F("Quarter-note swell", pattern("x.x.", { { "rate", kNote4 }, { "length", 60 }, { "attack", 50 }, { "release", 220 }, { "smooth", 60 }, { "depth", 80 } }));
    S("Pads: classic gate", pattern("xx.x x.xx .xx. xx.x", { { "length", 65 }, { "release", 15 } }));
    S("Vocals: stutter edit", pattern("xxxx .... x.x. xxxx", { { "rate", kNote32 }, { "length", 50 }, { "release", 5 } }));
    S("Drums: tight chop", pattern("x.xx x.x. x.xx x.xx", { { "length", 55 }, { "release", 8 }, { "depth", 70 } }));
    S("Bass: 16th pump", pattern(".6xx", { { "length", 100 }, { "attack", 25 }, { "release", 3 }, { "smooth", 25 }, { "depth", 75 } }));
    S("Guitar: tremolo", pattern("xxxx", { { "length", 50 }, { "attack", 5 }, { "release", 30 }, { "smooth", 80 }, { "depth", 60 } }));
    S("Keys: triplet flutter", pattern("x.x x.x x.x xx.", { { "rate", kNote16T }, { "length", 70 }, { "smooth", 40 }, { "depth", 70 } }));
    S("Synths: arp gate", pattern("x.xx .xx. x.xx .x.x", { { "mode", kBoth }, { "swing", 15 }, { "length", 45 }, { "release", 10 }, { "depth", 80 },
                                                       { "filter_low", 600 }, { "resonance", 35 } }));
    S("Bus: subtle groove", pattern("x6x7 x6x8", { { "swing", 20 }, { "length", 80 }, { "release", 30 }, { "smooth", 20 }, { "depth", 30 } }));
    C("Seven-step loop", pattern("x.xxx.x", { { "length", 70 }, { "release", 15 } }));
    C("Five against four", pattern("xx.x.", { { "length", 60 }, { "release", 12 }, { "stereo_offset", 2 } }));
    C("Two-bar riser", pattern("1122 3344 5566 7788 99xx xxxx xxxx xxxx", { { "length", 60 }, { "release", 10 } }));
    C("Glitch shuffle", pattern("x.xx x..x .xx. xxxx ..x. x.xx", { { "rate", kNote32 }, { "swing", 25 }, { "length", 60 }, { "release", 2 } }));
    C("Wah steps", pattern("38x5 x29x", { { "rate", kNote8 }, { "mode", kFilter }, { "length", 100 }, { "smooth", 45 }, { "resonance", 75 },
                                         { "filter_low", 300 }, { "filter_high", 5000 } }));
    C("Wide shimmer", pattern("xx.x x.xx .xx. x.x.", { { "rate", kNote32 }, { "stereo_offset", 3 }, { "length", 50 }, { "release", 10 }, { "depth", 85 } }));
    return v;
}

}} // namespace pump::gate
