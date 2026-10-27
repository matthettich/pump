// pump TUNE: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Pitch.hpp"
#include <vector>

namespace pump { namespace tune {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kMode = 0, kKey, kScale, kMidi, kRange,
    kRetune, kAmount, kHumanize, kFlex, kVibDepth, kVibRate, kTranspose,
    kFormant, kFormantFollow,
    kHarmMode, kH1, kH2, kH3, kH4, kH1Level, kH2Level, kH3Level, kH4Level, kH1Pan, kH2Pan, kH3Pan, kH4Pan,
    kHarmFormant, kHarmHumanize,
    kLead, kHarmLevel, kOutGain, kBypass,
    kNumParams
};

// Live: notes from the key / scale (or MIDI); Graph: the notes captured and edited on the graph
enum Mode : int { kLive = 0, kGraph, kNumModes };
static const char* const kModeNames[] = { "Live", "Graph" };
// What MIDI sent to TUNE does
enum Midi : int { kMidiOff = 0, kMidiLead, kMidiHarmony, kNumMidi };
static const char* const kMidiNames[] = { "Off", "Lead notes", "Harmony notes" };
// The voice's range: the pitch tracker's limits (and the latency: low voices need longer grains)
enum Range : int { kRangeHigh = 0, kRangeMid, kRangeLow, kNumRanges };
static const char* const kRangeNames[] = { "High", "Mid", "Low" };
inline double rangeMinHz(int r) { return r == kRangeHigh ? 100.0 : (r == kRangeMid ? 70.0 : 50.0); }
inline double rangeMaxHz(int r) { return r == kRangeHigh ? 1200.0 : (r == kRangeMid ? 900.0 : 600.0); }
// Harmony: intervals in scale steps (Scale), in semitones (Fixed), or the MIDI notes held
enum Harm : int { kHarmOff = 0, kHarmScale, kHarmFixed, kNumHarm };
static const char* const kHarmNames[] = { "Off", "Scale", "Fixed" };
constexpr int kVoices = 4;

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    v.push_back({ "mode", "Mode", "Mode", 0, kNumModes - 1, kLive, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
    v.push_back({ "key", "Key", "Key", 0, 11, 0, PUnit::None, PScale::Linear, A | kPInteger, pitch::kKeyNames, 1 });
    v.push_back({ "scale", "Scale", "Scale", 0, pitch::kNumScales - 1, pitch::kChromatic, PUnit::None, PScale::Linear, A | kPInteger, pitch::kScaleNames, 1 });
    v.push_back({ "midi", "MIDI", "MIDI", 0, kNumMidi - 1, kMidiOff, PUnit::None, PScale::Linear, A | kPInteger, kMidiNames, 1 });
    // the range sets the latency: not automatable
    v.push_back({ "range", "Voice Range", "Range", 0, kNumRanges - 1, kRangeMid, PUnit::None, PScale::Linear, kPInteger, kRangeNames, 1 });
    v.push_back({ "retune", "Retune Speed", "Retune", 0, 400, 20, PUnit::ms, PScale::Power, A, nullptr, 2.0f });
    v.push_back({ "amount", "Amount", "Amount", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "humanize", "Humanize", "Humanize", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "flex", "Flex", "Flex", 0, 100, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vib_depth", "Vibrato Depth", "Vibrato", 0, 100, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vib_rate", "Vibrato Rate", "Rate", 2, 9, 5.5f, PUnit::Hz, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "transpose", "Transpose", "Transpose", -24, 24, 0, PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    v.push_back({ "formant", "Formant", "Formant", -12, 12, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "formant_follow", "Formants Follow Pitch", "Follow", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "harm_mode", "Harmony", "Harmony", 0, kNumHarm - 1, kHarmOff, PUnit::None, PScale::Linear, A | kPInteger, kHarmNames, 1 });
    const char* sym[4] = { "h1", "h2", "h3", "h4" };
    const char* nm[4] = { "Harmony 1", "Harmony 2", "Harmony 3", "Harmony 4" };
    const float defI[4] = { 2, -3, 4, 7 };
    for (int i = 0; i < 4; ++i) v.push_back({ sym[i], nm[i], nm[i], -12, 12, defI[i], PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    const char* lsym[4] = { "h1_level", "h2_level", "h3_level", "h4_level" };
    const char* lnm[4] = { "Harmony 1 Level", "Harmony 2 Level", "Harmony 3 Level", "Harmony 4 Level" };
    const float defL[4] = { 70, 60, 0, 0 };
    for (int i = 0; i < 4; ++i) v.push_back({ lsym[i], lnm[i], "Level", 0, 100, defL[i], PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    const char* psym[4] = { "h1_pan", "h2_pan", "h3_pan", "h4_pan" };
    const char* pnm[4] = { "Harmony 1 Pan", "Harmony 2 Pan", "Harmony 3 Pan", "Harmony 4 Pan" };
    const float defP[4] = { -45, 45, -80, 80 };
    for (int i = 0; i < 4; ++i) v.push_back({ psym[i], pnm[i], "Pan", -100, 100, defP[i], PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "harm_formant", "Harmony Formant", "Formant", -12, 12, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "harm_humanize", "Harmony Humanize", "Humanize", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lead", "Lead Level", "Lead", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "harm_level", "Harmony Level", "Harmony", -24, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. scale: 0 chromatic 1 major 2 minor 3 harmonic minor ...; harm_mode: 0 off 1 scale 2 fixed;
// midi: 0 off 1 lead 2 harmony
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto H = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "HARMONY" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Natural correction", { { "retune", 80 }, { "humanize", 60 }, { "flex", 15 }, { "scale", 1 } });
    F("Transparent touch-up", { { "retune", 140 }, { "humanize", 80 }, { "amount", 70 }, { "flex", 25 }, { "scale", 1 } });
    F("Tight pop", { { "retune", 25 }, { "humanize", 30 }, { "scale", 1 } });
    F("Hard tune", { { "retune", 0 }, { "humanize", 0 }, { "scale", 1 } });
    F("Hard tune, minor", { { "retune", 0 }, { "humanize", 0 }, { "scale", 2 }, { "key", 9 } });
    F("Play the notes (MIDI)", { { "midi", 1 }, { "retune", 5 } });
    F("Low voice", { { "range", 2 }, { "retune", 40 }, { "humanize", 40 }, { "scale", 1 } });
    H("Third above", { { "scale", 1 }, { "harm_mode", 1 }, { "h1", 2 }, { "h1_level", 75 }, { "h2_level", 0 }, { "h1_pan", 0 } });
    H("Thirds and fifths", { { "scale", 1 }, { "harm_mode", 1 }, { "h1", 2 }, { "h2", 4 }, { "h1_level", 70 }, { "h2_level", 60 } });
    H("Wide stack", { { "scale", 1 }, { "harm_mode", 1 }, { "h1", 2 }, { "h2", -3 }, { "h3", 4 }, { "h4", -7 }, { "h1_level", 65 }, { "h2_level", 60 }, { "h3_level", 50 }, { "h4_level", 55 }, { "harm_level", -4 } });
    H("Octave doubles", { { "harm_mode", 2 }, { "h1", 12 }, { "h2", -12 }, { "h1_level", 45 }, { "h2_level", 55 }, { "h1_pan", -30 }, { "h2_pan", 30 } });
    H("Power fifths", { { "harm_mode", 2 }, { "h1", 7 }, { "h2", -5 }, { "h1_level", 60 }, { "h2_level", 50 } });
    H("Choir from MIDI", { { "midi", 2 }, { "harm_mode", 1 }, { "h1_level", 70 }, { "h2_level", 70 }, { "h3_level", 70 }, { "h4_level", 70 }, { "harm_humanize", 60 }, { "harm_level", -6 } });
    C("Robot", { { "retune", 0 }, { "humanize", 0 }, { "scale", 13 }, { "formant", -2 } });
    C("Chipmunk", { { "transpose", 12 }, { "formant_follow", 1 } });
    C("Giant", { { "transpose", -7 }, { "formant", -5 } });
    C("Gender flip up", { { "transpose", 5 }, { "formant", 4 } });
    C("Gender flip down", { { "transpose", -5 }, { "formant", -4 } });
    C("Wide vibrato", { { "retune", 0 }, { "vib_depth", 60 }, { "vib_rate", 6 }, { "scale", 1 } });
    return v;
    }();
    return v;
}

}} // namespace pump::tune
