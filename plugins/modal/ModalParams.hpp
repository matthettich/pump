// pump MODAL: parameters and factory voices (shared by the DSP and the UI).
//
// A physical-modelling synth of struck, plucked, bowed and blown objects. Every note is a bank of up to 48
// resonant modes (each a decaying sine), tuned in the ratios of an object: a free bar, a tuned marimba bar, a
// string, a drum membrane, a square plate, a bell, a tube (odd harmonics) or a wine glass. An exciter drives them:
// a MALLET (HARDNESS: a short click to a soft thud), a PLUCK (a noise burst), a BOW (stick-slip pulses and friction
// noise while the key is held, PRESSURE) or BREATH (noise around the note while held). MATERIAL goes from wood
// (the upper modes die fast) to glass to metal (everything rings); POSITION is where it is struck (the middle of a
// string has no even modes); STRETCH squeezes or spreads the modes; BRIGHT tilts them.
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace modal {

enum Model : int { kBar = 0, kMarimba, kString, kMembrane, kPlate, kBell, kTube, kGlass, kNumModels };
static const char* const kModelNames[] = { "Bar", "Marimba", "String", "Membrane", "Plate", "Bell", "Tube", "Glass" };
static const char* const kModelKeys[] = { "BAR", "MARIMBA", "STRING", "DRUM", "PLATE", "BELL", "TUBE", "GLASS" };
enum Exciter : int { kMallet = 0, kPluck, kBow, kBlow, kNumExciters };
static const char* const kExciterNames[] = { "Mallet", "Pluck", "Bow", "Breath" };
static const char* const kVoiceModeNames[] = { "Poly", "Mono", "Legato" };

enum Param : int {
    kModel = 0, kModes, kMaterial, kDecay, kDamp, kBright, kStretch, kPosition,
    kExciter, kHardness, kNoise, kPressure, kVibrato, kVibRate,
    kVelocity, kSpread, kVoices, kVoiceMode, kGlide, kOctave, kVolume,
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    v.push_back({ "model", "Object", "Object", 0, kNumModels - 1, kMarimba, PUnit::None, PScale::Linear, I, kModelNames, 1 });
    v.push_back({ "modes", "Modes", "Modes", 4, 48, 24, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "material", "Material", "Material", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "Decay", 0.05f, 20, 1.5f, PUnit::Seconds, PScale::Log, A, nullptr, 1 });
    v.push_back({ "damp", "Damp (key up)", "Damp", 0.02f, 10, 1.0f, PUnit::Seconds, PScale::Log, A, nullptr, 1 });
    v.push_back({ "bright", "Brightness", "Bright", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "stretch", "Stretch", "Stretch", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "position", "Strike Position", "Position", 0, 100, 22, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "exciter", "Exciter", "Exciter", 0, kNumExciters - 1, kMallet, PUnit::None, PScale::Linear, I, kExciterNames, 1 });
    v.push_back({ "hardness", "Hardness", "Hardness", 0, 100, 55, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "noise", "Exciter Noise", "Noise", 0, 100, 10, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pressure", "Bow / Breath Pressure", "Pressure", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vibrato", "Vibrato", "Vibrato", 0, 100, 0, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "vib_rate", "Vibrato Rate", "Rate", 0.5f, 12, 5.0f, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "velocity", "Velocity", "Velocity", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spread", "Stereo Spread", "Spread", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voices", "Voices", "Voices", 1, 8, 8, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "voice_mode", "Voice Mode", "Mode", 0, 2, 0, PUnit::None, PScale::Linear, I, kVoiceModeNames, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "octave", "Octave", "Oct", -3, 3, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
}

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("STRUCK", "Marimba", { { "model", kMarimba }, { "material", 20 }, { "decay", 1.2f }, { "hardness", 50 }, { "position", 22 } });
    F("STRUCK", "Vibraphone", { { "model", kMarimba }, { "material", 90 }, { "decay", 5.0f }, { "hardness", 45 }, { "vibrato", 0 }, { "damp", 0.3f } });
    F("STRUCK", "Glockenspiel", { { "model", kBar }, { "material", 95 }, { "decay", 3.0f }, { "hardness", 90 }, { "octave", 1 } });
    F("STRUCK", "Wood block", { { "model", kBar }, { "material", 5 }, { "decay", 0.25f }, { "hardness", 85 }, { "modes", 12 }, { "volume", 5 } });
    F("STRUCK", "Steel drum", { { "model", kPlate }, { "material", 75 }, { "decay", 1.8f }, { "hardness", 40 }, { "stretch", -40 }, { "position", 35 }, { "volume", 5 } });
    F("STRUCK", "Tubular bell", { { "model", kBell }, { "material", 100 }, { "decay", 9.0f }, { "hardness", 70 }, { "modes", 32 }, { "damp", 4.0f } });
    F("STRUCK", "Wine glass", { { "model", kGlass }, { "material", 92 }, { "decay", 6.0f }, { "hardness", 75 }, { "modes", 12 } });
    F("STRUCK", "Tom membrane", { { "model", kMembrane }, { "material", 40 }, { "decay", 0.7f }, { "hardness", 35 }, { "octave", -1 }, { "position", 30 }, { "volume", 4 } });
    F("PLUCKED", "Plucked string", { { "model", kString }, { "exciter", kPluck }, { "material", 60 }, { "decay", 3.0f }, { "hardness", 65 }, { "position", 15 }, { "modes", 40 } });
    F("PLUCKED", "Harp", { { "model", kString }, { "exciter", kPluck }, { "material", 50 }, { "decay", 4.5f }, { "hardness", 40 }, { "position", 30 }, { "modes", 32 }, { "damp", 2.0f } });
    F("PLUCKED", "Koto-ish", { { "model", kString }, { "exciter", kPluck }, { "material", 70 }, { "decay", 2.5f }, { "hardness", 90 }, { "position", 8 }, { "stretch", 8 }, { "modes", 40 } });
    F("BOWED", "Bowed string", { { "model", kString }, { "exciter", kBow }, { "material", 55 }, { "decay", 2.0f }, { "pressure", 60 }, { "position", 12 }, { "modes", 40 }, { "vibrato", 25 }, { "damp", 0.3f } });
    F("BOWED", "Bowed glass", { { "model", kGlass }, { "exciter", kBow }, { "material", 95 }, { "decay", 4.0f }, { "pressure", 45 }, { "modes", 12 }, { "damp", 1.2f }, { "volume", 7 } });
    F("BOWED", "Bowed bar", { { "model", kBar }, { "exciter", kBow }, { "material", 85 }, { "decay", 3.0f }, { "pressure", 55 }, { "damp", 0.8f }, { "volume", 6 } });
    F("BLOWN", "Breath tube", { { "model", kTube }, { "exciter", kBlow }, { "material", 50 }, { "decay", 0.6f }, { "pressure", 70 }, { "noise", 40 }, { "vibrato", 18 }, { "damp", 0.15f } });
    F("BLOWN", "Bottle", { { "model", kTube }, { "exciter", kBlow }, { "material", 80 }, { "decay", 1.0f }, { "pressure", 55 }, { "noise", 60 }, { "modes", 8 }, { "damp", 0.2f } });
    F("BLOWN", "Wind plate", { { "model", kPlate }, { "exciter", kBlow }, { "material", 90 }, { "decay", 3.0f }, { "pressure", 45 }, { "noise", 80 }, { "damp", 1.5f }, { "spread", 90 }, { "volume", 7 } });
    return v;
}

}} // namespace pump::modal
