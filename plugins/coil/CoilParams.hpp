// pump COIL: parameters and factory voices (shared by the DSP and the UI). The tuning and chord lists are shared with
// pump TWANG (the resonator on the same engine).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Mod.hpp"
#include "dsp/Tempo.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace coil {

// TWANG's tuning: a chord (ROOT + CHORD), the held MIDI notes, or the last notes played (latched)
enum Tuning : int { kTuneChord = 0, kTuneMidi, kTuneLatch, kNumTunings };
static const char* const kTuningNames[] = { "Chord", "MIDI", "MIDI latch" };
enum Chord : int { kChordSingle = 0, kChordOctave, kChordFifth, kChordPower, kChordMajor, kChordMinor, kChordSus4, kChordMaj7, kChordMin7, kChordDom7, kChordSus2, kChordStack, kNumChords };
static const char* const kChordNames[] = { "Single", "Octave", "Fifth", "Power", "Major", "Minor", "Sus4", "Maj7", "Min7", "Dom7", "Sus2", "Fifths stack" };
static const char* const kRootNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
// SPRING's models, as COIL / TWANG name them
static const char* const kModelNames[] = { "Two-spring", "Three-spring", "Short tank", "Long tank", "Digital", "Lo-fi digital" };

// Parameter indices. Symbols never change once released; append only.
enum Param : int {
    kModel = 0, kTension, kDrip, kDecay, kRelease, kHarmonic, kSpread, kWobble, kTone, kExcite, kTank, kGrit,
    kWidth, kOctave, kBend, kVelocity, kVolume,
    kVoices,
    // appended 2026-10-27: modulation. Two LFOs (shape, rate, sync, note division, retrigger), then the connections:
    // a pool of kModSlots slots of three (source, target, amount)
    kLfoBase,
    kSlotBase = kLfoBase + 2 * 5,
    // appended 2026-10-28: LFO 3 (its five settings, as LFO 1 and 2)
    kLfo3Base = kSlotBase + 3 * pump::mod::kSlots,
    // appended 2026-10-28: each LFO plays its drawn shape instead (the shapes are saved as the "shapes" state)
    kDrawBase = kLfo3Base + 5,
    kNumParams = kDrawBase + 3
};
inline int lfoDrawParam(int lfo) { return kDrawBase + lfo; }
enum LfoParam : int { kLfoShape = 0, kLfoRate, kLfoSync, kLfoDiv, kLfoRetrig, kPerLfo };
inline int lfoParam(int lfo, int what) { return (lfo < 2 ? kLfoBase + lfo * kPerLfo : kLfo3Base) + what; }
enum SlotParam : int { kSlotSrc = 0, kSlotDst, kSlotAmt, kPerSlot };
inline int slotParam(int slot, int what) { return kSlotBase + slot * kPerSlot + what; }

// What a connection can move: COIL's continuous controls and the LFOs' rates. Index 0 is "none". (Append only: a
// slot's target is saved as this index.)
static const int kTargetParams[] = { -1, kTension, kDrip, kDecay, kRelease, kHarmonic, kSpread, kWobble, kTone, kExcite, kTank, kGrit,
                                     kWidth, kVelocity, kVolume, -2 /* LFO 1 rate */, -3 /* LFO 2 rate */,
                                     -4 /* LFO 3 rate */ };
static const char* const kTargetNames[] = { "None", "Tension", "Drip", "Decay", "Release", "Harmonic", "Spread", "Wobble", "Tone", "Excite",
                                            "Tank", "Grit", "Width", "Velocity", "Volume", "LFO 1 rate", "LFO 2 rate",
                                            "LFO 3 rate" };
constexpr int kNumTargets = (int)(sizeof(kTargetParams) / sizeof(kTargetParams[0]));
// the target that is LFO l's rate, and which LFO's rate a target is (-1: none)
inline int lfoRateTarget(int l) { for (int t = 1; t < kNumTargets; ++t) if (kTargetParams[t] == -2 - l) return t; return 0; }
inline int lfoOfTarget(int t) { return t > 0 && t < kNumTargets && kTargetParams[t] <= -2 ? -2 - kTargetParams[t] : -1; }
// the target that moves a parameter (an LFO's rate included), 0: none
inline int targetOfParam(int param)
{
    for (int t = 1; t < kNumTargets; ++t) if (kTargetParams[t] == param) return t;
    for (int l = 0; l < pump::mod::kNumLfos; ++l) if (param == lfoParam(l, kLfoRate)) return lfoRateTarget(l);
    return 0;
}
// the parameter a target moves (-1: none)
inline int paramOfTarget(int t) { if (t <= 0 || t >= kNumTargets) return -1; const int l = lfoOfTarget(t); return l >= 0 ? lfoParam(l, kLfoRate) : kTargetParams[t]; }

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    v.push_back({ "model", "Spring Model", "Model", 0, 5, 0, PUnit::None, PScale::Linear, I, kModelNames, 1 });
    v.push_back({ "tension", "Tension", "Tension", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drip", "Drip", "Drip", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "Decay", 0.1f, 15, 2.0f, PUnit::Seconds, PScale::Log, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 0.02f, 8, 0.4f, PUnit::Seconds, PScale::Log, A, nullptr, 1 });
    v.push_back({ "harmonic", "Harmonic", "Harmonic", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spread", "Spread", "Spread", 0, 100, 10, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wobble", "Wobble", "Wobble", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tone", "Tone", "Tone", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "excite", "Excite", "Excite", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tank", "Tank", "Tank", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "grit", "Grit", "Grit", 0, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "width", "Width", "Width", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "octave", "Octave", "Oct", -2, 2, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "bend", "Pitch Bend Range", "Bend", 0, 12, 2, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "velocity", "Velocity to Kick", "Velocity", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    // appended 2026-10-27: polyphony (not automatable: changing it lets the ringing notes go)
    v.push_back({ "voices", "Voices", "Voices", 1, 16, 8, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    // appended 2026-10-27: modulation
    static std::deque<std::string> keep;   // a deque: adding never moves the strings already handed out
    auto str = [&](std::string x) { keep.push_back(std::move(x)); return keep.back().c_str(); };
    auto lfo = [&](int l, float rate) {
        const std::string sy = "lfo" + std::to_string(l + 1), nm = "LFO " + std::to_string(l + 1);
        v.push_back({ str(sy + "_shape"), str(nm + " Shape"), "Shape", 0, pump::mod::kNumShapes - 1, 0, PUnit::None, PScale::Linear, I, pump::mod::kShapeNames, 1 });
        v.push_back({ str(sy + "_rate"), str(nm + " Rate"), "Rate", 0.02f, 40, rate, PUnit::Hz, PScale::Log, A, nullptr, 1 });
        v.push_back({ str(sy + "_sync"), str(nm + " Sync"), "Sync", 0, 1, 0, PUnit::None, PScale::Linear, kPAutomatable | kPBoolean, nullptr, 1 });
        v.push_back({ str(sy + "_div"), str(nm + " Sync Rate"), "Sync", 0, pump::kNumNotes - 1, 11, PUnit::None, PScale::Linear, I, pump::kNoteNames, 1 });
        v.push_back({ str(sy + "_retrig"), str(nm + " Retrigger"), "Retrig", 0, 1, 0, PUnit::None, PScale::Linear, kPAutomatable | kPBoolean, nullptr, 1 });
    };
    lfo(0, 1.0f); lfo(1, 0.3f);
    for (int k = 0; k < pump::mod::kSlots; ++k)
    {
        const std::string sy = "mod" + std::to_string(k + 1), nm = "Mod " + std::to_string(k + 1);
        v.push_back({ str(sy + "_src"), str(nm + " Source"), "Source", 0, pump::mod::kNumSources - 1, 0, PUnit::None, PScale::Linear, kPInteger, pump::mod::kSourceNames, 1 });
        v.push_back({ str(sy + "_dst"), str(nm + " Target"), "Target", 0, kNumTargets - 1, 0, PUnit::None, PScale::Linear, kPInteger, kTargetNames, 1 });
        v.push_back({ str(sy + "_amt"), str(nm + " Amount"), "Amount", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    }
    // appended 2026-10-28
    lfo(2, 5.0f);
    // appended 2026-10-28
    for (int l = 0; l < 3; ++l)
        v.push_back({ str("lfo" + std::to_string(l + 1) + "_draw"), str("LFO " + std::to_string(l + 1) + " Drawn"), "Draw", 0, 1, 0, PUnit::None, PScale::Linear, kPAutomatable | kPBoolean, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory voices. Models: 0 two-spring 1 three-spring 2 short 3 long 4 digital 5 lo-fi digital.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("SPRINGS", "Two-spring boing", { { "model", 0 }, { "decay", 2.5f }, { "spread", 10 } });
    F("SPRINGS", "Three-spring clang", { { "model", 1 }, { "decay", 3.0f }, { "spread", 25 }, { "drip", 65 } });
    F("SPRINGS", "Short tank twang", { { "model", 2 }, { "decay", 1.2f }, { "excite", 70 }, { "tension", 30 } });
    F("SPRINGS", "Long tank gong", { { "model", 3 }, { "decay", 7.0f }, { "spread", 35 }, { "excite", 30 }, { "release", 2.0f } });
    F("SPRINGS", "Loose and drippy", { { "model", 0 }, { "tension", -80 }, { "drip", 100 }, { "decay", 3.0f }, { "wobble", 35 } });
    F("SPRINGS", "Tight wire", { { "model", 2 }, { "tension", 90 }, { "drip", 20 }, { "decay", 1.5f }, { "tone", 40 } });
    F("SPRINGS", "Shimmer cluster", { { "model", 1 }, { "spread", 100 }, { "decay", 5.0f }, { "wobble", 40 }, { "width", 100 } });
    F("SPRINGS", "Spring in the tank", { { "model", 0 }, { "tank", 60 }, { "decay", 2.5f } });
    F("TUNED", "Spring harp", { { "model", 1 }, { "harmonic", 70 }, { "decay", 4.0f }, { "excite", 60 }, { "spread", 5 }, { "release", 1.5f } });
    F("TUNED", "Spring guitar", { { "model", 0 }, { "harmonic", 85 }, { "decay", 2.5f }, { "excite", 80 }, { "spread", 4 }, { "tone", 20 } });
    F("TUNED", "Clean pluck", { { "model", 2 }, { "harmonic", 100 }, { "decay", 1.5f }, { "spread", 0 }, { "wobble", 0 }, { "excite", 75 } });
    F("TUNED", "Half spring", { { "model", 0 }, { "harmonic", 50 }, { "decay", 2.5f } });
    F("TUNED", "Music box", { { "model", 2 }, { "harmonic", 60 }, { "octave", 1 }, { "decay", 2.0f }, { "excite", 90 }, { "tension", 60 } });
    F("BASS", "Tank bass", { { "model", 3 }, { "octave", -1 }, { "harmonic", 40 }, { "decay", 1.6f }, { "excite", 15 }, { "tone", -30 }, { "spread", 3 } });
    F("BASS", "Boing bass", { { "model", 0 }, { "octave", -1 }, { "decay", 1.2f }, { "excite", 25 }, { "drip", 80 } });
    F("METAL", "Metal mallet", { { "model", 1 }, { "spread", 60 }, { "decay", 3.5f }, { "excite", 85 }, { "tension", 50 } });
    F("METAL", "Bell spring", { { "model", 3 }, { "spread", 45 }, { "harmonic", 20 }, { "decay", 6.0f }, { "excite", 90 }, { "tone", 30 } });
    F("METAL", "Drip lead", { { "model", 0 }, { "drip", 100 }, { "decay", 0.9f }, { "release", 0.2f }, { "harmonic", 30 }, { "tank", 30 } });
    F("DIGITAL", "Digital comb", { { "model", 4 }, { "decay", 2.0f }, { "grit", 30 } });
    F("DIGITAL", "Lo-fi bell", { { "model", 5 }, { "decay", 3.0f }, { "grit", 70 }, { "spread", 40 } });
    F("DIGITAL", "Crunchy pluck", { { "model", 5 }, { "harmonic", 70 }, { "decay", 1.2f }, { "grit", 90 } });
    F("DIGITAL", "Cheap reverb keys", { { "model", 4 }, { "harmonic", 50 }, { "tank", 50 }, { "decay", 2.0f } });
    return v;
    }();
    return v;
}

}} // namespace pump::coil
