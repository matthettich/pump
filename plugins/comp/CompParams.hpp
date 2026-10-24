// pump COMP: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace comp {

// Parameter indices. Symbols (first column of params()) never change once released.
enum Param : int {
    kStyle = 0, kThreshold, kRatio, kKnee, kRange, kAttack, kRelease, kAutoRelease, kHold, kLookahead,
    kMix, kInGain, kOutGain, kAutoMakeup, kLink, kChannelMode, kScSource, kScGain, kScHpf, kScLpf, kScListen,
    kOversampling, kBypass,
    kNumParams
};

static const char* const kStyleNames[] = { "Clean", "Glue", "Opto", "Punch", "Vari-Mu", "Pump" };
static const char* const kChannelNames[] = { "Stereo", "Mid/Side", "Mid only", "Side only" };
static const char* const kScNames[] = { "Internal", "External" };
static const char* const kOsNames[] = { "1x", "2x", "4x" };

// Sidechain filter end stops mean "off".
constexpr float kHpfOff = 20.0f, kLpfOff = 20000.0f;
inline bool hpfActive(double f) { return f > kHpfOff + 0.5; }
inline bool lpfActive(double f) { return f < kLpfOff - 100.0; }

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    // Lookahead and oversampling change the latency and channel mode the signal topology: not automatable.
    v.push_back({ "style", "Style", "Style", 0, 5, 0, PUnit::None, PScale::Linear, A | kPInteger, kStyleNames, 1 });
    v.push_back({ "threshold", "Threshold", "Thresh", -60, 0, -18, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "ratio", "Ratio", "Ratio", 1, 100, 2, PUnit::Ratio, PScale::Power, A, nullptr, 4.0f });
    v.push_back({ "knee", "Knee", "Knee", 0, 36, 6, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "range", "Range", "Range", 0, 60, 60, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "Attack", 0.005f, 250, 10, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 5, 2500, 100, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "auto_release", "Auto Release", "Auto Rel", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "hold", "Hold", "Hold", 0, 500, 0, PUnit::ms, PScale::Power, A, nullptr, 2.0f });
    v.push_back({ "lookahead", "Lookahead", "Lookahead", 0, 20, 0, PUnit::ms, PScale::Power, 0, nullptr, 1.6f });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "in_gain", "Input Gain", "Input", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "auto_makeup", "Auto Makeup", "Auto Gain", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "stereo_link", "Stereo Link", "Link", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "channel_mode", "Channel Mode", "Channels", 0, 3, 0, PUnit::None, PScale::Linear, kPInteger, kChannelNames, 1 });
    v.push_back({ "sc_source", "Sidechain Source", "SC Source", 0, 1, 0, PUnit::None, PScale::Linear, A | kPInteger, kScNames, 1 });
    v.push_back({ "sc_gain", "Sidechain Gain", "SC Gain", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sc_hpf", "Sidechain High-Pass", "SC HPF", kHpfOff, 1000, kHpfOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sc_lpf", "Sidechain Low-Pass", "SC LPF", 1000, kLpfOff, kLpfOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sc_listen", "Sidechain Listen", "SC Listen", 0, 1, 0, PUnit::None, PScale::Linear, kPBoolean | kPNoUndo, nullptr, 1 });
    v.push_back({ "oversampling", "Oversampling", "Oversampling", 0, 2, 0, PUnit::None, PScale::Linear, kPInteger, kOsNames, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
}

#include "CompPresetsSources.hpp"
#include "CompPresetsChains.hpp"

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    // styles: 0 Clean 1 Glue 2 Opto 3 Punch 4 Vari-Mu 5 Pump; channel_mode: 0 St 1 M/S 2 Mid 3 Side; oversampling 0/1/2
    v.push_back({ "Default", {} });
    v.push_back({ "Master glue 2:1", { { "style", 1 }, { "threshold", -16 }, { "ratio", 2 }, { "knee", 12 }, { "attack", 30 },
        { "release", 300 }, { "auto_release", 1 }, { "range", 6 }, { "auto_makeup", 1 } } });
    v.push_back({ "Gentle opto leveler", { { "style", 2 }, { "threshold", -24 }, { "ratio", 2.5f }, { "knee", 18 }, { "attack", 12 },
        { "release", 350 }, { "auto_makeup", 1 } } });
    v.push_back({ "Vari-mu warmth", { { "style", 4 }, { "threshold", -22 }, { "ratio", 2 }, { "knee", 18 }, { "attack", 25 },
        { "release", 450 }, { "oversampling", 1 }, { "auto_makeup", 1 } } });
    v.push_back({ "Drum bus punch", { { "style", 3 }, { "threshold", -22 }, { "ratio", 4 }, { "knee", 6 }, { "attack", 18 },
        { "release", 90 }, { "hold", 8 }, { "mix", 85 }, { "auto_makeup", 1 }, { "sc_hpf", 60 } } });
    v.push_back({ "Parallel smash", { { "style", 0 }, { "threshold", -38 }, { "ratio", 10 }, { "knee", 0 }, { "attack", 0.8f },
        { "release", 70 }, { "mix", 40 }, { "auto_makeup", 1 } } });
    v.push_back({ "EDM pump (external SC)", { { "style", 5 }, { "sc_source", 1 }, { "threshold", -30 }, { "ratio", 8 }, { "knee", 3 },
        { "attack", 0.5f }, { "release", 180 }, { "range", 18 }, { "sc_lpf", 2000 } } });
    v.push_back({ "Vocal leveler", { { "style", 0 }, { "threshold", -24 }, { "ratio", 3 }, { "knee", 12 }, { "attack", 6 },
        { "release", 90 }, { "auto_release", 1 }, { "sc_hpf", 120 }, { "auto_makeup", 1 } } });
    v.push_back({ "Bass control (SC HPF)", { { "style", 0 }, { "threshold", -20 }, { "ratio", 4 }, { "knee", 9 }, { "attack", 20 },
        { "release", 160 }, { "sc_hpf", 110 }, { "range", 9 } } });
    v.push_back({ "Mid/side: tame sides", { { "style", 0 }, { "channel_mode", 3 }, { "threshold", -30 }, { "ratio", 3 }, { "knee", 12 },
        { "attack", 15 }, { "release", 200 }, { "range", 6 }, { "sc_hpf", 200 } } });
    v.push_back({ "Transparent peak control", { { "style", 0 }, { "threshold", -8 }, { "ratio", 8 }, { "knee", 6 }, { "attack", 0.3f },
        { "release", 60 }, { "lookahead", 2 }, { "range", 6 }, { "oversampling", 1 } } });
    v.push_back({ "Snare snap", { { "style", 3 }, { "threshold", -26 }, { "ratio", 6 }, { "knee", 3 }, { "attack", 8 },
        { "release", 45 }, { "mix", 70 }, { "auto_makeup", 1 } } });
    v.push_back({ "Bus limiter (INF)", { { "style", 0 }, { "threshold", -6 }, { "ratio", 100 }, { "knee", 2 }, { "attack", 0.05f },
        { "release", 120 }, { "auto_release", 1 }, { "lookahead", 1.5f }, { "oversampling", 2 } } });
    addSourcePresets(v);   // CompPresetsSources.hpp: vocals, drums, bass, ... (SOURCES)
    addChainPresets(v);    // CompPresetsChains.hpp: matching mastering chains (CHAINS)
    return v;
}

}} // namespace pump::comp
