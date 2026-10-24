// pump ECHO: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pump { namespace echo {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kMode = 0, kTimeL, kTimeR, kSync, kNoteL, kNoteR, kLink, kFeedback, kStereo, kWidth,
    kModDepth, kModRate, kAge, kDrive, kLowCut, kHighCut, kDiffuse, kDuck, kFreeze,
    kPitch, kFine, kPitchMode, kMix, kOutGain, kBypass,
    kNumParams
};

enum Mode : int { kModeDigital = 0, kModeTape, kModeBbd, kModePitch, kModeReverse, kNumModes };
static const char* const kModeNames[] = { "Digital", "Tape", "BBD", "Pitch", "Reverse" };
enum Route : int { kRouteStereo = 0, kRoutePingPong, kRouteWide, kNumRoutes };
static const char* const kRouteNames[] = { "Stereo", "Ping-Pong", "Wide" };
enum Shift : int { kShiftOnce = 0, kShiftClimb, kNumShifts };
static const char* const kShiftNames[] = { "Once", "Climb" };

constexpr float kTimeMinMs = 1.0f, kTimeMaxMs = 4000.0f;
constexpr double kMaxSeconds = 8.0;   // synced times reach 2/1 at 60 BPM

// Filter end stops mean "off".
constexpr float kLowCutOff = 10.0f, kHighCutOff = 22000.0f;
inline bool lowCutActive(double f) { return f > kLowCutOff + 0.05; }
inline bool highCutActive(double f) { return f < kHighCutOff - 10.0; }

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    // every control glides or crossfades (modes, routings and pitch modes crossfade; times move by crossfading read
    // heads or by a tape-like glide), so all of them stay automatable
    v.push_back({ "mode", "Mode", "Mode", 0, kNumModes - 1, kModeDigital, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
    v.push_back({ "time_l", "Time L", "Time L", kTimeMinMs, kTimeMaxMs, 500, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "time_r", "Time R", "Time R", kTimeMinMs, kTimeMaxMs, 750, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sync", "Sync", "Sync", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "note_l", "Note L", "Note L", 0, kNumNotes - 1, kNote4, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "note_r", "Note R", "Note R", 0, kNumNotes - 1, kNote4D, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "link", "Link", "Link", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "feedback", "Feedback", "Feedback", 0, 120, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "stereo", "Stereo Mode", "Stereo", 0, kNumRoutes - 1, kRouteStereo, PUnit::None, PScale::Linear, A | kPInteger, kRouteNames, 1 });
    v.push_back({ "width", "Width", "Width", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mod_depth", "Mod Depth", "Mod", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mod_rate", "Mod Rate", "Rate", 0.05f, 10, 0.8f, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "age", "Age", "Age", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "low_cut", "Low Cut", "Low Cut", kLowCutOff, 2000, kLowCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "high_cut", "High Cut", "High Cut", 1000, kHighCutOff, kHighCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "diffuse", "Diffuse", "Diffuse", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "duck", "Duck", "Duck", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "freeze", "Freeze", "Freeze", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "pitch", "Pitch", "Pitch", -24, 24, 12, PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    v.push_back({ "fine", "Fine", "Fine", -100, 100, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pitch_mode", "Pitch Mode", "Pitch Mode", 0, kNumShifts - 1, kShiftOnce, PUnit::None, PScale::Linear, A | kPInteger, kShiftNames, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
}

// Factory presets. Modes: 0 digital 1 tape 2 bbd 3 pitch 4 reverse; stereo: 0 stereo 1 ping-pong 2 wide; pitch_mode:
// 0 once 1 climb. Notes: 5 1/16, 6 1/16D, 8 1/8, 9 1/8D, 10 1/4T, 11 1/4, 12 1/4D, 14 1/2, 16 1/1.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Clean quarter", { { "note_l", 11 }, { "link", 1 }, { "feedback", 30 }, { "low_cut", 80 }, { "high_cut", 12000 }, { "mix", 25 } });
    F("Dotted eighth", { { "note_l", 9 }, { "link", 1 }, { "feedback", 42 }, { "low_cut", 120 }, { "high_cut", 7500 }, { "mix", 30 } });
    F("Ping-pong eighths", { { "stereo", 1 }, { "note_l", 8 }, { "link", 1 }, { "feedback", 50 }, { "low_cut", 150 }, { "high_cut", 8000 }, { "mix", 28 } });
    F("Tape echo", { { "mode", 1 }, { "sync", 0 }, { "time_l", 340 }, { "link", 1 }, { "feedback", 48 }, { "age", 30 }, { "drive", 6 }, { "mod_depth", 15 }, { "low_cut", 90 }, { "high_cut", 6500 }, { "mix", 32 } });
    F("Worn tape", { { "mode", 1 }, { "note_l", 12 }, { "link", 1 }, { "feedback", 55 }, { "age", 75 }, { "drive", 9 }, { "mod_depth", 35 }, { "mod_rate", 0.6f }, { "low_cut", 120 }, { "high_cut", 5000 }, { "mix", 30 } });
    F("Bucket-brigade", { { "mode", 2 }, { "sync", 0 }, { "time_l", 300 }, { "time_r", 330 }, { "feedback", 45 }, { "mod_depth", 20 }, { "age", 20 }, { "mix", 30 } });
    F("Dark analog wash", { { "mode", 2 }, { "note_l", 11 }, { "note_r", 12 }, { "feedback", 72 }, { "diffuse", 50 }, { "mod_depth", 30 }, { "age", 40 }, { "low_cut", 100 }, { "mix", 28 } });
    F("Shimmer stairs", { { "mode", 3 }, { "pitch", 12 }, { "pitch_mode", 1 }, { "note_l", 12 }, { "link", 1 }, { "feedback", 55 }, { "diffuse", 60 }, { "low_cut", 200 }, { "high_cut", 9000 }, { "mix", 30 } });
    F("Falling octaves", { { "mode", 3 }, { "pitch", -12 }, { "pitch_mode", 1 }, { "note_l", 11 }, { "link", 1 }, { "stereo", 1 }, { "feedback", 52 }, { "low_cut", 60 }, { "mix", 30 } });
    F("Fifth echoes", { { "mode", 3 }, { "pitch", 7 }, { "note_l", 9 }, { "note_r", 11 }, { "feedback", 38 }, { "high_cut", 9000 }, { "mix", 28 } });
    F("Reverse swell", { { "mode", 4 }, { "note_l", 14 }, { "link", 1 }, { "feedback", 30 }, { "diffuse", 30 }, { "low_cut", 120 }, { "mix", 35 } });
    F("Wide diffused", { { "stereo", 2 }, { "note_l", 8 }, { "note_r", 9 }, { "feedback", 45 }, { "diffuse", 70 }, { "low_cut", 150 }, { "high_cut", 8000 }, { "mix", 25 } });
    S("Vocals: slapback", { { "mode", 1 }, { "sync", 0 }, { "time_l", 105 }, { "link", 1 }, { "feedback", 8 }, { "age", 15 }, { "low_cut", 150 }, { "high_cut", 6500 }, { "mix", 25 } });
    S("Vocals: ducked quarter", { { "stereo", 1 }, { "note_l", 11 }, { "link", 1 }, { "feedback", 35 }, { "duck", 60 }, { "low_cut", 220 }, { "high_cut", 7000 }, { "mix", 35 } });
    S("Vocals: throw", { { "note_l", 12 }, { "link", 1 }, { "feedback", 48 }, { "duck", 40 }, { "diffuse", 25 }, { "low_cut", 300 }, { "high_cut", 5000 }, { "mix", 45 } });
    S("Guitar: tape eighths", { { "mode", 1 }, { "note_l", 9 }, { "link", 1 }, { "feedback", 40 }, { "age", 25 }, { "drive", 4 }, { "mod_depth", 12 }, { "low_cut", 120 }, { "high_cut", 6000 }, { "mix", 30 } });
    S("Guitar: analog lead", { { "mode", 2 }, { "sync", 0 }, { "time_l", 380 }, { "time_r", 410 }, { "feedback", 42 }, { "mod_depth", 25 }, { "mod_rate", 1.2f }, { "mix", 30 } });
    S("Drums: ping-pong 1/16", { { "stereo", 1 }, { "note_l", 5 }, { "link", 1 }, { "feedback", 35 }, { "low_cut", 250 }, { "high_cut", 6000 }, { "mix", 20 } });
    S("Drums: tape room", { { "mode", 1 }, { "sync", 0 }, { "time_l", 68 }, { "time_r", 81 }, { "feedback", 25 }, { "diffuse", 45 }, { "age", 20 }, { "low_cut", 150 }, { "mix", 20 } });
    S("Bass: tight slap", { { "sync", 0 }, { "time_l", 90 }, { "link", 1 }, { "feedback", 10 }, { "low_cut", 250 }, { "high_cut", 5000 }, { "mix", 18 } });
    S("Keys: wide chorus echo", { { "mode", 2 }, { "stereo", 2 }, { "note_l", 8 }, { "link", 1 }, { "feedback", 30 }, { "mod_depth", 45 }, { "mod_rate", 0.7f }, { "mix", 30 } });
    S("Synths: dub tape", { { "mode", 1 }, { "note_l", 12 }, { "link", 1 }, { "feedback", 70 }, { "drive", 8 }, { "age", 35 }, { "low_cut", 250 }, { "high_cut", 3500 }, { "duck", 20 }, { "mix", 35 } });
    S("Bus: subtle depth", { { "stereo", 2 }, { "note_l", 8 }, { "link", 1 }, { "feedback", 20 }, { "diffuse", 50 }, { "low_cut", 300 }, { "high_cut", 6000 }, { "mix", 12 } });
    C("Runaway tape", { { "mode", 1 }, { "note_l", 12 }, { "link", 1 }, { "feedback", 112 }, { "drive", 10 }, { "age", 45 }, { "low_cut", 200 }, { "high_cut", 4500 }, { "mix", 35 } });
    C("Shimmer cathedral", { { "mode", 3 }, { "pitch", 12 }, { "pitch_mode", 1 }, { "note_l", 14 }, { "note_r", 15 }, { "feedback", 72 }, { "diffuse", 85 }, { "low_cut", 300 }, { "high_cut", 10000 }, { "mix", 45 } });
    C("Detune doubler", { { "mode", 3 }, { "pitch", 0 }, { "fine", 14 }, { "sync", 0 }, { "time_l", 32 }, { "time_r", 41 }, { "feedback", 0 }, { "mix", 40 } });
    C("Reverse ping-pong", { { "mode", 4 }, { "stereo", 1 }, { "note_l", 11 }, { "link", 1 }, { "feedback", 40 }, { "low_cut", 150 }, { "mix", 35 } });
    C("Broken BBD", { { "mode", 2 }, { "note_l", 11 }, { "note_r", 12 }, { "feedback", 60 }, { "age", 100 }, { "mod_depth", 40 }, { "mod_rate", 2.5f }, { "mix", 35 } });
    C("Lo-fi digital", { { "note_l", 9 }, { "link", 1 }, { "feedback", 50 }, { "age", 80 }, { "high_cut", 5000 }, { "mix", 32 } });
    return v;
}

}} // namespace pump::echo
