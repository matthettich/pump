// pump WOB: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pump { namespace wob {

// Parameter indices. Symbols never change once released; append only.
enum Param : int {
    kOctave = 0, kTune, kShape, kPartials, kTilt, kOddEven, kStretch,
    kFilterMode, kCutoff, kRes, kFilterEnv, kFilterDecay, kKeyTrack,
    kVowel, kFormant, kCombFreq, kComb,
    kDetune, kWidth, kSub, kSubOct,
    kWobSync, kWobDiv, kWobRate, kWobShape, kWobRetrig, kWobPhase, kWobSmooth, kWobDelay, kWobFade,
    kWobDepth, kWheel, kWobCut, kWobVowel, kWobComb, kWobAmp, kWobPitch,
    kAttack, kDecay, kSustain, kRelease, kGlide, kLegato, kDrive, kVolume,
    kNumParams
};

// The spectral filter is applied to each partial's level (no phase, no aliasing): 24 dB low-pass, a resonant
// band-pass or a 24 dB high-pass, the resonance a peak at the cutoff.
enum FilterMode : int { kLowPass = 0, kBandPass, kHighPass, kNumModes };
static const char* const kModeNames[] = { "Low-pass", "Band-pass", "High-pass" };
enum WobShape : int { kSine = 0, kTriangle, kSawDown, kSawUp, kSquare, kSteps, kNumShapes };
static const char* const kShapeNames[] = { "Sine", "Triangle", "Saw down", "Saw up", "Square", "Steps" };
static const char* const kSubOctNames[] = { "-1 oct", "-2 oct" };
constexpr int kMaxPartials = 64;

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean;
    v.push_back({ "octave", "Octave", "Oct", -3, 2, -1, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "tune", "Tune", "Tune", -12, 12, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "shape", "Spectrum Shape", "Shape", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "partials", "Partials", "Parts", 1, kMaxPartials, 48, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "tilt", "Tilt", "Tilt", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "odd_even", "Odd / Even", "Odd/Even", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "stretch", "Stretch", "Stretch", 0, 100, 0, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "filter_mode", "Filter Mode", "Mode", 0, kNumModes - 1, kLowPass, PUnit::None, PScale::Linear, I, kModeNames, 1 });
    v.push_back({ "cutoff", "Cutoff", "Cutoff", 20, 16000, 400, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "res", "Resonance", "Res", 0, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_env", "Filter Envelope", "Env", -4, 8, 2, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_decay", "Filter Decay", "Decay", 5, 5000, 250, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "key_track", "Key Tracking", "Key", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vowel", "Vowel", "Vowel", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "formant", "Formant Amount", "Formant", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "comb_freq", "Comb Frequency", "Comb F", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "comb", "Comb Amount", "Comb", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "detune", "Reese Detune", "Detune", 0, 50, 0, PUnit::None, PScale::Power, A, nullptr, 2 });
    v.push_back({ "width", "Width", "Width", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sub", "Sub", "Sub", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sub_oct", "Sub Octave", "Sub Oct", 0, 1, 0, PUnit::None, PScale::Linear, I, kSubOctNames, 1 });
    v.push_back({ "wob_sync", "Wobble Sync", "Sync", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "wob_div", "Wobble Division", "Division", 0, kNumNotes - 1, kNote8, PUnit::None, PScale::Linear, I, kNoteNames, 1 });
    v.push_back({ "wob_rate", "Wobble Rate", "Rate", 0.05f, 30, 4, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "wob_shape", "Wobble Shape", "Shape", 0, kNumShapes - 1, kSine, PUnit::None, PScale::Linear, I, kShapeNames, 1 });
    v.push_back({ "wob_retrig", "Wobble Retrigger", "Retrig", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "wob_phase", "Wobble Start Phase", "Phase", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wob_smooth", "Wobble Smooth", "Smooth", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wob_delay", "Wobble Delay", "Delay", 0, 4000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "wob_fade", "Wobble Fade-In", "Fade", 0, 4000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "wob_depth", "Wobble Depth", "Depth", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wheel", "Wheel to Depth", "Wheel", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wob_cut", "Wobble to Cutoff", "Cutoff", -6, 6, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wob_vowel", "Wobble to Vowel", "Vowel", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wob_comb", "Wobble to Comb", "Comb", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wob_amp", "Wobble to Level", "Level", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wob_pitch", "Wobble to Pitch", "Pitch", -12, 12, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "A", 0.5f, 5000, 2, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "D", 1, 10000, 300, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sustain", "Sustain", "S", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "release", "Release", "R", 1, 10000, 80, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "legato", "Legato", "Legato", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
}

// Factory presets. Divisions are kNoteIndex (1/16 = 5, 1/8T = 7, 1/8 = 8, 1/4T = 10, 1/4 = 11, 1/2 = 14, 1/1 = 16).
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("WOBBLE", "Classic wobble", { { "wob_div", kNote8 }, { "wob_cut", 3.5f }, { "cutoff", 180 }, { "res", 55 }, { "filter_env", 0 }, { "sub", 60 } });
    F("WOBBLE", "Yoi vowel", { { "wob_div", kNote8 }, { "formant", 85 }, { "wob_vowel", 100 }, { "wob_cut", 1.5f }, { "cutoff", 900 }, { "res", 30 }, { "drive", 30 } });
    F("WOBBLE", "Triplet talker", { { "wob_div", kNote8T }, { "wob_shape", kTriangle }, { "formant", 70 }, { "vowel", 20 }, { "wob_vowel", 70 }, { "wob_cut", 2 }, { "cutoff", 500 }, { "drive", 20 } });
    F("WOBBLE", "Slow growl", { { "wob_div", kNote4 }, { "detune", 14 }, { "width", 60 }, { "wob_cut", 3 }, { "wob_comb", 60 }, { "comb", 45 }, { "cutoff", 300 }, { "res", 45 }, { "drive", 35 } });
    F("WOBBLE", "Steps", { { "wob_div", kNote16 }, { "wob_shape", kSteps }, { "wob_smooth", 25 }, { "wob_cut", 4 }, { "cutoff", 150 }, { "res", 50 } });
    F("WOBBLE", "Fade-in wobble", { { "wob_div", kNote8 }, { "wob_delay", 400 }, { "wob_fade", 1200 }, { "wob_cut", 3.5f }, { "cutoff", 250 }, { "res", 50 } });
    F("WOBBLE", "Wheel wobble", { { "wob_depth", 0 }, { "wheel", 100 }, { "wob_div", kNote8 }, { "wob_cut", 4 }, { "cutoff", 200 }, { "res", 50 } });
    F("REESE", "Reese", { { "detune", 18 }, { "width", 70 }, { "cutoff", 700 }, { "res", 20 }, { "filter_env", 0.5f }, { "sub", 40 } });
    F("REESE", "Dark reese", { { "detune", 30 }, { "width", 80 }, { "tilt", -30 }, { "cutoff", 400 }, { "res", 30 }, { "wob_sync", 0 }, { "wob_rate", 0.3f }, { "wob_cut", 1 }, { "drive", 40 } });
    F("REESE", "Comb reese", { { "detune", 22 }, { "width", 60 }, { "comb", 60 }, { "comb_freq", 40 }, { "wob_sync", 0 }, { "wob_rate", 0.15f }, { "wob_comb", 40 }, { "cutoff", 1500 } });
    F("BASS", "Pluck bass", { { "cutoff", 120 }, { "res", 45 }, { "filter_env", 5 }, { "filter_decay", 180 }, { "decay", 400 }, { "sustain", 40 }, { "sub", 50 } });
    F("BASS", "Square sub", { { "shape", 50 }, { "partials", 16 }, { "cutoff", 600 }, { "res", 10 }, { "filter_env", 0 }, { "sub", 70 }, { "sub_oct", 0 } });
    F("BASS", "Hollow tri", { { "shape", 100 }, { "partials", 24 }, { "cutoff", 2000 }, { "res", 0 }, { "filter_env", 0 }, { "sub", 30 } });
    F("BASS", "Glide bass", { { "glide", 90 }, { "legato", 1 }, { "cutoff", 300 }, { "res", 40 }, { "filter_env", 3 }, { "sub", 50 } });
    F("BASS", "Odd harmonics", { { "odd_even", 100 }, { "tilt", 20 }, { "cutoff", 1200 }, { "res", 25 }, { "filter_env", 1.5f } });
    F("FX", "Metal bar", { { "stretch", 45 }, { "partials", 40 }, { "cutoff", 3000 }, { "filter_env", 0 }, { "decay", 1500 }, { "sustain", 0 }, { "release", 800 }, { "sub", 0 }, { "octave", 0 } });
    F("FX", "Band sweep", { { "filter_mode", kBandPass }, { "cutoff", 600 }, { "res", 70 }, { "wob_div", kNote2 }, { "wob_cut", 3 }, { "wob_shape", kTriangle }, { "sub", 30 } });
    F("FX", "High-pass laser", { { "filter_mode", kHighPass }, { "cutoff", 300 }, { "res", 60 }, { "wob_pitch", 7 }, { "wob_div", kNote16 }, { "wob_cut", 2 }, { "sub", 0 } });
    F("FX", "Siren", { { "wob_sync", 0 }, { "wob_rate", 1.5f }, { "wob_pitch", 12 }, { "wob_shape", kTriangle }, { "cutoff", 2000 }, { "filter_env", 0 }, { "sub", 0 }, { "octave", 0 } });
    return v;
}

}} // namespace pump::wob
