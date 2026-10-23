// pump CALM: parameters and factory presets (shared by the DSP and the UI; no framework code).
#pragma once

#include "PumpParams.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace calm {

constexpr int kNumNodes = 6;   // sensitivity curve: low shelf, 4 bells, high shelf

enum Global : int {
    kDepth = 0, kSharpness, kSelectivity, kAttack, kRelease, kMode, kFocusLow, kFocusHigh,
    kStereo, kBalance, kQuality, kDelta, kMix, kTrim, kBypass,
    kAudition, kAuditionFreq, kAuditionQ, kAuditionRegion,   // Alt-listen (UI-driven, hidden, not automatable)
    kNumGlobals
};
enum NodeField : int { kNFreq = 0, kNGain, kNQ, kNodeStride };
constexpr int kNumParams = kNumGlobals + kNumNodes * kNodeStride;

inline int nodeParam(int node, int field) { return kNumGlobals + node * kNodeStride + field; }
inline int nodeOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) / kNodeStride; }
inline int nodeFieldOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) % kNodeStride; }

enum Mode : int { kSoft = 0, kHard };
enum StereoMode : int { kLinked = 0, kStereoLR, kMidSide };
enum NodeType : int { kNodeLowShelf = 0, kNodeBell, kNodeHighShelf };
inline NodeType nodeType(int node) { return node == 0 ? kNodeLowShelf : (node == kNumNodes - 1 ? kNodeHighShelf : kNodeBell); }

static const char* const kModeNames[] = { "Soft", "Hard" };
static const char* const kStereoNames[] = { "Linked", "Stereo L/R", "Mid/Side" };
static const char* const kQualityNames[] = { "1024", "2048", "4096", "8192" };
static const char* const kRegionNames[] = { "Band", "Below", "Above" };
static const int kQualitySizes[] = { 1024, 2048, 4096, 8192 };
static const float kNodeDefFreq[kNumNodes] = { 120.0f, 350.0f, 1200.0f, 3500.0f, 8000.0f, 12000.0f };
static const float kNodeDefQ[kNumNodes] = { 0.71f, 1.0f, 1.0f, 1.0f, 1.0f, 0.71f };

inline const std::vector<ParamSpec>& params()
{
    static std::deque<std::string> pool;
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    auto keep = [](const std::string& s) -> const char* { pool.push_back(s); return pool.back().c_str(); };
    const unsigned A = kPAutomatable;
    v.push_back({ "depth", "Depth", "Depth", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sharpness", "Sharpness", "Sharp", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "selectivity", "Selectivity", "Select", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "Attack", 1, 200, 8, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 10, 1000, 120, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mode", "Mode", "Mode", 0, 1, 0, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
    v.push_back({ "focus_low", "Focus Low", "Focus Lo", 20, 20000, 80, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "focus_high", "Focus High", "Focus Hi", 20, 20000, 20000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "stereo", "Stereo Mode", "Stereo", 0, 2, 0, PUnit::None, PScale::Linear, A | kPInteger, kStereoNames, 1 });
    v.push_back({ "balance", "Stereo Balance", "Balance", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "quality", "Quality (FFT size)", "Quality", 0, 3, 1, PUnit::None, PScale::Linear, kPInteger, kQualityNames, 1 });
    v.push_back({ "delta", "Delta", "Delta", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPNoUndo, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "trim", "Output Trim", "Trim", -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    v.push_back({ "audition", "Audition", "Audition", 0, 1, 0, PUnit::None, PScale::Linear, kPBoolean | kPHidden, nullptr, 1 });
    v.push_back({ "audition_freq", "Audition Frequency", "Aud Freq", 20, 20000, 1000, PUnit::Hz, PScale::Log, kPHidden, nullptr, 1 });
    v.push_back({ "audition_q", "Audition Q", "Aud Q", 0.3f, 12, 2, PUnit::None, PScale::Log, kPHidden, nullptr, 1 });
    v.push_back({ "audition_region", "Audition Region", "Aud Region", 0, 2, 0, PUnit::None, PScale::Linear, kPInteger | kPHidden, kRegionNames, 1 });
    for (int n = 0; n < kNumNodes; ++n)
    {
        const std::string i = std::to_string(n + 1), p = "s" + i + "_", N = "Sensitivity " + i + " ";
        v.push_back({ keep(p + "freq"), keep(N + "Frequency"), keep("S" + i + " Freq"), 20, 20000, kNodeDefFreq[n], PUnit::Hz, PScale::Log, A, nullptr, 1 });
        v.push_back({ keep(p + "gain"), keep(N + "Gain"), keep("S" + i + " Gain"), -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "q"), keep(N + "Width (Q)"), keep("S" + i + " Q"), 0.1f, 10, kNodeDefQ[n], PUnit::None, PScale::Log, A, nullptr, 1 });
    }
    return v;
}

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    v.push_back({ "Default", {} });
    v.push_back({ "Gentle master de-harsh", {
        { "depth", 32 }, { "sharpness", 55 }, { "selectivity", 58 }, { "attack", 12 }, { "release", 160 },
        { "focus_low", 700 }, { "focus_high", 14000 }, { "s4_freq", 3200 }, { "s4_gain", 3 }, { "s4_q", 0.8f } } });
    v.push_back({ "Vocal de-ess-ish", {
        { "depth", 62 }, { "sharpness", 62 }, { "selectivity", 42 }, { "attack", 2.5f }, { "release", 60 },
        { "focus_low", 3500 }, { "focus_high", 16000 }, { "s6_freq", 6000 }, { "s6_gain", 5 }, { "s5_freq", 7500 }, { "s5_gain", 3 } } });
    v.push_back({ "Tame low mids", {
        { "depth", 50 }, { "sharpness", 40 }, { "selectivity", 48 }, { "attack", 20 }, { "release", 220 },
        { "focus_low", 110 }, { "focus_high", 900 }, { "s2_freq", 300 }, { "s2_gain", 3 }, { "s2_q", 0.7f }, { "quality", 2 } } });
    v.push_back({ "Bright mix softener", {
        { "depth", 40 }, { "sharpness", 30 }, { "selectivity", 35 }, { "attack", 10 }, { "release", 140 },
        { "focus_low", 2000 }, { "focus_high", 20000 }, { "s6_freq", 8000 }, { "s6_gain", 3 } } });
    v.push_back({ "Aggressive resonance hunt", {
        { "depth", 85 }, { "sharpness", 80 }, { "selectivity", 30 }, { "attack", 4 }, { "release", 80 }, { "mode", 1 },
        { "focus_low", 150 }, { "focus_high", 16000 }, { "quality", 2 } } });
    v.push_back({ "Side-only harshness", {
        { "depth", 60 }, { "sharpness", 50 }, { "selectivity", 45 }, { "stereo", 2 }, { "balance", 100 },
        { "focus_low", 1500 }, { "focus_high", 16000 }, { "s4_freq", 4000 }, { "s4_gain", 2 } } });
    v.push_back({ "Transparent master (8192)", {
        { "depth", 25 }, { "sharpness", 70 }, { "selectivity", 65 }, { "attack", 30 }, { "release", 300 },
        { "focus_low", 60 }, { "quality", 3 } } });
    v.push_back({ "Drum bus ring control", {
        { "depth", 55 }, { "sharpness", 75 }, { "selectivity", 40 }, { "attack", 2 }, { "release", 50 }, { "mode", 1 },
        { "focus_low", 90 }, { "focus_high", 7000 } } });
    v.push_back({ "Fast & tight (1024)", {
        { "depth", 45 }, { "sharpness", 50 }, { "selectivity", 50 }, { "attack", 1.5f }, { "release", 40 }, { "quality", 0 },
        { "focus_low", 300 } } });
    return v;
}

}} // namespace pump::calm
