// pump EQ: parameters and factory presets.
#pragma once

#include "PumpParams.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace eq {

constexpr int kNumBands = 24;

enum Global : int {
    kOutGain = 0, kGainScale, kAutoGain, kPhase, kLinQuality, kDynAttack, kDynRelease, kAnalyzer, kSolo, kBypass,
    kAudition, kAuditionFreq, kAuditionQ,   // Alt-solo: hear only the region under the mouse (UI-driven, hidden)
    kNumGlobals
};
enum BandField : int {
    kUsed = 0, kBandBypass, kShape, kFreq, kGain, kQ, kSlope, kPlace, kDyn, kDynRange, kDynThresh,
    kBandStride
};
constexpr int kNumParams = kNumGlobals + kNumBands * kBandStride;

inline int bandParam(int band, int field) { return kNumGlobals + band * kBandStride + field; }
inline int bandOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) / kBandStride; }
inline int fieldOf(int param) { return param < kNumGlobals ? -1 : (param - kNumGlobals) % kBandStride; }

static const char* const kShapeNames[] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf" };
static const char* const kSlopeNames[] = { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
static const int kSlopeValues[] = { 6, 12, 18, 24, 30, 36, 48, 72, 96 };
static const char* const kPlaceNames[] = { "Stereo", "Left", "Right", "Mid", "Side" };
static const char* const kPhaseNames[] = { "Zero Latency", "Linear Phase" };
static const char* const kQualityNames[] = { "Low", "Medium", "High", "Max" };
static const char* const kAnalyzerNames[] = { "Off", "Pre", "Post", "Pre+Post" };
static const char* const kOnOff[] = { "Off", "On" };

enum Place : int { kStereo = 0, kLeft, kRight, kMid, kSide };

inline const std::vector<ParamSpec>& params()
{
    static std::deque<std::string> pool;   // stable storage for generated names
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    auto keep = [](const std::string& s) -> const char* { pool.push_back(s); return pool.back().c_str(); };
    const unsigned A = kPAutomatable;
    v.push_back({ "out_gain", "Output Gain", "Output", -36, 36, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "gain_scale", "Gain Scale", "Scale", -100, 200, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "auto_gain", "Auto Gain", "Auto Gain", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "phase", "Phase Mode", "Phase", 0, 1, 0, PUnit::None, PScale::Linear, kPInteger, kPhaseNames, 1 });
    v.push_back({ "lin_quality", "Linear Phase Quality", "Quality", 0, 3, 1, PUnit::None, PScale::Linear, kPInteger, kQualityNames, 1 });
    v.push_back({ "dyn_attack", "Dynamic Attack", "Attack", 0.5f, 200, 10, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "dyn_release", "Dynamic Release", "Release", 10, 2000, 120, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "analyzer", "Analyzer", "Analyzer", 0, 3, 3, PUnit::None, PScale::Linear, kPInteger, kAnalyzerNames, 1 });
    v.push_back({ "solo", "Solo Band", "Solo", 0, kNumBands, 0, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    v.push_back({ "audition", "Audition", "Audition", 0, 1, 0, PUnit::None, PScale::Linear, kPBoolean | kPHidden, nullptr, 1 });
    v.push_back({ "audition_freq", "Audition Frequency", "Aud Freq", 20, 20000, 1000, PUnit::Hz, PScale::Log, kPHidden, nullptr, 1 });
    v.push_back({ "audition_q", "Audition Q", "Aud Q", 0.3f, 12, 2, PUnit::None, PScale::Log, kPHidden, nullptr, 1 });
    for (int b = 0; b < kNumBands; ++b)
    {
        const std::string n = std::to_string(b + 1), p = "b" + n + "_", N = "Band " + n + " ";
        v.push_back({ keep(p + "used"), keep(N + "Used"), keep(N + "Used"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
        v.push_back({ keep(p + "bypass"), keep(N + "Bypass"), keep(N + "Bypass"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
        v.push_back({ keep(p + "shape"), keep(N + "Shape"), keep(N + "Shape"), 0, 7, 0, PUnit::None, PScale::Linear, A | kPInteger, kShapeNames, 1 });
        v.push_back({ keep(p + "freq"), keep(N + "Frequency"), keep(N + "Freq"), 10, 30000, 1000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
        v.push_back({ keep(p + "gain"), keep(N + "Gain"), keep(N + "Gain"), -30, 30, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "q"), keep(N + "Q"), keep(N + "Q"), 0.025f, 40, 1, PUnit::None, PScale::Log, A, nullptr, 1 });
        v.push_back({ keep(p + "slope"), keep(N + "Slope"), keep(N + "Slope"), 0, 8, 1, PUnit::None, PScale::Linear, A | kPInteger, kSlopeNames, 1 });
        v.push_back({ keep(p + "place"), keep(N + "Stereo Placement"), keep(N + "Place"), 0, 4, 0, PUnit::None, PScale::Linear, A | kPInteger, kPlaceNames, 1 });
        v.push_back({ keep(p + "dyn"), keep(N + "Dynamic"), keep(N + "Dyn"), 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
        v.push_back({ keep(p + "dyn_range"), keep(N + "Dynamic Range"), keep(N + "Range"), -30, 30, -6, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ keep(p + "dyn_thresh"), keep(N + "Threshold"), keep(N + "Thresh"), -80, 0, -30, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    }
    return v;
}

// helpers for preset tables
struct B { int band; int shape; float freq; float gain; float q; int slope; int place; int dyn; float range; float thresh; };
inline void addBand(Preset& p, const B& b)
{
    static std::deque<std::string> pool;
    auto sym = [&](const char* f) -> const char* { pool.push_back("b" + std::to_string(b.band) + "_" + f); return pool.back().c_str(); };
    p.values.push_back({ sym("used"), 1 });
    p.values.push_back({ sym("shape"), (float)b.shape });
    p.values.push_back({ sym("freq"), b.freq });
    p.values.push_back({ sym("gain"), b.gain });
    p.values.push_back({ sym("q"), b.q });
    p.values.push_back({ sym("slope"), (float)b.slope });
    p.values.push_back({ sym("place"), (float)b.place });
    p.values.push_back({ sym("dyn"), (float)b.dyn });
    p.values.push_back({ sym("dyn_range"), b.range });
    p.values.push_back({ sym("dyn_thresh"), b.thresh });
}

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    // shapes: 0 bell 1 lowshelf 2 highshelf 3 lowcut 4 highcut 5 notch 6 bandpass 7 tilt
    // slope index: 0=6 1=12 2=18 3=24 4=30 5=36 6=48 7=72 8=96
    auto P = [&](const char* name, std::vector<B> bands, std::vector<PresetValue> extra = {}) {
        Preset p { name, {} };
        for (const B& b : bands) addBand(p, b);
        for (const auto& e : extra) p.values.push_back(e);
        v.push_back(p);
    };
    P("Default", {});
    P("Master: gentle polish", { { 1, 3, 25, 0, 0.71f, 3, 0, 0, -6, -30 }, { 2, 1, 90, 0.8f, 0.71f, 1, 0, 0, -6, -30 },
        { 3, 0, 320, -0.8f, 0.9f, 1, 0, 0, -6, -30 }, { 4, 2, 11000, 1.2f, 0.71f, 1, 0, 0, -6, -30 } });
    P("Master: air + body", { { 1, 3, 22, 0, 0.71f, 5, 0, 0, -6, -30 }, { 2, 1, 70, 1.5f, 0.6f, 1, 0, 0, -6, -30 },
        { 3, 2, 14000, 2.5f, 0.6f, 1, 0, 0, -6, -30 } });
    P("Master: tilt brighter", { { 1, 7, 900, 2.0f, 0.71f, 0, 0, 0, -6, -30 } });
    P("Master: tilt warmer", { { 1, 7, 900, -2.0f, 0.71f, 0, 0, 0, -6, -30 } });
    P("Master: tame harsh 2-5k", { { 1, 0, 3200, -1.5f, 1.4f, 1, 0, 1, -3.5f, -32 } });
    P("Low end: tighten (dynamic)", { { 1, 3, 28, 0, 0.71f, 6, 0, 0, -6, -30 }, { 2, 0, 65, 0, 1.2f, 1, 0, 1, -4.5f, -24 },
        { 3, 0, 220, -1.0f, 1.1f, 1, 0, 1, -3.0f, -28 } });
    P("Low end: mono-safe side cut", { { 1, 3, 140, 0, 0.71f, 3, kSide, 0, -6, -30 } });
    P("Wide air (side shelf)", { { 1, 2, 9000, 2.5f, 0.71f, 1, kSide, 0, -6, -30 }, { 2, 3, 120, 0, 0.71f, 1, kSide, 0, -6, -30 } });
    P("De-mud", { { 1, 0, 300, -2.5f, 1.0f, 1, 0, 0, -6, -30 }, { 2, 0, 550, -1.0f, 1.6f, 1, 0, 0, -6, -30 } });
    P("De-ess (dynamic)", { { 1, 0, 6800, 0, 2.0f, 1, 0, 1, -8, -36 } });
    P("Vocal presence", { { 1, 3, 90, 0, 0.71f, 2, 0, 0, -6, -30 }, { 2, 0, 250, -1.5f, 1.2f, 1, 0, 0, -6, -30 },
        { 3, 0, 3000, 2.0f, 0.9f, 1, 0, 0, -6, -30 }, { 4, 2, 12000, 2.0f, 0.71f, 1, 0, 0, -6, -30 } });
    P("Kick: punch", { { 1, 0, 60, 3.0f, 1.2f, 1, 0, 0, -6, -30 }, { 2, 0, 380, -4.0f, 1.5f, 1, 0, 0, -6, -30 },
        { 3, 0, 4000, 2.5f, 1.0f, 1, 0, 0, -6, -30 } });
    P("Telephone", { { 1, 3, 400, 0, 0.71f, 3, 0, 0, -6, -30 }, { 2, 4, 3200, 0, 0.71f, 3, 0, 0, -6, -30 },
        { 3, 0, 1400, 6.0f, 1.0f, 1, 0, 0, -6, -30 } });
    P("Rumble + hiss cleanup", { { 1, 3, 30, 0, 0.71f, 6, 0, 0, -6, -30 }, { 2, 4, 18500, 0, 0.71f, 6, 0, 0, -6, -30 } });
    P("Linear: master low cut", { { 1, 3, 30, 0, 0.71f, 4, 0, 0, -6, -30 } }, { { "phase", 1 }, { "lin_quality", 2 } });
    return v;
}

}} // namespace pump::eq
