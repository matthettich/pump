// pump METER: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace meter {

enum Param : int { kTarget = 0, kCustomLufs, kCustomTP, kHistory, kScale, kReset, kNumParams };

static const char* const kTargetNames[] = { "Streaming -14", "Apple Music -16", "YouTube -14", "Spotify Loud -11", "Club -9",
                                            "EBU R128 -23", "ATSC A/85 -24", "Custom" };
static const float kTargetLufsV[] = { -14.0f, -16.0f, -14.0f, -11.0f, -9.0f, -23.0f, -24.0f, -14.0f };
static const float kTargetTPV[] = { -1.0f, -1.0f, -1.0f, -2.0f, -0.3f, -1.0f, -2.0f, -1.0f };
static const float kTargetTolV[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 2.0f, 1.0f };   // LU either side counted "on target"
static const char* const kHistoryNames[] = { "1 min", "3 min", "10 min", "30 min" };
static const float kHistoryMinutes[] = { 1.0f, 3.0f, 10.0f, 30.0f };
static const char* const kScaleNames[] = { "EBU +9", "EBU +18", "-60..0 LUFS", "-36..0 LUFS" };

constexpr int kCustom = 7;

inline float targetLufs(int t, float custom) { return t == kCustom ? custom : kTargetLufsV[t < 0 ? 0 : (t > 7 ? 7 : t)]; }
inline float targetTP(int t, float custom) { return t == kCustom ? custom : kTargetTPV[t < 0 ? 0 : (t > 7 ? 7 : t)]; }
inline float targetTol(int t) { return kTargetTolV[t < 0 ? 0 : (t > 7 ? 7 : t)]; }

// Scale range (LUFS) of the history graph and loudness bar for a scale choice and target.
inline void scaleRange(int scale, float target, float& lo, float& hi)
{
    switch (scale)
    {
    case 0: lo = target - 18.0f; hi = target + 9.0f; break;
    case 1: lo = target - 36.0f; hi = target + 18.0f; break;
    case 2: lo = -60.0f; hi = 0.0f; break;
    default: lo = -36.0f; hi = 0.0f; break;
    }
}

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    v.push_back({ "target", "Loudness Target", "Target", 0, 7, 0, PUnit::None, PScale::Linear, A | kPInteger, kTargetNames, 1 });
    v.push_back({ "custom_lufs", "Custom Target Loudness", "Custom LUFS", -36, -5, -14, PUnit::LU, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "custom_tp", "Custom True Peak Limit", "Custom TP", -9, 0, -1, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "history", "History Length", "History", 0, 3, 1, PUnit::None, PScale::Linear, A | kPInteger, kHistoryNames, 1 });
    v.push_back({ "scale", "Meter Scale", "Scale", 0, 3, 0, PUnit::None, PScale::Linear, A | kPInteger, kScaleNames, 1 });
    // Reset: a counter the UI raises; the DSP resets whenever it goes above the highest value it has seen
    // (so undo, A/B and presets, which restore older values, never trigger a reset). Not automatable.
    v.push_back({ "reset", "Reset Count", "Reset", 0, 1000000, 0, PUnit::None, PScale::Linear, kPInteger | kPHidden, nullptr, 1 });
    return v;
}

#include "MeterPresetsSources.hpp"
#include "MeterPresetsChains.hpp"

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    v.push_back({ "Default", {} });
    v.push_back({ "Streaming -14 LUFS", { { "target", 0 } } });
    v.push_back({ "Apple Music -16 LUFS", { { "target", 1 } } });
    v.push_back({ "YouTube -14 LUFS", { { "target", 2 } } });
    v.push_back({ "Spotify Loud -11 LUFS", { { "target", 3 } } });
    v.push_back({ "Club -9 LUFS", { { "target", 4 }, { "scale", 3 } } });
    v.push_back({ "EBU R128 -23 LUFS", { { "target", 5 }, { "history", 2 }, { "scale", 0 } } });
    v.push_back({ "ATSC A/85 -24 LKFS", { { "target", 6 }, { "history", 2 } } });
    v.push_back({ "Long programme 30 min", { { "target", 5 }, { "history", 3 }, { "scale", 1 } } });
    v.push_back({ "Podcast -16 LUFS", { { "target", 7 }, { "custom_lufs", -16 }, { "custom_tp", -1 } } });
    v.push_back({ "Film -27 LKFS", { { "target", 7 }, { "custom_lufs", -27 }, { "custom_tp", -2 }, { "history", 2 }, { "scale", 1 } } });
    v.push_back({ "Full range -60..0", { { "scale", 2 }, { "history", 0 } } });
    addSourcePresets(v);   // MeterPresetsSources.hpp: vocals, drums, bass, ... (SOURCES)
    addChainPresets(v);    // MeterPresetsChains.hpp: matching mastering chains (CHAINS)
    return v;
}

}} // namespace pump::meter
