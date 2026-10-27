// pump TONE: parameters and factory presets (shared by the DSP and the UI; no framework code).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace tone {

enum Param : int {
    kRecover = 0, kTame, kBias, kBrighten, kBody, kSpeed, kAutoLevel, kOutGain, kMix, kQuality, kDelta, kBypass,
    kAudition, kAuditionFreq, kAuditionQ,   // unused placeholders (kept so parameter indices/ids never shift): Alt-hold
                                            // listen is driven through PluginTone's atomics instead
    kNumParams
};

static const char* const kQualityNames[] = { "2048", "4096" };
static const int kQualitySizes[] = { 2048, 4096 };

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    v.push_back({ "recover", "Recover", "Recover", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tame", "Tame", "Tame", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bias", "Bias", "Bias", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "brighten", "Brighten", "Brighten", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "body", "Body", "Body", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "speed", "Speed", "Speed", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "auto_level", "Auto Level", "Auto Level", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "quality", "Quality (FFT size)", "Quality", 0, 1, 0, PUnit::None, PScale::Linear, kPInteger, kQualityNames, 1 });
    v.push_back({ "delta", "Delta", "Delta", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPNoUndo, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    // placeholders (see the enum): symbols and order must not change
    v.push_back({ "audition", "Audition", "Audition", 0, 1, 0, PUnit::None, PScale::Linear, kPBoolean | kPHidden | kPNoUndo, nullptr, 1 });
    v.push_back({ "audition_freq", "Audition Frequency", "Aud Freq", 20, 20000, 1000, PUnit::Hz, PScale::Log, kPHidden | kPNoUndo, nullptr, 1 });
    v.push_back({ "audition_q", "Audition Q", "Aud Q", 0.3f, 12, 2, PUnit::None, PScale::Log, kPHidden | kPNoUndo, nullptr, 1 });
    return v;
    }();
    return v;
}

#include "TonePresetsSources.hpp"
#include "TonePresetsChains.hpp"

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    v.push_back({ "Default", {} });
    v.push_back({ "Subtle master balance", { { "recover", 18 }, { "tame", 22 }, { "speed", 25 } } });
    v.push_back({ "Recover masked mids", { { "recover", 65 }, { "tame", 15 }, { "bias", -35 }, { "speed", 35 } } });
    v.push_back({ "Tame harsh mix", { { "recover", 15 }, { "tame", 70 }, { "bias", 30 }, { "brighten", -40 }, { "speed", 50 } } });
    v.push_back({ "Warm & full", { { "recover", 35 }, { "tame", 35 }, { "brighten", -35 }, { "body", 45 }, { "speed", 30 } } });
    v.push_back({ "Bright & open", { { "recover", 40 }, { "tame", 30 }, { "brighten", 45 }, { "body", -15 }, { "speed", 35 } } });
    v.push_back({ "Dense mix clarity", { { "recover", 50 }, { "tame", 55 }, { "speed", 55 }, { "quality", 1 } } });
    v.push_back({ "Lively (fast)", { { "recover", 40 }, { "tame", 40 }, { "speed", 85 } } });
    v.push_back({ "Gentle tilt only", { { "recover", 0 }, { "tame", 0 }, { "brighten", 25 }, { "body", 15 } } });
    v.push_back({ "Heavy rebalance", { { "recover", 80 }, { "tame", 80 }, { "speed", 40 }, { "quality", 1 } } });
    addSourcePresets(v);   // TonePresetsSources.hpp: vocals, drums, bass, ... (SOURCES)
    addChainPresets(v);    // TonePresetsChains.hpp: matching mastering chains (CHAINS)
    return v;
    }();
    return v;
}

}} // namespace pump::tone
