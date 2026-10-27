// pump STUTTER: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace pump { namespace stutter {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kTrigger = 0, kInterval, kChance, kLength, kGrid, kGate, kPitch, kDecay, kFilter, kVariation, kTape,
    kMix, kOutGain, kBypass,
    kUse0,   // eight "use in auto" switches, one per move (kUse0 + move)
    kPatternOn = kUse0 + 8, kPatternSel, kPatStep,   // the pattern (added 2026-10-26)
    kNumParams
};

// The moves. Each plays from the buffer that records the input all the time.
//   Repeat   loops the slice (GRID) that starts where the move starts; GATE, PITCH per repeat, DECAY, FILTER per repeat
//   Roll     the same, but every repeat is half as long as the one before (an accelerating ratchet)
//   Reverse  plays what came before it backwards
//   Tape     the transport slows to a stop (TAPE = how much of the move it takes)
//   Half     half speed (an octave down)
//   Shuffle  every slice plays a different slice from the last bar
//   Gate     chops the live signal on the grid (GATE = how long each slice stays open)
//   Glitch   tiny loops (3-60 ms) that jump, change pitch and flip around every half or quarter slice
enum Move : int { kRepeat = 0, kRoll, kReverse, kTapeStop, kHalf, kShuffle, kGateMove, kGlitch, kNumMoves };
static const char* const kMoveNames[] = { "Repeat", "Roll", "Reverse", "Tape stop", "Half speed", "Shuffle", "Gate", "Glitch" };
static const char* const kMoveKeys[] = { "REPEAT", "ROLL", "REVERSE", "TAPE STOP", "HALF", "SHUFFLE", "GATE", "GLITCH" };
static const char* const kMoveSymbols[] = { "use_repeat", "use_roll", "use_reverse", "use_tape", "use_half", "use_shuffle", "use_gate", "use_glitch" };
enum Trigger : int { kAuto = 0, kManual, kNumTriggers };
static const char* const kTriggerNames[] = { "Auto", "Manual" };

// ---- patterns ----------------------------------------------------------------------------------------------------
// A pattern is up to 32 steps on its own grid (STEP: a 16th by default). A step can start a move (or not); the move
// lasts LEN steps (the steps it covers start nothing), repeats its own GRID slice (0 = the GRID control's) and is
// TRANSPOSED (semitones: from the second repeat on for repeat and roll, which play the live slice first; all of it for
// the others). Twelve factory patterns, and USER: the one you draw (kept with the session as text).
constexpr int kMaxSteps = 32;
struct Step { int move = -1, len = 1, grid = 0, transpose = 0; };   // grid: 0 = the GRID control, else a NoteIndex + 1
struct Pattern { int length = 16; Step s[kMaxSteps]; };
constexpr int kNumFactoryPatterns = 12, kUserPattern = kNumFactoryPatterns;
static const char* const kPatternNames[kNumFactoryPatterns + 1] = { "Empty", "Build-up", "End-of-bar roll", "3-3-2 stutter", "Reverse offbeats", "Half-time back half",
    "Tape stop drop", "Glitch funk", "Melodic repeats", "Gate the bar", "Shuffle chop", "Fill", "User" };
inline const Pattern& factoryPattern(int i)
{
    static const std::array<Pattern, kNumFactoryPatterns> P = [] {
        std::array<Pattern, kNumFactoryPatterns> p;
        for (Pattern& q : p) { q.length = 16; for (Step& st : q.s) st = Step { -1, 1, 0, 0 }; }   // (set out: brace-initialising the array zeroed some steps)
        auto put = [](Pattern& q, int at, int move, int len, int grid = 0, int tr = 0) { q.s[at].move = move; q.s[at].len = len; q.s[at].grid = grid; q.s[at].transpose = tr; };
        const int g8 = kNote8 + 1, g16 = kNote16 + 1, g32 = kNote32 + 1;
        put(p[1], 0, kRepeat, 4, g8); put(p[1], 4, kRepeat, 4, g16); put(p[1], 8, kRepeat, 4, g32); put(p[1], 12, kRoll, 4, g16);
        put(p[2], 12, kRoll, 4, g8);
        put(p[3], 0, kRepeat, 3, g16); put(p[3], 3, kRepeat, 3, g16); put(p[3], 6, kRepeat, 2, g16);
        for (int k = 2; k < 16; k += 4) put(p[4], k, kReverse, 2);
        put(p[5], 8, kHalf, 8);
        put(p[6], 12, kTapeStop, 4);
        put(p[7], 1, kGlitch, 1, g32); put(p[7], 3, kGlitch, 1, g32); put(p[7], 6, kGlitch, 2, g16); put(p[7], 11, kGlitch, 1, g32); put(p[7], 14, kGlitch, 2, g16);
        put(p[8], 0, kRepeat, 4, g16, 0); put(p[8], 4, kRepeat, 4, g16, 3); put(p[8], 8, kRepeat, 4, g16, 7); put(p[8], 12, kRepeat, 4, g16, 12);
        put(p[9], 0, kGateMove, 16, g16);
        put(p[10], 0, kShuffle, 16, g8);
        put(p[11], 8, kRepeat, 2, g8); put(p[11], 10, kRepeat, 2, g16); put(p[11], 12, kRoll, 2, g16); put(p[11], 14, kGlitch, 2, g32);
        return p;
    }();
    return P[(size_t)clampv(i, 0, kNumFactoryPatterns - 1)];
}
// the text a pattern is kept as: "length;move,len,grid,transpose;..." (32 steps)
inline std::string patternToText(const Pattern& p)
{
    std::string t = std::to_string(p.length);
    for (int i = 0; i < kMaxSteps; ++i) { const Step& s = p.s[i]; t += ";" + std::to_string(s.move) + "," + std::to_string(s.len) + "," + std::to_string(s.grid) + "," + std::to_string(s.transpose); }
    return t;
}
inline Pattern patternFromText(const char* t)
{
    Pattern p;
    if (!t || !*t) return p;
    p.length = clampv(std::atoi(t), 1, kMaxSteps);
    const char* c = std::strchr(t, ';');
    for (int i = 0; i < kMaxSteps && c; ++i)
    {
        ++c;
        int v[4] = { -1, 1, 0, 0 };
        for (int k = 0; k < 4; ++k) { v[k] = std::atoi(c); while (*c && *c != ',' && *c != ';') ++c; if (*c == ',') ++c; else break; }
        p.s[i].move = clampv(v[0], -1, kNumMoves - 1); p.s[i].len = clampv(v[1], 1, kMaxSteps); p.s[i].grid = clampv(v[2], 0, kNumNotes); p.s[i].transpose = clampv(v[3], -24, 24);
        c = std::strchr(c, ';');
    }
    return p;
}

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    v.push_back({ "trigger", "Trigger", "Trigger", 0, kNumTriggers - 1, kAuto, PUnit::None, PScale::Linear, A | kPInteger, kTriggerNames, 1 });
    v.push_back({ "interval", "Interval", "Interval", 0, kNumNotes - 1, kNote1, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "chance", "Chance", "Chance", 0, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "length", "Length", "Length", 0, kNumNotes - 1, kNote4, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "grid", "Grid", "Grid", 0, kNumNotes - 1, kNote16, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "gate", "Gate", "Gate", 5, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pitch", "Pitch per Repeat", "Pitch", -12, 12, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "decay", "Decay per Repeat", "Decay", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter", "Filter per Repeat", "Filter", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "variation", "Variation", "Variation", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tape", "Tape Stop Time", "Tape", 10, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    for (int m = 0; m < kNumMoves; ++m)
        v.push_back({ kMoveSymbols[m], kMoveNames[m], kMoveKeys[m], 0, 1, m <= kTapeStop ? 1.0f : 0.0f, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "pattern_on", "Pattern", "Pattern", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "pattern", "Pattern Select", "Pattern", 0, kUserPattern, 1, PUnit::None, PScale::Linear, A | kPInteger, kPatternNames, 1 });
    v.push_back({ "pattern_step", "Pattern Step", "Step", 0, kNumNotes - 1, kNote16, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    return v;
}

// Factory presets. Moves: 0 repeat 1 roll 2 reverse 3 tape 4 half 5 shuffle 6 gate 7 glitch.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    // which moves auto may pick (the rest off)
    auto uses = [](std::vector<int> on, std::vector<PresetValue> p) {
        for (int m = 0; m < kNumMoves; ++m)
        {
            bool u = false; for (int o : on) u = u || o == m;
            p.push_back({ kMoveSymbols[m], u ? 1.0f : 0.0f });
        }
        return p;
    };
    auto F = [&](const char* n, std::vector<int> on, std::vector<PresetValue> p) { v.push_back({ n, uses(on, std::move(p)) }); };
    auto S = [&](const char* n, std::vector<int> on, std::vector<PresetValue> p) { v.push_back({ n, uses(on, std::move(p)), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<int> on, std::vector<PresetValue> p) { v.push_back({ n, uses(on, std::move(p)), "CREATIVE" }); };
    v.push_back({ "Default", {} });
    F("Sixteenth repeats", { 0 }, { { "grid", kNote16 }, { "length", kNote4 }, { "chance", 40 } });
    F("Eighth repeats, pitched down", { 0 }, { { "grid", kNote8 }, { "length", kNote2 }, { "pitch", -2 }, { "decay", 15 } });
    F("Ratchet rolls", { 1 }, { { "grid", kNote8 }, { "length", kNote4 }, { "chance", 45 } });
    F("Reverse tails", { 2 }, { { "length", kNote4 }, { "interval", kNote2 }, { "chance", 30 } });
    F("Tape stops", { 3 }, { { "length", kNote2 }, { "interval", kNote2Bars }, { "chance", 50 }, { "tape", 80 } });
    F("Half-time drops", { 4 }, { { "length", kNote2 }, { "interval", kNote2Bars }, { "chance", 40 } });
    F("Beat shuffle", { 5 }, { { "grid", kNote8 }, { "length", kNote1 }, { "interval", kNote1 }, { "chance", 60 } });
    F("Trance gate", { 6 }, { { "grid", kNote16 }, { "gate", 45 }, { "length", kNote1 }, { "interval", kNote1 }, { "chance", 100 } });
    F("Glitch spray", { 7 }, { { "grid", kNote8 }, { "length", kNote4 }, { "chance", 45 }, { "variation", 60 } });
    F("Everything, now and then", { 0, 1, 2, 3, 4, 5, 6, 7 }, { { "chance", 25 }, { "variation", 40 } });
    F("Manual pads only", { 0, 1, 2, 3 }, { { "trigger", 1 } });
    S("Drums: fills at the bar end", { 0, 1 }, { { "interval", kNote1 }, { "length", kNote4 }, { "grid", kNote16 }, { "chance", 55 }, { "filter", -20 } });
    S("Drums: shuffled breaks", { 5, 2 }, { { "grid", kNote16 }, { "length", kNote2 }, { "chance", 50 }, { "variation", 30 } });
    S("Vocals: stutter chops", { 0, 6 }, { { "grid", kNote16 }, { "gate", 60 }, { "length", kNote8 }, { "interval", kNote2 }, { "chance", 30 } });
    S("Vocals: rising repeats", { 0 }, { { "grid", kNote8 }, { "pitch", 1 }, { "length", kNote2 }, { "chance", 30 } });
    S("Synths: tape-stop endings", { 3 }, { { "interval", kNote2Bars }, { "length", kNote4 }, { "chance", 60 }, { "tape", 100 } });
    S("Bass: half-time wobble", { 4, 6 }, { { "grid", kNote8 }, { "gate", 70 }, { "length", kNote2 }, { "chance", 35 } });
    S("Pads: reverse swells", { 2 }, { { "length", kNote1 }, { "interval", kNote2Bars }, { "chance", 50 }, { "mix", 60 } });
    S("Bus: occasional repeat", { 0, 1 }, { { "chance", 15 }, { "length", kNote8 }, { "grid", kNote16 } });
    C("Disappearing echoes", { 0 }, { { "grid", kNote8D }, { "decay", 35 }, { "filter", -45 }, { "length", kNote1 }, { "chance", 40 } });
    C("Climbing ladder", { 0, 1 }, { { "grid", kNote16 }, { "pitch", 2 }, { "filter", 30 }, { "length", kNote2 }, { "chance", 40 } });
    C("Broken CD", { 7, 0 }, { { "grid", kNote32 }, { "variation", 90 }, { "chance", 70 }, { "length", kNote8 }, { "interval", kNote4 } });
    C("Chopped and slowed", { 4, 3, 6 }, { { "grid", kNote8T }, { "gate", 55 }, { "chance", 45 }, { "variation", 35 } });
    C("Total chaos", { 0, 1, 2, 3, 4, 5, 6, 7 }, { { "interval", kNote4 }, { "chance", 70 }, { "variation", 100 }, { "length", kNote8 } });
    auto P = [&](const char* n, int pat, std::vector<PresetValue> p) { p.push_back({ "pattern_on", 1.0f }); p.push_back({ "pattern", (float)pat }); v.push_back({ n, uses({}, std::move(p)), "PATTERNS" }); };
    P("Pattern: build-up", 1, {});
    P("Pattern: bar-end roll", 2, {});
    P("Pattern: 3-3-2", 3, {});
    P("Pattern: reverse offbeats", 4, {});
    P("Pattern: half-time back half", 5, {});
    P("Pattern: tape stop drop", 6, {});
    P("Pattern: glitch funk", 7, {});
    P("Pattern: melodic repeats", 8, { { "decay", 10 } });
    P("Pattern: gate the bar", 9, { { "gate", 50 } });
    P("Pattern: shuffle chop", 10, {});
    P("Pattern: fill", 11, { { "filter", -25 } });
    return v;
}

}} // namespace pump::stutter
