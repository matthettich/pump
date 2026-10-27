// pump FOLD: parameters and factory voices (shared by the DSP and the UI).
//
// A west-coast-style synth: a complex oscillator (a principal oscillator and a modulation oscillator that frequency
// modulates it, amplitude modulates it and moves its folding), a multi-stage wavefolder (FOLD: how hard, SYMMETRY: a
// bias that brings in the even harmonics, EDGE: from round folds to sharp ones, STAGES), and a low-pass gate: a filter
// and an amplifier opened together by a light-dependent resistor's slow, uneven response, so that a short strike
// rings out like a struck object (the gate's own decay does it). A function generator (rise and fall; once per note,
// held while the key is down, or cycling) strikes the gate and can move the folding, the FM and the pitch; a
// random source (stepped to smooth) does the same. Everything up to the gate runs at four times the sample rate.
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace fold {

enum ModWave : int { kModSine = 0, kModTri, kModSaw, kModSquare, kNumModWaves };
static const char* const kModWaveNames[] = { "Sine", "Triangle", "Saw", "Square" };
enum GateMode : int { kGateCombo = 0, kGateLp, kGateVca, kNumGateModes };
static const char* const kGateModeNames[] = { "Combo", "Low-pass", "VCA" };
enum FuncMode : int { kFuncTrig = 0, kFuncSustain, kFuncCycle, kNumFuncModes };
static const char* const kFuncModeNames[] = { "Strike", "Sustain", "Cycle" };
static const char* const kVoiceModeNames[] = { "Poly", "Mono", "Legato" };

enum Param : int {
    kShape = 0, kModWaveP, kModRatio, kModDetune, kFmIndex, kAmDepth, kModFold,
    kFold, kSymmetry, kEdge, kStages,
    kGateModeP, kGateOpen, kVactrol, kGateReso,
    kFuncModeP, kRise, kFall, kFuncGate, kFuncFold, kFuncFm, kFuncPitch,
    kRandRate, kRandSmooth, kRandFold, kRandFm, kRandPitch,
    kVelocity, kSpread, kVoices, kVoiceMode, kGlide, kOctave, kFine, kVolume,
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    v.push_back({ "shape", "Principal Shape", "Shape", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mod_wave", "Modulator Wave", "Mod Wave", 0, kNumModWaves - 1, kModSine, PUnit::None, PScale::Linear, I, kModWaveNames, 1 });
    v.push_back({ "mod_ratio", "Modulator Ratio", "Ratio", 0.125f, 16, 2, PUnit::None, PScale::Log, A, nullptr, 3 });
    v.push_back({ "mod_detune", "Modulator Detune", "Detune", -20, 20, 0, PUnit::Hz, PScale::Linear, A, nullptr, 2 });
    v.push_back({ "fm_index", "FM Index", "FM", 0, 100, 0, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "am_depth", "AM Depth", "AM", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mod_fold", "Modulator to Fold", "Mod>Fold", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fold", "Fold", "Fold", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "symmetry", "Symmetry", "Symmetry", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "edge", "Edge", "Edge", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "stages", "Fold Stages", "Stages", 1, 6, 4, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "gate_mode", "Gate Mode", "Gate", 0, kNumGateModes - 1, kGateCombo, PUnit::None, PScale::Linear, I, kGateModeNames, 1 });
    v.push_back({ "gate_open", "Gate Offset", "Offset", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vactrol", "Gate Response", "Response", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "gate_reso", "Gate Resonance", "Reso", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "func_mode", "Function Mode", "Function", 0, kNumFuncModes - 1, kFuncTrig, PUnit::None, PScale::Linear, I, kFuncModeNames, 1 });
    v.push_back({ "rise", "Rise", "Rise", 0.2f, 5000, 1, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "fall", "Fall", "Fall", 1, 10000, 150, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "func_gate", "Function to Gate", "Gate", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "func_fold", "Function to Fold", "Fold", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "func_fm", "Function to FM", "FM", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "func_pitch", "Function to Pitch", "Pitch", -24, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "rand_rate", "Random Rate", "Rate", 0.05f, 40, 2, PUnit::Hz, PScale::Log, A, nullptr, 2 });
    v.push_back({ "rand_smooth", "Random Smooth", "Smooth", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "rand_fold", "Random to Fold", "Fold", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "rand_fm", "Random to FM", "FM", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "rand_pitch", "Random to Pitch", "Pitch", 0, 12, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "velocity", "Velocity", "Velocity", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spread", "Stereo Spread", "Spread", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voices", "Voices", "Voices", 1, 8, 8, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "voice_mode", "Voice Mode", "Mode", 0, 2, 0, PUnit::None, PScale::Linear, I, kVoiceModeNames, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "octave", "Octave", "Oct", -3, 3, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "fine", "Fine Tune", "Fine", -100, 100, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
    }();
    return v;
}

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("STRUCK", "Bongo ping", { { "fold", 20 }, { "rise", 0.3f }, { "fall", 20 }, { "vactrol", 55 }, { "fm_index", 10 }, { "mod_ratio", 1.5f }, { "volume", 5 } });
    F("STRUCK", "Wood tones", { { "fold", 45 }, { "symmetry", 25 }, { "rise", 0.5f }, { "fall", 60 }, { "vactrol", 35 }, { "func_fold", 40 }, { "volume", 4 } });
    F("STRUCK", "Metal ping", { { "fold", 35 }, { "fm_index", 45 }, { "mod_ratio", 3.41f }, { "rise", 0.3f }, { "fall", 40 }, { "vactrol", 70 }, { "edge", 70 }, { "volume", 6 } });
    F("STRUCK", "Folded marimba", { { "fold", 55 }, { "edge", 10 }, { "stages", 3 }, { "rise", 0.5f }, { "fall", 120 }, { "vactrol", 45 }, { "func_fold", 30 } });
    F("STRUCK", "Rain on a roof", { { "fold", 40 }, { "fm_index", 30 }, { "mod_ratio", 2.76f }, { "rise", 0.2f }, { "fall", 10 }, { "vactrol", 30 }, { "rand_fold", 60 }, { "rand_pitch", 7 }, { "rand_rate", 8 }, { "volume", 9 } });
    F("LEAD", "West lead", { { "func_mode", kFuncSustain }, { "fold", 50 }, { "symmetry", 15 }, { "rise", 10 }, { "fall", 400 }, { "func_fold", 35 }, { "voice_mode", 2 }, { "glide", 40 } });
    F("LEAD", "Complex FM lead", { { "func_mode", kFuncSustain }, { "fold", 30 }, { "fm_index", 40 }, { "mod_ratio", 1.0f }, { "mod_detune", 0.7f }, { "func_fm", 40 }, { "rise", 5 }, { "fall", 500 }, { "voice_mode", 1 } });
    F("LEAD", "Sharp folds", { { "func_mode", kFuncSustain }, { "fold", 70 }, { "edge", 100 }, { "stages", 6 }, { "mod_fold", 30 }, { "mod_ratio", 0.5f }, { "rise", 3 }, { "fall", 300 }, { "voice_mode", 2 } });
    F("BASS", "Folded bass", { { "func_mode", kFuncSustain }, { "octave", -1 }, { "fold", 40 }, { "symmetry", 30 }, { "func_fold", 50 }, { "rise", 1 }, { "fall", 250 }, { "gate_mode", kGateLp }, { "voice_mode", 2 }, { "glide", 30 }, { "volume", -3 } });
    F("BASS", "Rubber bass", { { "octave", -1 }, { "fold", 25 }, { "fm_index", 20 }, { "mod_ratio", 1 }, { "rise", 1 }, { "fall", 300 }, { "vactrol", 60 }, { "func_fm", 50 }, { "voice_mode", 1 }, { "volume", 2 } });
    F("MOTION", "Cycling folds", { { "func_mode", kFuncCycle }, { "rise", 120 }, { "fall", 380 }, { "fold", 35 }, { "func_fold", 60 }, { "func_gate", 60 }, { "gate_open", 30 }, { "spread", 70 } });
    F("MOTION", "Uncertain arps", { { "func_mode", kFuncCycle }, { "rise", 2 }, { "fall", 110 }, { "fold", 40 }, { "rand_fold", 50 }, { "rand_pitch", 12 }, { "rand_rate", 9 }, { "vactrol", 40 } });
    F("MOTION", "Drifting complex", { { "func_mode", kFuncSustain }, { "rise", 600 }, { "fall", 1500 }, { "fold", 30 }, { "fm_index", 25 }, { "mod_ratio", 0.501f }, { "rand_fold", 30 }, { "rand_fm", 30 }, { "rand_smooth", 100 }, { "rand_rate", 0.4f }, { "spread", 80 }, { "volume", -2 } });
    F("DRONE", "Folded drone", { { "func_mode", kFuncSustain }, { "rise", 1500 }, { "fall", 3000 }, { "fold", 60 }, { "mod_fold", 40 }, { "mod_ratio", 0.125f }, { "am_depth", 30 }, { "spread", 90 }, { "volume", -2 } });
    F("DRONE", "Gong of folds", { { "rise", 0.5f }, { "fall", 2500 }, { "vactrol", 90 }, { "fold", 50 }, { "fm_index", 35 }, { "mod_ratio", 1.414f }, { "func_fold", 50 }, { "octave", -1 }, { "volume", -4 } });
    return v;
    }();
    return v;
}

}} // namespace pump::fold
