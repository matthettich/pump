// pump MONO: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace mono {

// Parameter indices. Symbols never change once released; append only.
enum Param : int {
    kOsc1Wave = 0, kOsc1Oct, kOsc1Level,
    kOsc2Wave, kOsc2Oct, kOsc2Pitch, kOsc2Level, kSync,
    kOsc3Wave, kOsc3Oct, kOsc3Pitch, kOsc3Level, kFm,
    kPw, kNoise, kOverdrive,
    kFilterType, kCutoff, kRes, kFilterEnv, kKeyTrack, kFilterMode, kVelFilter,
    kFA, kFD, kFS, kFR, kAA, kAD, kAS, kAR, kVelAmp,
    kLfoShape, kLfoRate, kLfoPitch, kLfoFilter, kLfoPw, kWheelVib,
    kGlide, kGlideMode, kPriority, kLegato, kDrift, kBendRange, kVolume,
    kNumParams
};

enum Wave : int { kTri = 0, kSaw, kSquare, kPulse, kNumWaves };
static const char* const kWaveNames[] = { "Triangle", "Saw", "Square", "Pulse" };
static const char* const kFootNames[] = { "32'", "16'", "8'", "4'", "2'" };   // octave -2 .. +2
// The filter models:
//   Ladder 24  a transistor ladder: four poles, round and fat, loses bass as resonance rises, sings when pushed
//   SVF 12     a two-pole state-variable: MODE sweeps low-pass -> notch -> high-pass; soft, vocal, hollow
//   Diode 18   a diode ladder: three poles with a high-pass in the feedback; rubbery, squelchy resonance
//   Sallen 12  a two-pole with a clipping resonance loop: bright, aggressive, screams when pushed
enum FilterType : int { kLadder = 0, kSvf, kDiode, kSallen, kNumFilters };
static const char* const kFilterNames[] = { "Ladder 24", "SVF 12", "Diode 18", "Sallen 12" };
static const char* const kFilterKeys[] = { "LADDER", "SVF", "DIODE", "SALLEN" };
enum LfoShape : int { kLfoTri = 0, kLfoSaw, kLfoSquare, kLfoSh, kNumLfo };
static const char* const kLfoNames[] = { "Triangle", "Saw", "Square", "S&H" };
static const char* const kGlideModeNames[] = { "Always", "Legato" };
enum Priority : int { kLast = 0, kLow, kHigh, kNumPriority };
static const char* const kPriorityNames[] = { "Last", "Low", "High" };

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean;
    v.push_back({ "osc1_wave", "Osc 1 Wave", "Wave", 0, kNumWaves - 1, kSaw, PUnit::None, PScale::Linear, I, kWaveNames, 1 });
    v.push_back({ "osc1_oct", "Osc 1 Range", "Range", 0, 4, 1, PUnit::None, PScale::Linear, I, kFootNames, 1 });
    v.push_back({ "osc1_level", "Osc 1 Level", "Level", 0, 100, 80, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "osc2_wave", "Osc 2 Wave", "Wave", 0, kNumWaves - 1, kSaw, PUnit::None, PScale::Linear, I, kWaveNames, 1 });
    v.push_back({ "osc2_oct", "Osc 2 Range", "Range", 0, 4, 1, PUnit::None, PScale::Linear, I, kFootNames, 1 });
    v.push_back({ "osc2_pitch", "Osc 2 Pitch", "Pitch", -12, 12, 0.08f, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "osc2_level", "Osc 2 Level", "Level", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sync", "Sync 2 to 1", "Sync", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "osc3_wave", "Osc 3 Wave", "Wave", 0, kNumWaves - 1, kSquare, PUnit::None, PScale::Linear, I, kWaveNames, 1 });
    v.push_back({ "osc3_oct", "Osc 3 Range", "Range", 0, 4, 0, PUnit::None, PScale::Linear, I, kFootNames, 1 });
    v.push_back({ "osc3_pitch", "Osc 3 Pitch", "Pitch", -12, 12, -0.06f, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "osc3_level", "Osc 3 Level", "Level", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fm", "FM 3 to 1", "FM", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pw", "Pulse Width", "PW", 5, 50, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "noise", "Noise", "Noise", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "overdrive", "Overdrive", "Drive", 0, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_type", "Filter Model", "Model", 0, kNumFilters - 1, kLadder, PUnit::None, PScale::Linear, I, kFilterNames, 1 });
    v.push_back({ "cutoff", "Cutoff", "Cutoff", 20, 20000, 800, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "res", "Resonance", "Res", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_env", "Filter Envelope", "Env", -100, 100, 45, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "key_track", "Key Tracking", "Key", 0, 100, 33, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_mode", "SVF Mode", "Mode", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vel_filter", "Velocity to Filter", "Vel F", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "f_a", "Filter Attack", "A", 0.5f, 10000, 2, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "f_d", "Filter Decay", "D", 1, 10000, 350, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "f_s", "Filter Sustain", "S", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "f_r", "Filter Release", "R", 1, 15000, 250, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "a_a", "Amp Attack", "A", 0.5f, 10000, 2, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "a_d", "Amp Decay", "D", 1, 10000, 600, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "a_s", "Amp Sustain", "S", 0, 100, 85, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "a_r", "Amp Release", "R", 1, 15000, 150, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "vel_amp", "Velocity to Level", "Vel A", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_shape", "LFO Shape", "Shape", 0, kNumLfo - 1, kLfoTri, PUnit::None, PScale::Linear, I, kLfoNames, 1 });
    v.push_back({ "lfo_rate", "LFO Rate", "Rate", 0.05f, 40, 5, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo_pitch", "LFO to Pitch", "Pitch", 0, 100, 0, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "lfo_filter", "LFO to Filter", "Filter", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_pw", "LFO to Pulse Width", "PW", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wheel_vib", "Wheel Vibrato", "Wheel", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "glide_mode", "Glide Mode", "Glide", 0, 1, 0, PUnit::None, PScale::Linear, I, kGlideModeNames, 1 });
    v.push_back({ "priority", "Note Priority", "Priority", 0, kNumPriority - 1, kLast, PUnit::None, PScale::Linear, I, kPriorityNames, 1 });
    v.push_back({ "legato", "Legato", "Legato", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "drift", "Drift", "Drift", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bend_range", "Pitch Bend Range", "Bend", 0, 24, 2, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. Waves: 0 tri 1 saw 2 square 3 pulse; ranges 0..4 = 32' 16' 8' 4' 2'; filters: 0 ladder 1 svf
// 2 diode 3 sallen.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("BASS", "Three-oscillator bass", { { "osc1_oct", 0 }, { "osc2_oct", 0 }, { "osc3_level", 60 }, { "osc3_oct", 0 }, { "osc3_wave", 1 }, { "cutoff", 450 }, { "res", 15 }, { "filter_env", 55 }, { "f_d", 300 }, { "f_s", 15 }, { "overdrive", 6 } });
    F("BASS", "Round sub", { { "osc1_wave", 0 }, { "osc1_oct", 0 }, { "osc2_level", 0 }, { "osc3_wave", 2 }, { "osc3_oct", 0 }, { "osc3_level", 40 }, { "cutoff", 300 }, { "filter_env", 20 }, { "res", 0 } });
    F("BASS", "Squelch line", { { "filter_type", 2 }, { "osc1_oct", 1 }, { "osc2_level", 0 }, { "cutoff", 350 }, { "res", 78 }, { "filter_env", 70 }, { "f_d", 220 }, { "f_s", 0 }, { "a_s", 100 }, { "a_r", 30 }, { "glide", 60 }, { "glide_mode", 1 }, { "overdrive", 8 } });
    F("BASS", "Square pluck bass", { { "osc1_wave", 2 }, { "osc2_wave", 2 }, { "osc1_oct", 0 }, { "osc2_oct", 1 }, { "cutoff", 600 }, { "filter_env", 60 }, { "f_d", 180 }, { "f_s", 0 }, { "a_d", 450 }, { "a_s", 0 }, { "a_r", 120 } });
    F("BASS", "Hollow notch bass", { { "filter_type", 1 }, { "filter_mode", 50 }, { "cutoff", 700 }, { "res", 50 }, { "filter_env", 40 }, { "osc1_oct", 0 }, { "osc2_oct", 0 } });
    F("BASS", "Driven saw bass", { { "osc1_oct", 0 }, { "osc2_oct", 0 }, { "osc2_pitch", 0.15f }, { "overdrive", 18 }, { "cutoff", 900 }, { "res", 30 }, { "filter_env", 35 } });
    F("LEADS", "Classic lead", { { "osc1_oct", 2 }, { "osc2_oct", 2 }, { "osc3_level", 50 }, { "osc3_oct", 1 }, { "cutoff", 2500 }, { "res", 25 }, { "filter_env", 30 }, { "glide", 70 }, { "lfo_pitch", 0 }, { "wheel_vib", 40 } });
    F("LEADS", "Sync lead", { { "sync", 1 }, { "osc2_pitch", 7 }, { "osc1_level", 0 }, { "osc2_level", 85 }, { "osc1_oct", 2 }, { "osc2_oct", 2 }, { "cutoff", 5000 }, { "filter_env", 25 }, { "glide", 50 } });
    F("LEADS", "Screamer", { { "filter_type", 3 }, { "osc1_oct", 2 }, { "osc2_oct", 2 }, { "cutoff", 1800 }, { "res", 80 }, { "overdrive", 12 }, { "filter_env", 30 }, { "glide", 40 } });
    F("LEADS", "Vocal SVF lead", { { "filter_type", 1 }, { "filter_mode", 30 }, { "osc1_oct", 2 }, { "osc2_oct", 2 }, { "cutoff", 1400 }, { "res", 60 }, { "lfo_filter", 25 }, { "lfo_rate", 0.8f }, { "glide", 60 } });
    F("LEADS", "FM bite", { { "osc3_oct", 3 }, { "osc3_level", 0 }, { "fm", 35 }, { "osc1_oct", 2 }, { "osc2_level", 0 }, { "cutoff", 4000 }, { "filter_env", 20 } });
    F("PLUCKS", "Pulse pluck", { { "osc1_wave", 3 }, { "osc2_wave", 3 }, { "pw", 15 }, { "osc1_oct", 2 }, { "osc2_oct", 2 }, { "cutoff", 900 }, { "filter_env", 60 }, { "f_d", 160 }, { "f_s", 0 }, { "a_d", 400 }, { "a_s", 0 }, { "a_r", 300 } });
    F("PLUCKS", "PWM wobble", { { "osc1_wave", 3 }, { "osc2_wave", 3 }, { "lfo_pw", 70 }, { "lfo_rate", 2 }, { "osc1_oct", 1 }, { "osc2_oct", 1 }, { "cutoff", 1500 } });
    F("FX", "Siren", { { "lfo_pitch", 80 }, { "lfo_rate", 0.4f }, { "osc1_oct", 2 }, { "osc2_level", 0 }, { "cutoff", 3000 } });
    F("FX", "Self-oscillating filter", { { "volume", -9 }, { "osc1_level", 0 }, { "osc2_level", 0 }, { "res", 100 }, { "key_track", 100 }, { "cutoff", 260 }, { "filter_env", 0 } });
    F("FX", "Noise sweep", { { "osc1_level", 0 }, { "osc2_level", 0 }, { "noise", 90 }, { "filter_type", 1 }, { "res", 70 }, { "lfo_filter", 60 }, { "lfo_rate", 0.2f } });
    return v;
    }();
    return v;
}

}} // namespace pump::mono
