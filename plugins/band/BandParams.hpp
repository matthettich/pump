// pump BAND: parameters and factory presets.
#pragma once

#include "PumpParams.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace band {

constexpr int kNumBands = 4;

enum Global : int {
    kOutGain = 0, kMix, kXoverMode, kNumBandsP, kXover1, kXover2, kXover3, kLookahead, kDetect, kLink, kAutoMakeup,
    kBypass, kListen,   // kListen: Alt-solo from the display (hidden, UI-driven): 0 = off, 1..4 = that band only
    kNumGlobals
};
enum BandField : int {
    kMode = 0, kThresh, kRatio, kAttack, kRelease, kKnee, kRange, kMakeup, kSolo, kMute, kBandBypass,
    kBandStride
};
constexpr int kNumParams = kNumGlobals + kNumBands * kBandStride;

inline int bandParam(int band, int field) { return kNumGlobals + band * kBandStride + field; }
inline int bandOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) / kBandStride; }
inline int fieldOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) % kBandStride; }
inline int xoverParam(int k) { return kXover1 + k; }

enum DynMode : int { kCompress = 0, kExpand, kUpward };

static const char* const kModeNames[] = { "Compress", "Expand", "Upward" };
static const char* const kXoverNames[] = { "Analog", "Linear Phase" };
static const char* const kDetectNames[] = { "Peak", "RMS" };

inline const std::vector<ParamSpec>& params()
{
    static std::deque<std::string> pool;
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    auto keep = [](const std::string& s) -> const char* { pool.push_back(s); return pool.back().c_str(); };
    const unsigned A = kPAutomatable;
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "xover_mode", "Crossover Mode", "Crossover", 0, 1, 0, PUnit::None, PScale::Linear, kPInteger, kXoverNames, 1 });
    v.push_back({ "bands", "Bands", "Bands", 1, 4, 3, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "xover1", "Crossover 1", "Xover 1", 20, 20000, 120, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "xover2", "Crossover 2", "Xover 2", 20, 20000, 2000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "xover3", "Crossover 3", "Xover 3", 20, 20000, 8000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lookahead", "Lookahead", "Lookahead", 0, 10, 0, PUnit::ms, PScale::Linear, 0, nullptr, 1 });
    v.push_back({ "detection", "Detection", "Detect", 0, 1, 0, PUnit::None, PScale::Linear, A | kPInteger, kDetectNames, 1 });
    v.push_back({ "stereo_link", "Stereo Link", "Link", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "auto_makeup", "Auto Makeup", "Auto Makeup", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    v.push_back({ "listen_band", "Listen Band", "Listen", 0, 4, 0, PUnit::None, PScale::Linear, kPInteger | kPHidden, nullptr, 1 });
    for (int b = 0; b < kNumBands; ++b)
    {
        const std::string n = std::to_string(b + 1), p = "b" + n + "_", N = "Band " + n + " ";
        v.push_back({ keep(p + "mode"), keep(N + "Mode"), keep(N + "Mode"), 0, 2, 0, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
        v.push_back({ keep(p + "thresh"), keep(N + "Threshold"), keep(N + "Thresh"), -60, 0, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "ratio"), keep(N + "Ratio"), keep(N + "Ratio"), 1, 20, 2, PUnit::Ratio, PScale::Power, A, nullptr, 2.5f });
        v.push_back({ keep(p + "attack"), keep(N + "Attack"), keep(N + "Attack"), 0.1f, 500, 15, PUnit::ms, PScale::Log, A, nullptr, 1 });
        v.push_back({ keep(p + "release"), keep(N + "Release"), keep(N + "Release"), 5, 5000, 150, PUnit::ms, PScale::Log, A, nullptr, 1 });
        v.push_back({ keep(p + "knee"), keep(N + "Knee"), keep(N + "Knee"), 0, 30, 6, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "range"), keep(N + "Range"), keep(N + "Range"), 0, 60, 24, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "makeup"), keep(N + "Makeup"), keep(N + "Makeup"), -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "solo"), keep(N + "Solo"), keep(N + "Solo"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPNoUndo, nullptr, 1 });
        v.push_back({ keep(p + "mute"), keep(N + "Mute"), keep(N + "Mute"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
        v.push_back({ keep(p + "bypass"), keep(N + "Bypass"), keep(N + "Bypass"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    }
    return v;
}

// preset helpers: one band's settings
struct BP { int band; int mode; float thresh, ratio, attack, release, knee, range, makeup; };
inline void addBand(Preset& p, const BP& b)
{
    static std::deque<std::string> pool;
    auto sym = [&](const char* f) -> const char* { pool.push_back("b" + std::to_string(b.band) + "_" + f); return pool.back().c_str(); };
    p.values.push_back({ sym("mode"), (float)b.mode });
    p.values.push_back({ sym("thresh"), b.thresh });
    p.values.push_back({ sym("ratio"), b.ratio });
    p.values.push_back({ sym("attack"), b.attack });
    p.values.push_back({ sym("release"), b.release });
    p.values.push_back({ sym("knee"), b.knee });
    p.values.push_back({ sym("range"), b.range });
    p.values.push_back({ sym("makeup"), b.makeup });
}

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto P = [&](const char* name, std::vector<PresetValue> globals, std::vector<BP> bands) {
        Preset p { name, std::move(globals) };
        for (const BP& b : bands) addBand(p, b);
        v.push_back(p);
    };
    // modes: 0 compress, 1 expand, 2 upward
    P("Default", {}, {});
    // Thresholds are set for mixes that peak around -6 dBFS (typical pre-master levels); RMS band levels there sit
    // near -23 (lows), -31 (mids) and -42..-45 dBFS (highs).
    P("Master: gentle glue", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 2500 }, { "detection", 1 } },
      { { 1, 0, -27, 1.6f, 30, 200, 10, 6, 0.5f }, { 2, 0, -33, 1.5f, 20, 160, 10, 6, 0.5f }, { 3, 0, -42, 1.5f, 10, 120, 10, 6, 0.5f } });
    P("Master: 4-band control", { { "bands", 4 }, { "xover1", 120 }, { "xover2", 1000 }, { "xover3", 6000 }, { "detection", 1 } },
      { { 1, 0, -26, 2.0f, 40, 250, 8, 8, 1.0f }, { 2, 0, -33, 1.8f, 25, 180, 8, 8, 1.0f },
        { 3, 0, -44, 1.8f, 15, 140, 8, 8, 1.0f }, { 4, 0, -48, 2.0f, 8, 100, 8, 8, 1.0f } });
    P("Master: linear phase", { { "bands", 4 }, { "xover_mode", 1 }, { "xover1", 100 }, { "xover2", 800 }, { "xover3", 5000 }, { "detection", 1 }, { "lookahead", 3 } },
      { { 1, 0, -26, 1.8f, 40, 250, 10, 6, 0.8f }, { 2, 0, -33, 1.6f, 25, 180, 10, 6, 0.8f },
        { 3, 0, -43, 1.6f, 15, 140, 10, 6, 0.8f }, { 4, 0, -47, 1.8f, 8, 100, 10, 6, 0.8f } });
    P("Low end: tighten", { { "bands", 2 }, { "xover1", 160 } },
      { { 1, 0, -24, 3.0f, 25, 120, 6, 12, 1.5f } });
    P("De-harsh 2-6k", { { "bands", 4 }, { "xover1", 120 }, { "xover2", 2200 }, { "xover3", 6500 } },
      { { 3, 0, -38, 4.0f, 3, 80, 6, 8, 0.0f } });
    P("De-ess (high band)", { { "bands", 2 }, { "xover1", 5500 }, { "lookahead", 2 } },
      { { 2, 0, -34, 6.0f, 0.5f, 60, 4, 10, 0.0f } });
    P("Upward: lift detail", { { "bands", 3 }, { "xover1", 200 }, { "xover2", 4000 }, { "detection", 1 } },
      { { 1, 2, -28, 1.6f, 30, 300, 10, 6, 0.0f }, { 2, 2, -34, 1.8f, 20, 250, 10, 6, 0.0f }, { 3, 2, -44, 2.0f, 10, 200, 10, 8, 0.0f } });
    P("Expand: clean low rumble", { { "bands", 2 }, { "xover1", 90 } },
      { { 1, 1, -36, 2.0f, 5, 150, 6, 12, 0.0f } });
    P("Parallel punch (mix 40%)", { { "bands", 3 }, { "xover1", 120 }, { "xover2", 3000 }, { "mix", 40 } },
      { { 1, 0, -30, 6.0f, 10, 100, 4, 24, 6.0f }, { 2, 0, -36, 6.0f, 5, 80, 4, 24, 6.0f }, { 3, 0, -44, 6.0f, 2, 60, 4, 24, 6.0f } });
    P("Vocal: smooth mids", { { "bands", 3 }, { "xover1", 250 }, { "xover2", 4500 }, { "detection", 1 } },
      { { 2, 0, -33, 2.5f, 12, 140, 8, 9, 1.0f }, { 3, 0, -44, 3.0f, 4, 90, 6, 8, 0.0f } });
    P("Drum bus: snap", { { "bands", 3 }, { "xover1", 100 }, { "xover2", 2500 }, { "stereo_link", 50 } },
      { { 1, 1, -30, 1.5f, 1, 90, 6, 6, 0.0f }, { 2, 0, -30, 3.0f, 20, 90, 4, 10, 2.0f }, { 3, 2, -44, 1.8f, 5, 120, 8, 6, 0.0f } });
    return v;
}

}} // namespace pump::band
