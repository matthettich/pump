// pump POLY: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace poly {

// Parameter indices. Symbols never change once released; append only.
enum Param : int {
    kASaw = 0, kAPulse, kAFreq, kAPw, kSync,
    kBSaw, kBTri, kBPulse, kBFreq, kBFine, kBPw, kBLow,
    kLevelA, kLevelB, kNoise, kSub,
    kFilterType, kCutoff, kRes, kFilterEnv, kKeyTrack, kHpf, kVelFilter,
    kFA, kFD, kFS, kFR, kAA, kAD, kAS, kAR, kVelAmp,
    kPmEnv, kPmOscB, kPmFreqA, kPmPwA, kPmFilter,
    kLfoShape, kLfoRate, kLfoDelay, kLfoPitch, kLfoPw, kLfoFilter, kWheelMod,
    kVoiceMode, kVoices, kUnisonDetune, kGlide, kSlop, kBendRange, kChorus, kVolume,
    kNumParams
};

// The filter voicings:
//   Five 24   a four-pole low-pass that keeps its bass as resonance rises: the round, punchy 5-voice-era sound
//   Eight 24  a four-pole ladder: brighter, loses bass with resonance, sings at the top
//   Eight 12  the same filter tapped at two poles: open, buzzy, more harmonics through
//   OB 12     a two-pole state-variable: soft, wide, rings but never whistles
enum FilterType : int { kFive24 = 0, kEight24, kEight12, kOb12, kNumFilters };
static const char* const kFilterNames[] = { "Five 24", "Eight 24", "Eight 12", "OB 12" };
static const char* const kFilterKeys[] = { "FIVE 24", "EIGHT 24", "EIGHT 12", "OB 12" };
enum LfoShape : int { kLfoTri = 0, kLfoSaw, kLfoSquare, kLfoSh, kNumLfo };
static const char* const kLfoNames[] = { "Triangle", "Saw", "Square", "S&H" };
enum VoiceMode : int { kModePoly = 0, kModeUnison, kModeMono, kNumModes };
static const char* const kModeNames[] = { "Poly", "Unison", "Mono" };
enum Chorus : int { kChorusOff = 0, kChorusI, kChorusII, kChorusBoth, kNumChorus };
static const char* const kChorusNames[] = { "Off", "I", "II", "I+II" };
constexpr float kHpfOff = 10.0f;
inline bool hpfActive(double f) { return f > kHpfOff + 0.05; }

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean;
    v.push_back({ "a_saw", "Osc A Saw", "Saw", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "a_pulse", "Osc A Pulse", "Pulse", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "a_freq", "Osc A Frequency", "Freq", -24, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "a_pw", "Osc A Pulse Width", "PW", 5, 95, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sync", "Sync A to B", "Sync", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "b_saw", "Osc B Saw", "Saw", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "b_tri", "Osc B Triangle", "Tri", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "b_pulse", "Osc B Pulse", "Pulse", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "b_freq", "Osc B Frequency", "Freq", -24, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "b_fine", "Osc B Fine", "Fine", -50, 50, 7, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "b_pw", "Osc B Pulse Width", "PW", 5, 95, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "b_low", "Osc B Low Frequency", "Lo", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "level_a", "Osc A Level", "A", 0, 100, 80, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "level_b", "Osc B Level", "B", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "noise", "Noise", "Noise", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sub", "Sub Oscillator", "Sub", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_type", "Filter Voicing", "Filter", 0, kNumFilters - 1, kFive24, PUnit::None, PScale::Linear, I, kFilterNames, 1 });
    v.push_back({ "cutoff", "Cutoff", "Cutoff", 20, 20000, 2200, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "res", "Resonance", "Res", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_env", "Filter Envelope", "Env", -100, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "key_track", "Key Tracking", "Key", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "hpf", "High-Pass", "HPF", kHpfOff, 2000, kHpfOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "vel_filter", "Velocity to Filter", "Vel F", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "f_a", "Filter Attack", "A", 0.5f, 10000, 5, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "f_d", "Filter Decay", "D", 1, 10000, 700, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "f_s", "Filter Sustain", "S", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "f_r", "Filter Release", "R", 1, 15000, 500, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "a_a", "Amp Attack", "A", 0.5f, 10000, 5, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "a_d", "Amp Decay", "D", 1, 10000, 800, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "a_s", "Amp Sustain", "S", 0, 100, 80, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "a_r", "Amp Release", "R", 1, 15000, 600, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "vel_amp", "Velocity to Level", "Vel A", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pm_env", "Poly-Mod Filter Envelope", "Env", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pm_oscb", "Poly-Mod Osc B", "Osc B", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pm_freq_a", "Poly-Mod to Freq A", "Freq A", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "pm_pw_a", "Poly-Mod to PW A", "PW A", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "pm_filter", "Poly-Mod to Filter", "Filter", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "lfo_shape", "LFO Shape", "Shape", 0, kNumLfo - 1, kLfoTri, PUnit::None, PScale::Linear, I, kLfoNames, 1 });
    v.push_back({ "lfo_rate", "LFO Rate", "Rate", 0.05f, 30, 5, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo_delay", "LFO Delay", "Delay", 0, 5000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "lfo_pitch", "LFO to Pitch", "Pitch", 0, 100, 0, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "lfo_pw", "LFO to Pulse Width", "PW", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_filter", "LFO to Filter", "Filter", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wheel_mod", "Wheel to LFO", "Wheel", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voice_mode", "Voice Mode", "Mode", 0, kNumModes - 1, kModePoly, PUnit::None, PScale::Linear, I, kModeNames, 1 });
    v.push_back({ "voices", "Voices", "Voices", 1, 16, 8, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "unison_detune", "Unison Detune", "Detune", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "slop", "Slop", "Slop", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bend_range", "Pitch Bend Range", "Bend", 0, 24, 2, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "chorus", "Chorus", "Chorus", 0, kNumChorus - 1, kChorusOff, PUnit::None, PScale::Linear, I, kChorusNames, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 6, -8, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
}

// Factory presets. Filters: 0 five24 1 eight24 2 eight12 3 ob12; chorus 0 off 1 I 2 II 3 I+II; modes 0 poly 1 unison 2 mono.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("BRASS", "Classic brass", { { "a_saw", 1 }, { "b_saw", 1 }, { "b_fine", 9 }, { "cutoff", 600 }, { "res", 10 }, { "filter_env", 60 }, { "f_a", 80 }, { "f_d", 900 }, { "f_s", 55 }, { "a_a", 40 }, { "a_r", 350 } });
    F("BRASS", "Stab brass", { { "cutoff", 900 }, { "filter_env", 55 }, { "f_a", 3 }, { "f_d", 300 }, { "f_s", 20 }, { "a_d", 400 }, { "a_s", 50 }, { "a_r", 200 } });
    F("BRASS", "Unison brass lead", { { "voice_mode", 1 }, { "voices", 6 }, { "unison_detune", 30 }, { "cutoff", 700 }, { "filter_env", 55 }, { "f_a", 60 }, { "glide", 60 } });
    F("PADS", "Warm strings", { { "a_saw", 1 }, { "b_saw", 1 }, { "b_freq", 12 }, { "b_fine", 5 }, { "level_b", 50 }, { "cutoff", 2500 }, { "filter_env", 10 }, { "a_a", 600 }, { "a_r", 1500 }, { "chorus", 2 }, { "lfo_pitch", 8 }, { "lfo_delay", 600 } });
    F("PADS", "Ensemble pad", { { "a_saw", 0 }, { "a_pulse", 1 }, { "a_pw", 35 }, { "lfo_pw", 30 }, { "lfo_rate", 0.6f }, { "level_b", 0 }, { "sub", 40 }, { "filter_type", 2 }, { "cutoff", 3000 }, { "a_a", 400 }, { "a_r", 1200 }, { "chorus", 3 }, { "hpf", 120 } });
    F("PADS", "Soft OB pad", { { "filter_type", 3 }, { "cutoff", 1800 }, { "res", 30 }, { "a_a", 700 }, { "a_r", 1800 }, { "f_a", 900 }, { "filter_env", 25 }, { "chorus", 1 } });
    F("PADS", "Sync sweep pad", { { "sync", 1 }, { "a_freq", 7 }, { "pm_env", 40 }, { "f_a", 1500 }, { "f_d", 3000 }, { "f_s", 40 }, { "a_a", 600 }, { "a_r", 1500 }, { "level_b", 30 } });
    F("KEYS", "Poly-mod bell", { { "pm_oscb", 45 }, { "pm_freq_a", 1 }, { "b_freq", 19 }, { "b_saw", 0 }, { "b_tri", 1 }, { "level_b", 0 }, { "cutoff", 5000 }, { "filter_env", 20 }, { "a_d", 1800 }, { "a_s", 0 }, { "a_r", 1500 } });
    F("KEYS", "Electric keys", { { "a_saw", 0 }, { "a_pulse", 1 }, { "a_pw", 20 }, { "b_saw", 0 }, { "b_tri", 1 }, { "b_freq", 12 }, { "level_b", 40 }, { "cutoff", 1600 }, { "filter_env", 35 }, { "f_d", 600 }, { "f_s", 10 }, { "a_d", 1200 }, { "a_s", 20 }, { "chorus", 1 } });
    F("KEYS", "Clavi pluck", { { "a_saw", 0 }, { "a_pulse", 1 }, { "a_pw", 12 }, { "level_b", 0 }, { "filter_type", 2 }, { "cutoff", 1200 }, { "res", 35 }, { "filter_env", 55 }, { "f_d", 200 }, { "f_s", 0 }, { "a_d", 400 }, { "a_s", 0 }, { "a_r", 150 } });
    F("LEADS", "Sync lead", { { "voice_mode", 2 }, { "sync", 1 }, { "pm_env", 35 }, { "a_freq", 5 }, { "level_b", 0 }, { "cutoff", 4000 }, { "glide", 50 }, { "f_d", 1000 } });
    F("LEADS", "Fat unison", { { "voice_mode", 1 }, { "voices", 8 }, { "unison_detune", 45 }, { "cutoff", 1500 }, { "res", 25 }, { "filter_env", 40 }, { "glide", 30 } });
    F("BASS", "Poly bass", { { "a_freq", -12 }, { "b_freq", -12 }, { "cutoff", 400 }, { "filter_env", 55 }, { "f_d", 350 }, { "f_s", 10 }, { "a_r", 120 } });
    F("BASS", "Sub pulse bass", { { "a_saw", 0 }, { "a_pulse", 1 }, { "a_pw", 40 }, { "sub", 70 }, { "level_b", 0 }, { "a_freq", -12 }, { "cutoff", 500 }, { "filter_env", 40 }, { "f_d", 300 } });
    F("FX", "Poly-mod growl", { { "pm_oscb", 60 }, { "pm_filter", 1 }, { "pm_freq_a", 1 }, { "b_low", 1 }, { "b_freq", -5 }, { "cutoff", 900 }, { "res", 40 } });
    F("FX", "Wind", { { "level_a", 0 }, { "level_b", 0 }, { "noise", 80 }, { "filter_type", 3 }, { "res", 75 }, { "lfo_filter", 50 }, { "lfo_shape", 3 }, { "lfo_rate", 0.4f }, { "a_a", 900 }, { "a_r", 2000 } });
    return v;
}

}} // namespace pump::poly
