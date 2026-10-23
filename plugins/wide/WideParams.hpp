// pump WIDE: parameters and factory presets.
#pragma once

#include "PumpParams.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace wide {

constexpr int kNumBands = 4;

enum Global : int {
    kOutGain = 0, kBalance, kMonoBass, kMonoFreq, kMonoSlope, kXoverMode, kNumBandsP, kXover1, kXover2, kXover3,
    kBypass, kListen,   // kListen: unused placeholder (kept so parameter indices / ids never shift). The Alt-hold
                        // listen goes through the plugin's listenHold atomic: a hidden parameter is saved with
                        // the session, and a stale value would silence the plugin
    kNumGlobals
};
enum BandField : int { kWidth = 0, kStereoize, kSolo, kMute, kBandBypass, kBandStride };
constexpr int kNumParams = kNumGlobals + kNumBands * kBandStride;

inline int bandParam(int band, int field) { return kNumGlobals + band * kBandStride + field; }
inline int bandOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) / kBandStride; }
inline int fieldOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) % kBandStride; }
inline int xoverParam(int k) { return kXover1 + k; }

static const char* const kXoverNames[] = { "Analog", "Linear Phase" };
static const char* const kSlopeNames[] = { "24 dB/oct", "48 dB/oct" };

inline const std::vector<ParamSpec>& params()
{
    static std::deque<std::string> pool;
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    auto keep = [](const std::string& s) -> const char* { pool.push_back(s); return pool.back().c_str(); };
    const unsigned A = kPAutomatable;
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "balance", "Balance", "Balance", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mono_bass", "Mono Bass", "Mono Bass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "mono_freq", "Mono Bass Frequency", "Mono Freq", 20, 500, 120, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mono_slope", "Mono Bass Slope", "Slope", 0, 1, 0, PUnit::None, PScale::Linear, A | kPInteger, kSlopeNames, 1 });
    v.push_back({ "xover_mode", "Crossover Mode", "Crossover", 0, 1, 0, PUnit::None, PScale::Linear, kPInteger, kXoverNames, 1 });
    v.push_back({ "bands", "Bands", "Bands", 1, 4, 4, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "xover1", "Crossover 1", "Xover 1", 20, 20000, 120, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "xover2", "Crossover 2", "Xover 2", 20, 20000, 1000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "xover3", "Crossover 3", "Xover 3", 20, 20000, 6000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    v.push_back({ "listen_band", "Listen Band", "Listen", 0, 4, 0, PUnit::None, PScale::Linear, kPInteger | kPHidden, nullptr, 1 });   // unused placeholder (see kListen)
    for (int b = 0; b < kNumBands; ++b)
    {
        const std::string n = std::to_string(b + 1), p = "b" + n + "_", N = "Band " + n + " ";
        v.push_back({ keep(p + "width"), keep(N + "Width"), keep(N + "Width"), 0, 200, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "stereoize"), keep(N + "Stereoize"), keep(N + "Stereoize"), 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "solo"), keep(N + "Solo"), keep(N + "Solo"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPNoUndo, nullptr, 1 });
        v.push_back({ keep(p + "mute"), keep(N + "Mute"), keep(N + "Mute"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
        v.push_back({ keep(p + "bypass"), keep(N + "Bypass"), keep(N + "Bypass"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    }
    return v;
}

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    // widths and stereoize amounts per band (4 values each), plus globals
    auto P = [&](const char* name, std::vector<PresetValue> globals, std::vector<float> w, std::vector<float> st = {}) {
        Preset p { name, std::move(globals) };
        static std::deque<std::string> pool;
        for (size_t b = 0; b < w.size(); ++b) { pool.push_back("b" + std::to_string(b + 1) + "_width"); p.values.push_back({ pool.back().c_str(), w[b] }); }
        for (size_t b = 0; b < st.size(); ++b) { pool.push_back("b" + std::to_string(b + 1) + "_stereoize"); p.values.push_back({ pool.back().c_str(), st[b] }); }
        v.push_back(p);
    };
    P("Default", {}, {});
    P("Master: subtle width", { { "mono_bass", 1 }, { "mono_freq", 90 } }, { 90, 100, 112, 120 });
    P("Master: mono bass 120 Hz", { { "mono_bass", 1 }, { "mono_freq", 120 } }, {});
    P("Wide highs, tight lows", { { "mono_bass", 1 }, { "mono_freq", 100 } }, { 60, 100, 125, 145 }, { 0, 0, 0, 15 });
    P("Air: stereoize top", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 7000 } }, { 100, 100, 130 }, { 0, 0, 35 });
    P("Mono synth: stereoize", { { "bands", 3 }, { "xover1", 250 }, { "xover2", 3000 }, { "mono_bass", 1 }, { "mono_freq", 140 } }, { 100, 110, 120 }, { 0, 45, 55 });
    P("Focus the centre", {}, { 70, 70, 75, 80 });
    P("Extra wide (check mono!)", { { "mono_bass", 1 }, { "mono_freq", 150 } }, { 50, 140, 165, 180 }, { 0, 10, 20, 25 });
    P("Tame phasey highs", { { "bands", 2 }, { "xover1", 5000 } }, { 100, 65 });
    P("Mono check (all bands 0%)", {}, { 0, 0, 0, 0 });
    P("Linear phase: gentle", { { "xover_mode", 1 }, { "mono_bass", 1 }, { "mono_freq", 80 }, { "mono_slope", 1 } }, { 85, 100, 110, 118 });
    return v;
}

}} // namespace pump::wide
