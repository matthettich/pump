// pump LIMIT: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace limit {

enum Param : int {
    kGain = 0, kCeiling, kStyle, kLookahead, kAttack, kRelease, kLinkTrans, kLinkRel,
    kTruePeak, kOversampling, kDC, kDither, kShaping, kUnity, kBypass, kTarget, kTargetCustom,
    kNumParams
};

enum Style : int { kTransparent = 0, kPunchy, kDynamic, kAllround, kAggressive, kModern, kSafe, kNumStyles };

static const char* const kStyleNames[] = { "Transparent", "Punchy", "Dynamic", "Allround", "Aggressive", "Modern", "Safe" };
static const char* const kOSNames[] = { "Off", "2x", "4x", "8x" };
static const char* const kDitherNames[] = { "Off", "16-bit", "24-bit" };
static const char* const kShapingNames[] = { "Off", "Shaped" };
static const char* const kTargetNames[] = { "Off", "Streaming -14", "Apple Music -16", "Club -9", "CD -9", "YouTube -14", "Custom" };
static const float kTargetLufs[] = { 0.0f, -14.0f, -16.0f, -9.0f, -9.0f, -14.0f, 0.0f };   // [0] off, [6] custom

inline float targetLufs(int target, float custom)
{
    if (target <= 0 || target > 6) return 0.0f;
    return target == 6 ? custom : kTargetLufs[target];
}

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    v.push_back({ "gain", "Gain", "Gain", 0, 30, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "ceiling", "Output Ceiling", "Ceiling", -12, 0, -1, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "style", "Style", "Style", 0, kNumStyles - 1, kAllround, PUnit::None, PScale::Linear, A | kPInteger, kStyleNames, 1 });
    // lookahead and oversampling change the latency: not automatable
    v.push_back({ "lookahead", "Lookahead", "Lookahead", 0.1f, 10, 5, PUnit::ms, PScale::Log, 0, nullptr, 1 });
    v.push_back({ "attack", "Attack", "Attack", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 1, 1000, 100, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "link_transients", "Channel Link Transients", "Link Trans", 0, 100, 75, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "link_release", "Channel Link Release", "Link Rel", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "true_peak", "True Peak Limiting", "True Peak", 0, 1, 1, PUnit::None, PScale::Linear, kPBoolean, nullptr, 1 });
    v.push_back({ "oversampling", "Oversampling", "Oversampling", 0, 3, 1, PUnit::None, PScale::Linear, kPInteger, kOSNames, 1 });
    v.push_back({ "dc_filter", "DC Offset Filter", "DC", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "dither", "Dither", "Dither", 0, 2, 0, PUnit::None, PScale::Linear, A | kPInteger, kDitherNames, 1 });
    v.push_back({ "noise_shaping", "Noise Shaping", "Shaping", 0, 1, 1, PUnit::None, PScale::Linear, A | kPInteger, kShapingNames, 1 });
    v.push_back({ "unity", "Unity Gain Listen", "Unity", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPNoUndo, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    v.push_back({ "target", "Loudness Target", "Target", 0, 6, 1, PUnit::None, PScale::Linear, kPInteger, kTargetNames, 1 });
    v.push_back({ "target_custom", "Custom Target", "Custom", -30, -5, -14, PUnit::LU, PScale::Linear, 0, nullptr, 1 });
    return v;
}

#include "LimitPresetsSources.hpp"
#include "LimitPresetsChains.hpp"

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    v.push_back({ "Default", {} });
    v.push_back({ "Streaming -14 LUFS", { { "gain", 4 }, { "ceiling", -1 }, { "style", kTransparent }, { "lookahead", 5 }, { "attack", 60 }, { "release", 160 }, { "target", 1 } } });
    v.push_back({ "Apple Music -16 LUFS", { { "gain", 2.5f }, { "ceiling", -1 }, { "style", kTransparent }, { "release", 200 }, { "target", 2 } } });
    v.push_back({ "Allround loud", { { "gain", 7 }, { "ceiling", -1 }, { "style", kAllround }, { "release", 90 }, { "target", 1 } } });
    v.push_back({ "Punchy drums", { { "gain", 6 }, { "ceiling", -1 }, { "style", kPunchy }, { "lookahead", 3 }, { "attack", 30 }, { "release", 60 }, { "link_transients", 60 } } });
    v.push_back({ "Dynamic acoustic", { { "gain", 3 }, { "ceiling", -1 }, { "style", kDynamic }, { "lookahead", 6 }, { "attack", 70 }, { "release", 250 } } });
    v.push_back({ "Modern pop loud", { { "gain", 9 }, { "ceiling", -1 }, { "style", kModern }, { "oversampling", 2 }, { "release", 80 }, { "target", 3 } } });
    v.push_back({ "Aggressive EDM", { { "gain", 11 }, { "ceiling", -0.3f }, { "style", kAggressive }, { "oversampling", 2 }, { "lookahead", 2 }, { "attack", 20 }, { "release", 40 }, { "target", 3 } } });
    v.push_back({ "Club master -9 LUFS", { { "gain", 10 }, { "ceiling", -0.5f }, { "style", kModern }, { "oversampling", 2 }, { "release", 60 }, { "target", 3 } } });
    v.push_back({ "Safe broadcast", { { "gain", 3 }, { "ceiling", -2 }, { "style", kSafe }, { "lookahead", 8 }, { "attack", 100 }, { "release", 400 }, { "target", 6 }, { "target_custom", -23 } } });
    v.push_back({ "CD master 16-bit", { { "gain", 8 }, { "ceiling", -0.3f }, { "style", kAllround }, { "oversampling", 2 }, { "dither", 1 }, { "noise_shaping", 1 }, { "target", 4 } } });
    v.push_back({ "Gentle peak catcher", { { "gain", 0 }, { "ceiling", -1 }, { "style", kSafe }, { "lookahead", 2 }, { "release", 50 }, { "target", 0 } } });
    v.push_back({ "Tight low end", { { "gain", 6 }, { "ceiling", -1 }, { "style", kSafe }, { "lookahead", 10 }, { "attack", 100 }, { "release", 300 }, { "link_release", 100 } } });
    v.push_back({ "YouTube -14 LUFS", { { "gain", 5 }, { "ceiling", -1 }, { "style", kAllround }, { "target", 5 } } });
    addSourcePresets(v);   // LimitPresetsSources.hpp: vocals, drums, bass, ... (SOURCES)
    addChainPresets(v);    // LimitPresetsChains.hpp: matching mastering chains (CHAINS)
    return v;
}

}} // namespace pump::limit
