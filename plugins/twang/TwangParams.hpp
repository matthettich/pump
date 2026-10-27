// pump TWANG: parameters and factory presets (shared by the DSP and the UI). The engine is pump COIL's.
#pragma once

#include "PumpParams.hpp"
#include "../coil/CoilParams.hpp"
#include <vector>

namespace pump { namespace twang {

using coil::kModelNames; using coil::kTuningNames; using coil::kChordNames; using coil::kRootNames;

// Parameter indices. Symbols never change once released; append only.
enum Param : int {
    kModel = 0, kTension, kDrip, kDecay, kRelease, kHarmonic, kSpread, kWobble, kTone, kGrit,
    kTuning, kRoot, kChord, kOctave, kDrive, kTank, kWidth, kMix, kVolume, kBypass,
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    v.push_back({ "model", "Spring Model", "Model", 0, 5, 0, PUnit::None, PScale::Linear, I, kModelNames, 1 });
    v.push_back({ "tension", "Tension", "Tension", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drip", "Drip", "Drip", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "Decay", 0.1f, 15, 2.5f, PUnit::Seconds, PScale::Log, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 0.02f, 8, 0.6f, PUnit::Seconds, PScale::Log, A, nullptr, 1 });
    v.push_back({ "harmonic", "Harmonic", "Harmonic", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spread", "Spread", "Spread", 0, 100, 10, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wobble", "Wobble", "Wobble", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tone", "Tone", "Tone", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "grit", "Grit", "Grit", 0, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tuning", "Tuning", "Tuning", 0, coil::kNumTunings - 1, coil::kTuneChord, PUnit::None, PScale::Linear, I, kTuningNames, 1 });
    v.push_back({ "root", "Root", "Root", 0, 11, 9, PUnit::None, PScale::Linear, I, kRootNames, 1 });
    v.push_back({ "chord", "Chord", "Chord", 0, coil::kNumChords - 1, coil::kChordFifth, PUnit::None, PScale::Linear, I, kChordNames, 1 });
    v.push_back({ "octave", "Octave", "Oct", -2, 2, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tank", "Tank", "Tank", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "width", "Width", "Width", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Output", "Output", -24, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Chords: 0 single 1 octave 2 fifth 3 power 4 major 5 minor 6 sus4 7 maj7 8 min7 9 dom7 10 sus2 11 fifths stack.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("CHORDS", "Fifth drone", { { "chord", 2 }, { "decay", 5.0f }, { "harmonic", 50 }, { "mix", 60 } });
    F("CHORDS", "Major strings", { { "chord", 4 }, { "harmonic", 90 }, { "decay", 3.0f }, { "spread", 3 }, { "mix", 50 } });
    F("CHORDS", "Minor seventh shimmer", { { "chord", 8 }, { "harmonic", 60 }, { "decay", 6.0f }, { "spread", 30 }, { "wobble", 35 }, { "width", 100 } });
    F("CHORDS", "Octave sitar-ish", { { "chord", 1 }, { "harmonic", 25 }, { "drip", 90 }, { "decay", 3.0f }, { "tension", 40 } });
    F("CHORDS", "Fifths stack", { { "chord", 11 }, { "octave", -1 }, { "harmonic", 50 }, { "decay", 4.0f } });
    F("CHORDS", "Sus2 bells", { { "chord", 10 }, { "octave", 1 }, { "harmonic", 30 }, { "decay", 4.0f }, { "spread", 40 } });
    F("METAL", "Clanging tank", { { "model", 1 }, { "chord", 3 }, { "harmonic", 0 }, { "spread", 70 }, { "decay", 3.0f }, { "tank", 40 } });
    F("METAL", "Long tank gong", { { "model", 3 }, { "chord", 0 }, { "octave", -1 }, { "harmonic", 10 }, { "decay", 8.0f }, { "spread", 35 } });
    F("METAL", "Drippy single", { { "chord", 0 }, { "harmonic", 0 }, { "drip", 100 }, { "tension", -60 }, { "decay", 2.5f } });
    F("DIGITAL", "Digital comb chord", { { "model", 4 }, { "chord", 4 }, { "harmonic", 50 }, { "decay", 2.0f } });
    F("DIGITAL", "Lo-fi buzz", { { "model", 5 }, { "chord", 2 }, { "grit", 80 }, { "decay", 2.0f }, { "drive", 70 } });
    F("MIDI", "MIDI resonator", { { "tuning", 1 }, { "harmonic", 70 }, { "decay", 2.5f }, { "release", 0.5f } });
    F("MIDI", "MIDI latch drone", { { "tuning", 2 }, { "harmonic", 50 }, { "decay", 6.0f }, { "mix", 60 } });
    return v;
    }();
    return v;
}

}} // namespace pump::twang
