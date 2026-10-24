// pump STUTTER: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pump { namespace stutter {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kTrigger = 0, kInterval, kChance, kLength, kGrid, kGate, kPitch, kDecay, kFilter, kVariation, kTape,
    kMix, kOutGain, kBypass,
    kUse0,   // eight "use in auto" switches, one per move (kUse0 + move)
    kNumParams = kUse0 + 8
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
    return v;
}

}} // namespace pump::stutter
