// pump SWIRL: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pump { namespace swirl {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kMode = 0, kVariant, kRate, kSync, kDivision, kDepth, kManual, kFeedback,
    kSpread, kTone, kWarmth, kMix, kOutGain, kBypass,
    kNumParams
};

// The types. Each has four variants (the keys under the type keys), named per type.
//   Ensemble  two bucket-brigade lines swept in opposition, cross-mixed into both sides: wide, still, no wobble
//   Analog    a bucket-brigade chorus pedal / synth chorus: one line per side, triangle sweep
//   Rack      a clean digital multi-voice chorus (2-6 voices per side, sine sweeps at spread phases)
//   Flanger   tape-style flanging: classic, through-zero (the swept copy crosses the fixed one), negative, jet
//   Phaser    4 / 6 / 8 / 12 all-pass stages with feedback
//   Lamp      a lamp-and-photocell phaser: four unevenly spaced stages, the lamp's lag shapes the sweep
enum Mode : int { kEnsemble = 0, kAnalog, kRack, kFlanger, kPhaser, kLamp, kNumModes };
static const char* const kModeNames[] = { "Ensemble", "Analog", "Rack", "Flanger", "Phaser", "Lamp" };
static const char* const kModeKeys[] = { "ENSEMBLE", "ANALOG", "RACK", "FLANGER", "PHASER", "LAMP" };
static const char* const kVariantNames[] = { "1", "2", "3", "4" };
inline const char* variantKey(int mode, int v)
{
    static const char* const k[kNumModes][4] = {
        { "1", "2", "3", "4" },
        { "SLOW", "FAST", "SHIMMER", "VIBRATO" },
        { "2 VOICE", "3 VOICE", "4 VOICE", "6 VOICE" },
        { "CLASSIC", "THRU-0", "NEGATIVE", "JET" },
        { "4 STAGE", "6 STAGE", "8 STAGE", "12 STAGE" },
        { "CHORUS", "VIBRATO", "DEEP", "SLOW LAMP" },
    };
    return k[mode < 0 ? 0 : (mode >= kNumModes ? kNumModes - 1 : mode)][v < 0 ? 0 : (v > 3 ? 3 : v)];
}

// What clicking a type / variant also sets (the type's classic settings): rate Hz, depth %, manual %, feedback %, mix %
struct Classic { float rate, depth, manual, feedback, mix; };
inline Classic classic(int mode, int v)
{
    static const Classic c[kNumModes][4] = {
        { { 0.25f, 30, 40, 0, 50 }, { 0.45f, 45, 45, 0, 50 }, { 0.50f, 65, 50, 0, 50 }, { 0.50f, 90, 55, 0, 50 } },
        { { 0.50f, 60, 45, 0, 50 }, { 0.83f, 60, 45, 0, 50 }, { 8.00f, 12, 40, 0, 50 }, { 5.00f, 45, 30, 0, 100 } },
        { { 0.60f, 40, 50, 0, 50 }, { 0.55f, 45, 55, 0, 50 }, { 0.50f, 50, 55, 10, 50 }, { 0.45f, 55, 60, 10, 50 } },
        { { 0.20f, 80, 30, 50, 50 }, { 0.15f, 90, 50, 30, 50 }, { 0.20f, 80, 30, -60, 50 }, { 0.08f, 95, 25, 85, 50 } },
        { { 0.40f, 70, 45, 40, 50 }, { 0.35f, 75, 45, 50, 50 }, { 0.30f, 80, 50, 55, 50 }, { 0.25f, 85, 50, 60, 50 } },
        { { 1.20f, 70, 50, 0, 50 }, { 4.50f, 60, 50, 0, 100 }, { 0.80f, 95, 55, 25, 50 }, { 0.50f, 85, 50, 15, 50 } },
    };
    return c[mode < 0 ? 0 : (mode >= kNumModes ? kNumModes - 1 : mode)][v < 0 ? 0 : (v > 3 ? 3 : v)];
}

constexpr float kToneOff = 20000.0f;
inline bool toneActive(double f) { return f < kToneOff - 10.0; }

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    v.push_back({ "mode", "Type", "Type", 0, kNumModes - 1, kAnalog, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
    v.push_back({ "variant", "Variant", "Variant", 0, 3, 0, PUnit::None, PScale::Linear, A | kPInteger, kVariantNames, 1 });
    v.push_back({ "rate", "Rate", "Rate", 0.02f, 12.0f, 0.5f, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sync", "Sync", "Sync", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "division", "Division", "Division", 0, kNumNotes - 1, kNote1, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "depth", "Depth", "Depth", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "manual", "Manual", "Manual", 0, 100, 45, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "feedback", "Feedback", "Feedback", -95, 95, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spread", "Spread", "Spread", 0, 180, 90, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tone", "Tone", "Tone", 1000, kToneOff, kToneOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "warmth", "Warmth", "Warmth", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. Types: 0 ensemble 1 analog 2 rack 3 flanger 4 phaser 5 lamp; variants 0..3.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto P = [&](const char* sec, const char* n, int mode, int var, std::vector<PresetValue> extra) {
        const Classic c = classic(mode, var);
        std::vector<PresetValue> p = { { "mode", (float)mode }, { "variant", (float)var }, { "rate", c.rate }, { "depth", c.depth },
                                       { "manual", c.manual }, { "feedback", c.feedback }, { "mix", c.mix } };
        for (const auto& e : extra)
        {
            bool found = false;
            for (auto& x : p) if (std::strcmp(x.symbol, e.symbol) == 0) { x.value = e.value; found = true; }
            if (!found) p.push_back(e);
        }
        v.push_back({ n, std::move(p), sec });
    };
    v.push_back({ "Default", {} });
    P(nullptr, "Ensemble 1 (still)", 0, 0, {});
    P(nullptr, "Ensemble 2", 0, 1, {});
    P(nullptr, "Ensemble 3", 0, 2, {});
    P(nullptr, "Ensemble 4 (widest)", 0, 3, {});
    P(nullptr, "Synth chorus slow", 1, 0, {});
    P(nullptr, "Synth chorus fast", 1, 1, {});
    P(nullptr, "Chorus shimmer", 1, 2, {});
    P(nullptr, "Pedal vibrato", 1, 3, {});
    P(nullptr, "Rack chorus wide", 2, 1, { { "spread", 180 } });
    P(nullptr, "Rack six voices", 2, 3, { { "spread", 180 }, { "warmth", 0 } });
    P(nullptr, "Tape flange", 3, 0, {});
    P(nullptr, "Through-zero sweep", 3, 1, {});
    P(nullptr, "Negative flange", 3, 2, {});
    P(nullptr, "Jet", 3, 3, {});
    P(nullptr, "Phaser 4", 4, 0, {});
    P(nullptr, "Phaser 12, deep", 4, 3, {});
    P(nullptr, "Lamp chorus", 5, 0, {});
    P(nullptr, "Lamp vibrato", 5, 1, {});
    P("SOURCES", "Guitar: clean chorus", 1, 0, { { "mix", 45 }, { "tone", 9000 } });
    P("SOURCES", "Guitar: lamp swirl", 5, 0, { { "rate", 2.5f } });
    P("SOURCES", "Keys: electric piano phaser", 4, 0, { { "rate", 0.3f }, { "mix", 50 } });
    P("SOURCES", "Keys: organ lamp", 5, 3, { { "rate", 5.5f } });
    P("SOURCES", "Synths: wide pad", 0, 2, {});
    P("SOURCES", "Synths: string ensemble", 2, 3, { { "rate", 0.9f }, { "depth", 35 }, { "spread", 180 }, { "mix", 60 } });
    P("SOURCES", "Bass: subtle chorus", 1, 0, { { "depth", 35 }, { "mix", 30 } });
    P("SOURCES", "Vocals: doubler", 2, 1, { { "rate", 0.3f }, { "depth", 20 }, { "manual", 70 }, { "mix", 35 }, { "spread", 180 } });
    P("SOURCES", "Drums: flanged hats", 3, 0, { { "sync", 1 }, { "division", (float)kNote2Bars }, { "mix", 40 } });
    P("SOURCES", "Bus: ensemble width", 0, 0, { { "mix", 35 } });
    P("CREATIVE", "Warble tape", 1, 3, { { "rate", 1.3f }, { "depth", 85 }, { "warmth", 70 } });
    P("CREATIVE", "Metal comb", 3, 3, { { "feedback", 95 }, { "rate", 0.05f }, { "manual", 10 } });
    P("CREATIVE", "Bar-synced phaser", 4, 2, { { "sync", 1 }, { "division", (float)kNote1 } });
    P("CREATIVE", "Underwater", 5, 2, { { "tone", 2500 }, { "rate", 0.3f } });
    P("CREATIVE", "Seasick", 2, 3, { { "rate", 2.4f }, { "depth", 100 }, { "mix", 100 } });
    return v;
    }();
    return v;
}

}} // namespace pump::swirl
