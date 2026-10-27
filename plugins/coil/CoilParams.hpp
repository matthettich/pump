// pump COIL: parameters and factory voices (shared by the DSP and the UI). The tuning and chord lists are shared with
// pump TWANG (the resonator on the same engine).
#pragma once

#include "PumpParams.hpp"
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
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
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
    v.push_back({ "volume", "Volume", "Volume", -48, 12, -6, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
}

// Factory voices. Models: 0 two-spring 1 three-spring 2 short 3 long 4 digital 5 lo-fi digital.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
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
}

}} // namespace pump::coil
