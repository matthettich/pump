// pump HEAT: parameters and factory presets.
#pragma once

#include "PumpParams.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace heat {

constexpr int kNumBands = 4;

enum Global : int {
    kOutGain = 0, kMix, kOversampling, kAutoGain, kXoverMode, kNumBandsP, kXover1, kXover2, kXover3,
    kBypass, kListen,   // kListen: Alt-solo from the display (hidden, UI-driven): 0 = off, 1..4 = that band only
    kNumGlobals
};
enum BandField : int { kMode = 0, kDrive, kBandMix, kLevel, kSolo, kMute, kBandBypass, kBandStride };
constexpr int kNumParams = kNumGlobals + kNumBands * kBandStride;

inline int bandParam(int band, int field) { return kNumGlobals + band * kBandStride + field; }
inline int bandOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) / kBandStride; }
inline int fieldOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) % kBandStride; }
inline int xoverParam(int k) { return kXover1 + k; }

enum SatType : int { kWarm = 0, kTape, kTube, kTriode, kRetro, kClip, kNumTypes };
static const char* const kModeNames[] = { "Warm", "Tape", "Tube", "Triode", "Retro", "Clip" };
static const char* const kOsNames[] = { "1x", "2x", "4x", "8x" };
static const char* const kXoverNames[] = { "Analog", "Linear Phase" };

inline const std::vector<ParamSpec>& params()
{
    static std::deque<std::string> pool;
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    auto keep = [](const std::string& s) -> const char* { pool.push_back(s); return pool.back().c_str(); };
    const unsigned A = kPAutomatable;
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "oversampling", "Oversampling", "Oversampling", 0, 3, 2, PUnit::None, PScale::Linear, kPInteger, kOsNames, 1 });
    v.push_back({ "auto_gain", "Auto Gain", "Auto Gain", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "xover_mode", "Crossover Mode", "Crossover", 0, 1, 0, PUnit::None, PScale::Linear, kPInteger, kXoverNames, 1 });
    v.push_back({ "bands", "Bands", "Bands", 1, 4, 3, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "xover1", "Crossover 1", "Xover 1", 20, 20000, 150, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "xover2", "Crossover 2", "Xover 2", 20, 20000, 3000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "xover3", "Crossover 3", "Xover 3", 20, 20000, 9000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    v.push_back({ "listen_band", "Listen Band", "Listen", 0, 4, 0, PUnit::None, PScale::Linear, kPInteger | kPHidden, nullptr, 1 });
    for (int b = 0; b < kNumBands; ++b)
    {
        const std::string n = std::to_string(b + 1), p = "b" + n + "_", N = "Band " + n + " ";
        v.push_back({ keep(p + "mode"), keep(N + "Mode"), keep(N + "Mode"), 0, 5, 0, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
        v.push_back({ keep(p + "drive"), keep(N + "Drive"), keep(N + "Drive"), 0, 24, 6, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "mix"), keep(N + "Mix"), keep(N + "Mix"), 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "level"), keep(N + "Level"), keep(N + "Level"), -12, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "solo"), keep(N + "Solo"), keep(N + "Solo"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPNoUndo, nullptr, 1 });
        v.push_back({ keep(p + "mute"), keep(N + "Mute"), keep(N + "Mute"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
        v.push_back({ keep(p + "bypass"), keep(N + "Bypass"), keep(N + "Bypass"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    }
    return v;
}

// preset helpers: one band's settings
struct BP { int band; int mode; float drive, mix, level; };
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto P = [&](const char* name, std::vector<PresetValue> globals, std::vector<BP> bands) {
        static std::deque<std::string> pool;
        Preset p { name, std::move(globals) };
        for (const BP& b : bands)
        {
            auto sym = [&](const char* f) -> const char* { pool.push_back("b" + std::to_string(b.band) + "_" + f); return pool.back().c_str(); };
            p.values.push_back({ sym("mode"), (float)b.mode });
            p.values.push_back({ sym("drive"), b.drive });
            p.values.push_back({ sym("mix"), b.mix });
            p.values.push_back({ sym("level"), b.level });
        }
        v.push_back(p);
    };
    // modes: 0 warm 1 tape 2 tube 3 triode 4 retro 5 clip
    P("Default", {}, {});
    P("Master: warm glue", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 3000 } },
      { { 1, 1, 5, 60, 0 }, { 2, 0, 4, 50, 0 }, { 3, 0, 3, 35, 0 } });
    P("Master: tape", { { "bands", 3 }, { "xover1", 120 }, { "xover2", 5000 } },
      { { 1, 1, 8, 80, 0 }, { 2, 1, 6, 70, 0 }, { 3, 1, 4, 50, 0 } });
    P("Exciter: air", { { "bands", 3 }, { "xover1", 200 }, { "xover2", 6000 }, { "mix", 60 } },
      { { 1, 0, 0, 0, 0 }, { 2, 0, 0, 0, 0 }, { 3, 2, 14, 100, 1.5f } });
    P("Exciter: presence", { { "bands", 4 }, { "xover1", 150 }, { "xover2", 1500 }, { "xover3", 6000 } },
      { { 1, 0, 0, 0, 0 }, { 2, 0, 0, 0, 0 }, { 3, 3, 10, 55, 0.5f }, { 4, 2, 6, 40, 0 } });
    P("Bass: tube weight", { { "bands", 2 }, { "xover1", 180 } },
      { { 1, 2, 12, 70, 0.5f }, { 2, 0, 0, 0, 0 } });
    P("Bass: retro grit", { { "bands", 3 }, { "xover1", 100 }, { "xover2", 700 } },
      { { 1, 4, 9, 60, 0 }, { 2, 4, 12, 70, 0 }, { 3, 0, 0, 0, 0 } });
    P("Drums: clip transients", { { "bands", 3 }, { "xover1", 120 }, { "xover2", 4000 }, { "oversampling", 3 } },
      { { 1, 5, 6, 100, 0 }, { 2, 5, 8, 100, 0 }, { 3, 5, 4, 80, 0 } });
    P("Vocal: triode warmth", { { "bands", 3 }, { "xover1", 250 }, { "xover2", 4000 } },
      { { 1, 0, 0, 0, 0 }, { 2, 3, 9, 60, 0 }, { 3, 2, 6, 45, 0 } });
    P("Lo-fi: crushed mids", { { "bands", 3 }, { "xover1", 300 }, { "xover2", 2500 }, { "auto_gain", 1 } },
      { { 1, 0, 2, 50, -2 }, { 2, 5, 22, 100, 0 }, { 3, 4, 16, 100, -3 } });
    P("Parallel heat (mix 35%)", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 3000 }, { "mix", 35 } },
      { { 1, 1, 16, 100, 0 }, { 2, 2, 18, 100, 0 }, { 3, 0, 16, 100, 0 } });
    P("Linear phase: gentle", { { "bands", 3 }, { "xover_mode", 1 }, { "xover1", 120 }, { "xover2", 4000 } },
      { { 1, 1, 6, 60, 0 }, { 2, 0, 5, 50, 0 }, { 3, 2, 4, 40, 0 } });
    return v;
}

}} // namespace pump::heat
