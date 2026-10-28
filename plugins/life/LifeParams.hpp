// pump LIFE: parameters and factory sets (shared by the DSP and the UI).
//
// A groovebox played by a Game of Life. Eight tone voices, each a parabolic sine with PITCH, KICK (a fast pitch drop:
// tones become drums), FM (from the next voice's oscillator), RING (times the previous voice's oscillator), DECAY and
// LEVEL; global FM, drive, decay and an LFO; a multi-FX (a delay with a pitch shifter and a bipolar low/high-pass in
// its loop, feedback, its own envelope per hit). The sequencer is a grid of cells under a life rule: every step is a
// generation, and the cells that are born (or live, or die) play the voice of their colour.
#pragma once

#include "PumpParams.hpp"
#include <string>
#include <vector>

namespace pump { namespace life {

static constexpr int kTones = 8, kMaxGrid = 16;
enum SeqMode : int { kModeLoop = 0, kModeEvolve, kModeFree, kNumModes };
static const char* const kModeNames[] = { "Loop", "Evolve", "Free" };
static const char* const kRateNames[] = { "1/4", "1/8", "1/16", "1/32" };
static const int kStepsPerQuarter[] = { 1, 2, 4, 8 };
enum Rule : int { kLife = 0, kHighLife, kSeeds, kDayNight, kMaze, kMorley, kNumRules };
static const char* const kRuleNames[] = { "Life B3/S23", "HighLife B36/S23", "Seeds B2/S", "Day & Night B3678/S34678", "Maze B3/S12345", "Move B368/S245" };
static const char* const kRuleKeys[] = { "LIFE", "HIGHLIFE", "SEEDS", "DAY & NIGHT", "MAZE", "MOVE" };
// birth and survival masks (bit n: n live neighbours)
static const unsigned kBirth[kNumRules] = { 1u << 3, 1u << 3 | 1u << 6, 1u << 2, 1u << 3 | 1u << 6 | 1u << 7 | 1u << 8, 1u << 3, 1u << 3 | 1u << 6 | 1u << 8 };
static const unsigned kSurvive[kNumRules] = { 1u << 2 | 1u << 3, 1u << 2 | 1u << 3, 0u, 1u << 3 | 1u << 4 | 1u << 6 | 1u << 7 | 1u << 8, 0x3Eu, 1u << 2 | 1u << 4 | 1u << 5 };
enum Trigger : int { kTrigBirth = 0, kTrigAlive, kTrigDeath, kNumTriggers };
static const char* const kTriggerNames[] = { "Births", "Living", "Deaths" };
static const char* const kPatternNames[] = { "Drawn", "Glider", "Two gliders", "Blinkers", "Pulsar", "R-pentomino", "Acorn", "Spaceship",
                                             "Toad and beacon", "Diehard", "Galaxy", "Cross", "Random", "Random sparse", "Random dense" };
static constexpr int kNumPatterns = 15;
static const char* const kOnOff[] = { "Off", "On" };

enum Param : int {
    kVolume = 0, kDrive, kFmAll, kDecayAll, kPitchRange, kLfoRate, kLfoDepth, kWidth,
    kMode, kRate, kLength, kOffset, kSens, kXWrap, kYWrap, kColour, kCols, kRows, kRule, kTrigger, kPattern, kReseed,
    kShift, kGrain, kTime, kFeedback, kFilter, kFxDecay, kMix,
    kToneBase
};
enum ToneField : int { kTPitch = 0, kTKick, kTFm, kTRing, kTDecay, kTLevel, kTMute, kToneFields };
constexpr int tone(int t, int f) { return kToneBase + t * kToneFields + f; }
// appended after the tones (2026-10-27): FM LOOP, the old FM where every voice is modulated by its neighbour's already
// modulated sound, all eight in a ring (feedback FM: rich and noisy). Off: each voice is modulated by its neighbour's
// own oscillator, clean (the default).
static constexpr int kFmLoop = kToneBase + kTones * kToneFields;
// appended 2026-10-27: each voice's send into the FX (its own bar row, FX)
static constexpr int kFxSendBase = kFmLoop + 1;
constexpr int fxSend(int t) { return kFxSendBase + t; }
static constexpr int kNumParams = kFxSendBase + kTones;

inline const std::vector<ParamSpec>& params()
{
    static std::vector<std::string> names;
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean;
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fm", "FM (all)", "FM", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "decay", "Decay (all)", "Decay", 25, 400, 100, PUnit::Percent, PScale::Log, A, nullptr, 1 });
    v.push_back({ "pitch_range", "LFO Pitch Range", "Range", -24, 24, 2, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_rate", "LFO Rate", "Rate", 0.05f, 20, 0.5f, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo_depth", "LFO Depth", "Depth", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "width", "Stereo Width", "Width", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mode", "Sequencer Mode", "Mode", 0, kNumModes - 1, kModeLoop, PUnit::None, PScale::Linear, I, kModeNames, 1 });
    v.push_back({ "rate", "Step Rate", "Rate", 0, 3, 2, PUnit::None, PScale::Linear, I, kRateNames, 1 });
    v.push_back({ "length", "Cycle Length", "Length", 1, 64, 16, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "offset", "Cycle Offset", "Offset", 0, 63, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "sensitivity", "Sensitivity", "Sens", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "x_wrap", "Colour X Step", "X-Wrap", 0, 7, 1, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "y_wrap", "Colour Y Step", "Y-Wrap", 0, 7, 3, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "colour", "Colour Offset", "Colour", 0, 7, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "cols", "Grid Width", "Width", 4, kMaxGrid, 16, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "rows", "Grid Height", "Height", 4, kMaxGrid, 16, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "rule", "Life Rule", "Rule", 0, kNumRules - 1, kLife, PUnit::None, PScale::Linear, I, kRuleNames, 1 });
    v.push_back({ "trigger", "Trigger On", "Trigger", 0, kNumTriggers - 1, kTrigBirth, PUnit::None, PScale::Linear, I, kTriggerNames, 1 });
    v.push_back({ "pattern", "Pattern", "Pattern", 0, kNumPatterns - 1, 2, PUnit::None, PScale::Linear, I, kPatternNames, 1 });
    v.push_back({ "reseed", "Reseed When Still", "Reseed", 0, 1, 1, PUnit::None, PScale::Linear, B, kOnOff, 1 });
    v.push_back({ "fx_shift", "FX Pitch Shift", "Shift", -24, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_grain", "FX Grain", "Grain", 10, 200, 60, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "fx_time", "FX Delay Time", "Time", 1, 1000, 180, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "fx_feedback", "FX Feedback", "Feedback", 0, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_filter", "FX Filter", "Filter", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_decay", "FX Envelope Decay", "FX Dec", 5, 2000, 120, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "fx_mix", "FX Mix", "Mix", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    // the default kit: a kick, a snare-ish ring, toms and metal
    static const float pitch[kTones] = { 33, 45, 50, 57, 62, 69, 79, 86 };
    static const float kick[kTones] = { 85, 40, 60, 50, 20, 0, 0, 0 };
    static const float fm[kTones] = { 0, 60, 10, 20, 40, 70, 90, 100 };
    static const float ring[kTones] = { 0, 50, 0, 0, 30, 40, 70, 80 };
    static const float decay[kTones] = { 320, 140, 260, 220, 180, 120, 60, 40 };
    static const float level[kTones] = { 90, 70, 70, 65, 60, 55, 50, 45 };
    names.reserve(kTones * kToneFields * 2 + kTones * 2);
    for (int t = 0; t < kTones; ++t)
    {
        auto nm = [&](const char* sym, const char* full) -> std::pair<const char*, const char*> {
            names.push_back("t" + std::to_string(t + 1) + "_" + sym);
            const char* s = names.back().c_str();
            names.push_back("Tone " + std::to_string(t + 1) + " " + full);
            return { s, names.back().c_str() };
        };
        auto p = nm("pitch", "Pitch"); v.push_back({ p.first, p.second, "Pitch", 24, 96, pitch[t], PUnit::None, PScale::Linear, A, nullptr, 1 });
        p = nm("kick", "Kick");        v.push_back({ p.first, p.second, "Kick", 0, 100, kick[t], PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        p = nm("fm", "FM");            v.push_back({ p.first, p.second, "FM", 0, 100, fm[t], PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        p = nm("ring", "Ring");        v.push_back({ p.first, p.second, "Ring", 0, 100, ring[t], PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        p = nm("decay", "Decay");      v.push_back({ p.first, p.second, "Decay", 5, 2000, decay[t], PUnit::ms, PScale::Log, A, nullptr, 1 });
        p = nm("level", "Level");      v.push_back({ p.first, p.second, "Level", 0, 100, level[t], PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        p = nm("mute", "Mute");        v.push_back({ p.first, p.second, "Mute", 0, 1, 0, PUnit::None, PScale::Linear, B, kOnOff, 1 });
    }
    v.push_back({ "fm_loop", "FM Loop (noisy)", "FM Loop", 0, 1, 0, PUnit::None, PScale::Linear, B, kOnOff, 1 });
    for (int t = 0; t < kTones; ++t)
    {
        names.push_back("t" + std::to_string(t + 1) + "_fx"); const char* sy = names.back().c_str();
        names.push_back("Tone " + std::to_string(t + 1) + " FX Send"); const char* nm = names.back().c_str();
        v.push_back({ sy, nm, "FX", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    }
    return v;
    }();
    return v;
}

// Factory sets: a pattern, a rule and a cycle, with a kit to match.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    using PV = std::vector<PresetValue>;
    static std::vector<std::string> keep;
    keep.reserve(1024);
    auto ts = [&](int t, const char* f) { keep.push_back("t" + std::to_string(t) + "_" + f); return keep.back().c_str(); };
    // a whole kit at once: pitch, kick, fm, ring, decay for tones 1-8
    auto kit = [&](PV& p, const float (&pi)[8], const float (&ki)[8], const float (&fm)[8], const float (&ri)[8], const float (&de)[8]) {
        for (int t = 0; t < 8; ++t)
        {
            p.push_back({ ts(t + 1, "pitch"), pi[t] }); p.push_back({ ts(t + 1, "kick"), ki[t] }); p.push_back({ ts(t + 1, "fm"), fm[t] });
            p.push_back({ ts(t + 1, "ring"), ri[t] }); p.push_back({ ts(t + 1, "decay"), de[t] });
        }
    };
    auto F = [&](const char* sec, const char* n, PV p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("LOOPS", "Glider groove", { { "pattern", 1 }, { "length", 16 }, { "sensitivity", 100 } });
    F("LOOPS", "Blinker pulse", { { "pattern", 3 }, { "length", 8 }, { "rate", 2 }, { "sensitivity", 80 }, { "x_wrap", 2 }, { "y_wrap", 1 } });
    F("LOOPS", "Pulsar machine", { { "pattern", 4 }, { "length", 12 }, { "sensitivity", 40 }, { "fx_mix", 35 } });
    F("LOOPS", "Toad steps", { { "pattern", 8 }, { "length", 16 }, { "sensitivity", 90 }, { "colour", 3 } });
    F("LOOPS", "Galaxy spin", { { "pattern", 10 }, { "length", 8 }, { "sensitivity", 60 }, { "x_wrap", 0 }, { "y_wrap", 1 } });
    { PV p { { "pattern", 5 }, { "mode", kModeEvolve }, { "sensitivity", 55 }, { "fx_mix", 30 } };
      const float pi[8] = { 31, 38, 43, 50, 55, 62, 67, 74 }, ki[8] = { 90, 70, 60, 40, 20, 10, 0, 0 }, fm[8] = { 10, 20, 30, 40, 50, 60, 70, 80 }, ri[8] = { 0, 10, 20, 30, 40, 50, 60, 70 }, de[8] = { 400, 300, 250, 200, 160, 120, 90, 60 };
      kit(p, pi, ki, fm, ri, de); F("EVOLVING", "R-pentomino chaos", p); }
    F("EVOLVING", "Acorn bloom", { { "pattern", 6 }, { "mode", kModeEvolve }, { "sensitivity", 45 }, { "fx_shift", 12 }, { "fx_mix", 40 }, { "fx_feedback", 55 } });
    F("EVOLVING", "Diehard fade", { { "pattern", 9 }, { "mode", kModeEvolve }, { "sensitivity", 70 }, { "reseed", 1 } });
    F("EVOLVING", "Seeds storm", { { "pattern", 13 }, { "rule", kSeeds }, { "mode", kModeEvolve }, { "sensitivity", 20 }, { "decay", 60 } });
    F("EVOLVING", "HighLife drift", { { "pattern", 12 }, { "rule", kHighLife }, { "mode", kModeEvolve }, { "sensitivity", 50 }, { "lfo_depth", 30 } });
    { PV p { { "pattern", 14 }, { "rule", kDayNight }, { "mode", kModeLoop }, { "length", 32 }, { "sensitivity", 15 }, { "trigger", kTrigDeath }, { "fx_mix", 45 }, { "fx_feedback", 60 }, { "fx_time", 375 } };
      const float pi[8] = { 48, 51, 55, 58, 60, 63, 67, 70 }, ki[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }, fm[8] = { 30, 20, 40, 10, 50, 20, 60, 30 }, ri[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }, de[8] = { 900, 800, 700, 900, 600, 800, 500, 700 };
      kit(p, pi, ki, fm, ri, de); F("MELODIC", "Day and night chords", p); }
    { PV p { { "pattern", 7 }, { "mode", kModeLoop }, { "length", 32 }, { "sensitivity", 90 }, { "lfo_depth", 20 }, { "fx_shift", 7 }, { "fx_mix", 30 } };
      const float pi[8] = { 60, 62, 64, 67, 69, 72, 74, 76 }, ki[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }, fm[8] = { 15, 15, 15, 15, 15, 15, 15, 15 }, ri[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }, de[8] = { 350, 350, 350, 350, 350, 350, 350, 350 };
      kit(p, pi, ki, fm, ri, de); F("MELODIC", "Pentatonic ship", p); }
    { PV p { { "pattern", 11 }, { "rule", kMaze }, { "mode", kModeEvolve }, { "sensitivity", 30 }, { "trigger", kTrigBirth } };
      const float pi[8] = { 36, 43, 48, 55, 60, 67, 72, 79 }, ki[8] = { 30, 20, 10, 0, 0, 0, 0, 0 }, fm[8] = { 80, 70, 60, 50, 40, 30, 20, 10 }, ri[8] = { 50, 40, 30, 20, 10, 0, 0, 0 }, de[8] = { 200, 200, 180, 160, 140, 120, 100, 80 };
      kit(p, pi, ki, fm, ri, de); F("MELODIC", "Maze fifths", p); }
    F("FX", "Shimmer feedback", { { "pattern", 2 }, { "fx_shift", 12 }, { "fx_feedback", 80 }, { "fx_mix", 60 }, { "fx_time", 250 }, { "fx_filter", 40 } });
    F("FX", "Dark dub", { { "pattern", 1 }, { "fx_filter", -60 }, { "fx_feedback", 75 }, { "fx_mix", 50 }, { "fx_time", 375 }, { "drive", 40 } });
    F("FX", "Grain crush", { { "pattern", 12 }, { "mode", kModeEvolve }, { "fx_shift", -12 }, { "fx_grain", 15 }, { "fx_feedback", 60 }, { "fx_mix", 55 }, { "drive", 70 } });
    return v;
    }();
    return v;
}

}} // namespace pump::life
