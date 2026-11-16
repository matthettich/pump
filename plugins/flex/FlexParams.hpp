// pump FLEX: parameters (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "FlexTable.hpp"
#include <string>
#include <vector>

namespace pump { namespace flex {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kPlay = 0,                 // which slot's pattern runs (0 = off): the main control, made to be automated
    kSlot1,                    // eight slots (kSlot1 + i): 0 empty, 1-255 FLX 01-FF, 256-510 FLY 01-FF
    kMix = kSlot1 + 8, kOutGain, kBypass,
    kNumParams
};
constexpr int kNumSlots = 8, kNumSlotValues = 511;   // slot values 0-510

static const char* const kPlayNames[] = { "Off", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8" };
static const char* const kSlotSymbols[] = { "slot1", "slot2", "slot3", "slot4", "slot5", "slot6", "slot7", "slot8" };
static const char* const kSlotNames[] = { "Slot 1 Pattern", "Slot 2 Pattern", "Slot 3 Pattern", "Slot 4 Pattern", "Slot 5 Pattern", "Slot 6 Pattern", "Slot 7 Pattern", "Slot 8 Pattern" };

// a slot value from its bank and hex number: flxSlot(0x30) = FLX 30, flySlot(0x01) = FLY 01
constexpr int flxSlot(int hex) { return hex; }
constexpr int flySlot(int hex) { return 255 + hex; }
// the slots' defaults: a spread of the moves
constexpr int kDefaultSlot[kNumSlots] = { flxSlot(0x01), flxSlot(0x02), flxSlot(0x30), flySlot(0x01), flxSlot(0x06), flxSlot(0x47), flySlot(0x74), flySlot(0x50) };

// the slot values' names ("EMPTY", "FLX 1A ROLL 1/32 FROM BEAT 3", "FLY 74 ..."), 511 of them, built once
inline const std::vector<const char*>& slotChoices()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v;
        for (int s = 0; s < kNumSlotValues; ++s) v.push_back(slotLabel(s));
        return v;
    }();
    static const std::vector<const char*> ptrs = [] {
        std::vector<const char*> v;
        for (const std::string& s : names) v.push_back(s.c_str());
        return v;
    }();
    return ptrs;
}

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    // PLAY: automate this to switch patterns from the host (the engine crossfades from one to the next)
    v.push_back({ "play", "Play", "Play", 0, kNumSlots, 0, PUnit::None, PScale::Linear, A | kPInteger, kPlayNames, 1 });
    // (hosts see the slots as numbers, 0-510: DPF's list of named values holds 255 at most, so the names are shown in the window)
    for (int i = 0; i < kNumSlots; ++i)
        v.push_back({ kSlotSymbols[i], kSlotNames[i], kSlotNames[i], 0, kNumSlotValues - 1, (float)kDefaultSlot[i], PUnit::None, PScale::Linear, A | kPInteger, slotChoices().data(), 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// no factory presets: the slots are the presets (the window's own A/B and preset menu still work on all of it)
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = [] {
        std::vector<Preset> p;
        p.push_back({ "Default", {} });
        return p;
    }();
    return v;
}

}} // namespace pump::flex
