// pump CHOP: parameters and factory presets (shared by the DSP and the UI).
//
// A live chopper. The input is recorded all the time; the last bar (or LENGTH: half a bar to four) is cut into
// SLICES (8 or 16). Each of eight pads, while it is held, plays its slice (and on through the bar from there) instead
// of the live input; hold two and it loops between their slices. While a pad is down the captured bar stays put (HOLD
// keeps it put after, too: chop the same bar again and again); with no pad down it follows the song. Each pad has its
// own slice, pitch, reverse, an effect (low-pass, high-pass, crush, ratchet, tape stop, gate, drive) with an amount,
// and a level. Pads answer a MIDI note each and a key on the computer keyboard each (LEARN: press a pad's LEARN, then
// the note or key), as momentary switches; QUANTISE lands a press on the grid. Each pad is also a host parameter (Pad N
// Trigger: map any key, pad or button to it in the host, or draw it as automation; the window's pads write it too), and
// MIDI NOTES off leaves the incoming notes alone so the host's mapping is the only way in.
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <string>
#include <vector>

namespace pump { namespace chop {

constexpr int kPads = 8;
enum PadField : int { kSlice = 0, kPitch, kReverse, kFx, kFxAmount, kLevel, kNote, kKey, kPerPad };
constexpr int pp(int pad, int field) { return pad * kPerPad + field; }
enum Fx : int { kFxNone = 0, kFxLowpass, kFxHighpass, kFxCrush, kFxRatchet, kFxTape, kFxGate, kFxDrive, kNumFx };
static const char* const kFxNames[] = { "None", "Low-pass", "High-pass", "Crush", "Ratchet", "Tape stop", "Gate", "Drive" };
static const char* const kFxKeys[] = { "-", "LP", "HP", "CRUSH", "RATCHET", "TAPE", "GATE", "DRIVE" };
enum Length : int { kHalfBar = 0, kOneBar, kTwoBars, kFourBars, kNumLengths };
static const char* const kLengthNames[] = { "1/2 bar", "1 bar", "2 bars", "4 bars" };
enum Quant : int { kQOff = 0, kQ32, kQ16, kQ8, kQ4, kNumQuant };
static const char* const kQuantNames[] = { "Off", "1/32", "1/16", "1/8", "1/4" };
static const char* const kSliceCountNames[] = { "8", "16" };

enum KitParam : int { kSlices = kPads * kPerPad, kLengthP, kQuantise, kHold, kLoopTwo, kBpm, kMix, kOutGain, kBypass,
                     kTrigger0, kMidiNotes = kTrigger0 + kPads, kNumParams };   // appended: host-mappable pads

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    static std::vector<std::string> keep;
    keep.reserve(kPads * kPerPad * 2 + kPads * 3 + 8);
    auto s = [&](std::string x) { keep.push_back(std::move(x)); return keep.back().c_str(); };
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean;
    for (int p = 0; p < kPads; ++p)
    {
        const std::string sy = "pad" + std::to_string(p + 1), nm = "Pad " + std::to_string(p + 1);
        v.push_back({ s(sy + "_slice"), s(nm + " Slice"), "Slice", 1, 16, (float)(p + 1), PUnit::None, PScale::Linear, I, nullptr, 1 });
        v.push_back({ s(sy + "_pitch"), s(nm + " Pitch"), "Pitch", -24, 24, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
        v.push_back({ s(sy + "_reverse"), s(nm + " Reverse"), "Reverse", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
        v.push_back({ s(sy + "_fx"), s(nm + " Effect"), "FX", 0, kNumFx - 1, kFxNone, PUnit::None, PScale::Linear, I, kFxNames, 1 });
        v.push_back({ s(sy + "_fx_amount"), s(nm + " Effect Amount"), "Amount", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_level"), s(nm + " Level"), "Level", -24, 6, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_note"), s(nm + " MIDI Note"), "Note", 0, 127, (float)(48 + p), PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
        v.push_back({ s(sy + "_key"), s(nm + " Key"), "Key", 0, 127, (float)('1' + p), PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    }
    v.push_back({ "slices", "Slices", "Slices", 0, 1, 0, PUnit::None, PScale::Linear, I, kSliceCountNames, 1 });
    v.push_back({ "length", "Capture Length", "Length", 0, kNumLengths - 1, kOneBar, PUnit::None, PScale::Linear, I, kLengthNames, 1 });
    v.push_back({ "quantise", "Quantise", "Quantise", 0, kNumQuant - 1, kQ16, PUnit::None, PScale::Linear, I, kQuantNames, 1 });
    v.push_back({ "hold", "Hold the Bar", "Hold", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "loop_two", "Two Pads Loop", "Loop", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "bpm", "Tempo (no host)", "BPM", 40, 240, 120, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    // momentary: held while 1 (never part of presets or undo)
    for (int p = 0; p < kPads; ++p)
        v.push_back({ s("pad" + std::to_string(p + 1) + "_trigger"), s("Pad " + std::to_string(p + 1) + " Trigger"), s("Pad " + std::to_string(p + 1)), 0, 1, 0, PUnit::None, PScale::Linear, B | kPNoUndo, nullptr, 1 });
    v.push_back({ "midi_notes", "Pads Answer MIDI Notes", "Notes", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    return v;
}

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    static std::vector<std::string> keep;
    keep.reserve(512);
    static const char* const field[kPerPad] = { "slice", "pitch", "reverse", "fx", "fx_amount", "level", "note", "key" };
    struct C { int pad, f; float v; };
    auto K = [&](const char* sec, const char* n, std::vector<C> cs, std::vector<PresetValue> extra = {}) {
        std::vector<PresetValue> p = std::move(extra);
        for (const C& c : cs) { keep.push_back("pad" + std::to_string(c.pad + 1) + "_" + field[c.f]); p.push_back({ keep.back().c_str(), c.v }); }
        v.push_back({ n, std::move(p), sec });
    };
    v.push_back({ "Default", {} });
    K("CHOPS", "Straight chops", {});
    K("CHOPS", "Sixteen slices, every other", { { 0, kSlice, 1 }, { 1, kSlice, 3 }, { 2, kSlice, 5 }, { 3, kSlice, 7 }, { 4, kSlice, 9 }, { 5, kSlice, 11 }, { 6, kSlice, 13 }, { 7, kSlice, 15 } }, { { "slices", 1 } });
    K("CHOPS", "Reversed back half", { { 4, kReverse, 1 }, { 5, kReverse, 1 }, { 6, kReverse, 1 }, { 7, kReverse, 1 } });
    K("CHOPS", "Two-bar chops", {}, { { "length", kTwoBars }, { "quantise", kQ8 } });
    K("PLAY", "Pitched pads", { { 0, kPitch, 0 }, { 1, kPitch, 2 }, { 2, kPitch, 4 }, { 3, kPitch, 5 }, { 4, kPitch, 7 }, { 5, kPitch, 9 }, { 6, kPitch, 11 }, { 7, kPitch, 12 } }, { { "loop_two", 0 } });
    K("PLAY", "Ratchets", { { 0, kFx, kFxRatchet }, { 1, kFx, kFxRatchet }, { 2, kFx, kFxRatchet }, { 3, kFx, kFxRatchet }, { 0, kFxAmount, 20 }, { 1, kFxAmount, 40 }, { 2, kFxAmount, 60 }, { 3, kFxAmount, 80 } });
    K("PLAY", "Effect per pad", { { 1, kFx, kFxLowpass }, { 2, kFx, kFxHighpass }, { 3, kFx, kFxCrush }, { 4, kFx, kFxRatchet }, { 5, kFx, kFxTape }, { 6, kFx, kFxGate }, { 7, kFx, kFxDrive } });
    K("PLAY", "Tape stops and reverses", { { 0, kFx, kFxTape }, { 1, kReverse, 1 }, { 2, kFx, kFxTape }, { 3, kReverse, 1 }, { 4, kFx, kFxTape }, { 5, kReverse, 1 }, { 6, kFx, kFxTape }, { 7, kReverse, 1 } });
    K("PLAY", "Low and slow", { { 0, kPitch, -12 }, { 1, kPitch, -12 }, { 2, kPitch, -7 }, { 3, kPitch, -7 }, { 4, kPitch, -5 }, { 5, kPitch, -5 }, { 6, kFx, kFxLowpass }, { 7, kFx, kFxLowpass } });
    K("PLAY", "Crushed and gated", { { 0, kFx, kFxCrush }, { 1, kFx, kFxGate }, { 2, kFx, kFxCrush }, { 3, kFx, kFxGate }, { 4, kFx, kFxCrush }, { 5, kFx, kFxGate }, { 6, kFx, kFxCrush }, { 7, kFx, kFxGate }, { 0, kFxAmount, 70 }, { 2, kFxAmount, 85 } });
    K("HOLD", "Hold a bar, chop it", {}, { { "hold", 1 } });
    return v;
}

}} // namespace pump::chop
