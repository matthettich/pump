// pump X0X: parameters, kits and patterns (shared by the DSP, the UI and the tests).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Util.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace x0x {

// ---- the instruments ------------------------------------------------------------------------------------------------
// Eleven, played in order on the white keys up from Renoise's C-4 (MIDI 48): C-4 bass drum, D-4 snare, E-4 clap,
// F-4 closed hat, G-4 open hat, A-4 low tom, B-4 mid tom, C-5 high tom, D-5 rim, E-5 cowbell, F-5 cymbal. The
// General MIDI drum notes below C-4 play them too.
constexpr int kInstr = 11;
enum Instr : int { kBD = 0, kSD, kCP, kCH, kOH, kLT, kMT, kHT, kRS, kCB, kCY };
struct InstrInfo { const char* sym; const char* name; const char* label; const char* shortLabel; };
static const InstrInfo kInstrInfo[kInstr] = {
    { "bd", "Bass Drum", "BASS DRUM", "BD" }, { "sd", "Snare Drum", "SNARE", "SD" }, { "cp", "Clap", "CLAP", "CP" },
    { "ch", "Closed Hat", "CLOSED HAT", "CH" }, { "oh", "Open Hat", "OPEN HAT", "OH" }, { "lt", "Low Tom", "LOW TOM", "LT" },
    { "mt", "Mid Tom", "MID TOM", "MT" }, { "ht", "High Tom", "HIGH TOM", "HT" }, { "rs", "Rim Shot", "RIM SHOT", "RS" },
    { "cb", "Cowbell", "COWBELL", "CB" }, { "cy", "Cymbal", "CYMBAL", "CY" },
};
inline int noteForInstr(int i) { return whiteKeyNote(clampv(i, 0, kInstr - 1)); }

// ---- the machines ---------------------------------------------------------------------------------------------------
// Every instrument can be any of five classic analog (and early digital) rhythm machine circuits, so a kit can mix
// them. Where a machine had no such instrument, its nearest sound in the same circuit style stands in.
//   Eight   bridged-T resonators rung by a trigger pulse (a long, deep, boomy bass drum; tuned snare heads; tom
//           congas); six square oscillators through two band-passes for the hats, cymbal and cowbell; a clap of
//           three noise bursts and a tail
//   Nine    a triangle oscillator swept down fast and shaped toward a sine (the punchy bass drum), with an attack
//           click; snare and toms swept, noise through its own low-pass; the hats and cymbal as crisp six-bit samples
//   Six     a small, tight version of Eight: a short, clicky bass drum, a noisy snare, bright metallic hats
//   Ten     a cheap, lo-fi analog box: a short thud, a papery snare, noise hats, a noisy clap
//   Seven   the first sample-based machines: everything at eight bits and a 25 kHz rate, crunchy
enum Machine : int { kEight = 0, kNine, kSix, kTen, kSeven, kNumMachines };
static const char* const kMachineNames[] = { "Eight", "Nine", "Six", "Ten", "Seven" };
static const char* const kMachineKeys[] = { "EIGHT", "NINE", "SIX", "TEN", "SEVEN" };

// what TONE and SNAP do, per instrument (shown on the controls)
static const char* const kToneNames[kInstr] = { "TONE", "TONE", "TONE", "TONE", "TONE", "NOISE", "NOISE", "NOISE", "TONE", "TONE", "TONE" };
static const char* const kSnapNames[kInstr] = { "ATTACK", "SNAPPY", "TAIL", "NOISE", "NOISE", "SWEEP", "SWEEP", "SWEEP", "CLICK", "ATTACK", "NOISE" };

// ---- the sequencer --------------------------------------------------------------------------------------------------
constexpr int kSteps = 16;
enum SeqMode : int { kSeqOff = 0, kSeqSong, kSeqFree, kNumSeqModes };
static const char* const kSeqModeNames[] = { "Off", "Song", "Free" };
static const char* const kRateNames[] = { "1/32", "1/16T", "1/16", "1/8T", "1/8" };
constexpr int kNumRates = 5;
inline double stepsPerQuarter(int rate) { static const double s[kNumRates] = { 8.0, 6.0, 4.0, 3.0, 2.0 }; return s[clampv(rate, 0, kNumRates - 1)]; }
constexpr int kRows = kInstr + 1;   // the eleven instruments and the accent row (last)
constexpr int kAccentRow = kInstr;

// Parameter indices: seven per instrument, then the kit's, then a step mask per row (bit k = step k + 1). Symbols
// never change once released; append only.
enum InstrParam : int { kMachine = 0, kTune, kDecay, kTone, kSnap, kLevel, kPan, kPerInstr };
inline int ip(int instr, int what) { return instr * kPerInstr + what; }
enum KitParam : int {
    kAccent = kInstr * kPerInstr, kDrive, kVolume, kVelocity, kAccentVel,
    kSeqMode, kSeqLength, kSeqRate, kSwing,
    kRow1,
    kNumParams = kRow1 + kRows
};
inline int rowParam(int row) { return kRow1 + clampv(row, 0, kRows - 1); }

// A pattern as text: one line of sixteen characters per row, 'x' a hit ('X' a hit with an accent: it sets the accent
// row's step), anything else a rest. Rows are given in instrument order, the accent row last; missing rows are empty.
struct Pattern { uint32_t rows[kRows] = {}; };
inline uint32_t maskOf(const char* s)
{
    uint32_t m = 0;
    for (int k = 0; k < kSteps && s && s[k]; ++k) if (s[k] == 'x' || s[k] == 'X') m |= 1u << k;
    return m;
}
inline Pattern makePattern(std::vector<std::pair<int, const char*>> rows, const char* accents = nullptr)
{
    Pattern p;
    for (const auto& r : rows) if (r.first >= 0 && r.first < kRows)
    {
        p.rows[r.first] |= maskOf(r.second);
        for (int k = 0; k < kSteps && r.second[k]; ++k) if (r.second[k] == 'X') p.rows[kAccentRow] |= 1u << k;
    }
    if (accents) p.rows[kAccentRow] |= maskOf(accents);
    return p;
}

// ---- the pattern library (the window's PATTERN menu: writes the steps only, the sounds stay) -------------------------
struct NamedPattern { const char* name; Pattern p; };
inline const std::vector<NamedPattern>& patternLibrary()
{
    static const std::vector<NamedPattern> v = {
        { "Four on the floor", makePattern({ { kBD, "x...x...x...x..." }, { kCP, "....x.......x..." }, { kCH, "..x...x...x...x." }, { kOH, "......x.......x." } }, "x...x...x...x...") },
        { "Acid house", makePattern({ { kBD, "x...x...x...x..." }, { kSD, "....x.......x..." }, { kCH, "x.xxx.xxx.xxx.xx" }, { kOH, "..x...x...x...x." }, { kCB, ".....x.....x..x." } }, "x.......x......x") },
        { "Jack", makePattern({ { kBD, "x...x...x...x..." }, { kCP, "....x..x....x..." }, { kCH, "xxxxxxxxxxxxxxxx" }, { kRS, "...x..x...x..x.." } }, "..x...x...x...x.") },
        { "Techno drive", makePattern({ { kBD, "x...x...x...x..." }, { kCH, "x.x.x.x.x.x.x.x." }, { kOH, "..x...x...x...x." }, { kCP, "....x.......x..." }, { kRS, "......x.......x." } }, "..x...x...x...x.") },
        { "Detroit", makePattern({ { kBD, "x..x..x...x..x.." }, { kSD, "....x.......x..." }, { kCH, "xxxxxxxxxxxxxxxx" }, { kOH, "......x.......x." }, { kCB, "x.....x...x....." }, { kHT, "...........x...x" } }, "x...x...x...x...") },
        { "Electro", makePattern({ { kBD, "x.....x...x....." }, { kSD, "....x.......x..." }, { kCH, "x.x.x.x.x.x.x.x." }, { kCP, "....x.......x..." }, { kCB, "..x.......x..x.." } }, "....x.......x...") },
        { "Breakbeat", makePattern({ { kBD, "x.........x..x.." }, { kSD, "....x..x.x..x..x" }, { kCH, "x.x.x.x.x.x.x.x." }, { kOH, "..............x." } }, "....x.......x...") },
        { "Miami bass", makePattern({ { kBD, "x.....x.....x..." }, { kSD, "....x.......x..." }, { kCH, "x.xxx.xxx.xxx.xx" }, { kCP, "....x.......x..." }, { kCB, "..x...x...x...x." } }, "x...........x...") },
        { "Shuffle", makePattern({ { kBD, "x...x...x...x..." }, { kCH, ".xx..xx..xx..xx." }, { kOH, "...x...x...x...x" }, { kCP, "....x.......x..." } }, "...x...x...x...x") },
        { "Tom groove", makePattern({ { kBD, "x...x...x...x..." }, { kLT, "..x.......x....." }, { kMT, ".....x.......x.." }, { kHT, "...x...x....x..." }, { kCH, "x.x.x.x.x.x.x.x." } }, "...x.......x....") },
        { "Half-time", makePattern({ { kBD, "x.......x.x....." }, { kSD, "........x......." }, { kCH, "x.x.x.x.x.x.x.x." }, { kCY, "x..............." } }, "........x.......") },
        { "Empty", Pattern() },
    };
    return v;
}

// ---- the parameter table ------------------------------------------------------------------------------------------
struct InstrDefaults { int machine; float tune, decay, tone, snap, level, pan; };
static const InstrDefaults kDefaults[kInstr] = {
    { kEight, 0, 55, 45, 30,   0,   0 },   // BD
    { kEight, 0, 45, 50, 60,  -3,   0 },   // SD
    { kEight, 0, 45, 50, 50,  -4,   8 },   // CP
    { kEight, 0, 30, 55, 20,  -9, -15 },   // CH
    { kEight, 0, 45, 55, 20, -10, -15 },   // OH
    { kEight, 0, 50, 15, 30,  -5, -25 },   // LT
    { kEight, 0, 50, 15, 30,  -5,   0 },   // MT
    { kEight, 0, 50, 15, 30,  -5,  25 },   // HT
    { kEight, 0, 40, 50, 50,  -8,  15 },   // RS
    { kEight, 0, 45, 50, 50,  -9,  20 },   // CB
    { kEight, 0, 60, 50, 30, -10,  30 },   // CY
};
inline const Pattern& defaultPattern() { static const Pattern p = patternLibrary()[0].p; return p; }

inline const std::vector<ParamSpec>& params()
{
    static std::deque<std::string> text;
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, S = kPInteger;
    auto s = [&](const std::string& x) { text.push_back(x); return text.back().c_str(); };
    for (int i = 0; i < kInstr; ++i)
    {
        const InstrInfo& in = kInstrInfo[i];
        const InstrDefaults& d = kDefaults[i];
        const std::string sy = in.sym, nm = in.name;
        v.push_back({ s(sy + "_machine"), s(nm + " Machine"), "Machine", 0, kNumMachines - 1, (float)d.machine, PUnit::None, PScale::Linear, I, kMachineNames, 1 });
        v.push_back({ s(sy + "_tune"), s(nm + " Tune"), "Tune", -12, 12, d.tune, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_decay"), s(nm + " Decay"), "Decay", 0, 100, d.decay, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_tone"), s(nm + " Tone"), "Tone", 0, 100, d.tone, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_snap"), s(nm + " " + kSnapNames[i]), "Snap", 0, 100, d.snap, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_level"), s(nm + " Level"), "Level", -48, 6, d.level, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_pan"), s(nm + " Pan"), "Pan", -100, 100, d.pan, PUnit::None, PScale::Linear, A, nullptr, 1 });
    }
    v.push_back({ "accent", "Accent", "Accent", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Kit Drive", "Drive", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "velocity", "Velocity Sensitivity", "Velocity", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "accent_vel", "Accent From Velocity", "Acc Vel", 1, 127, 110, PUnit::None, PScale::Linear, S, nullptr, 1 });
    v.push_back({ "seq_mode", "Sequencer", "Seq", 0, kNumSeqModes - 1, kSeqSong, PUnit::None, PScale::Linear, I, kSeqModeNames, 1 });
    v.push_back({ "seq_length", "Pattern Length", "Length", 1, kSteps, kSteps, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "seq_rate", "Step Rate", "Rate", 0, kNumRates - 1, 2, PUnit::None, PScale::Linear, I, kRateNames, 1 });
    v.push_back({ "swing", "Swing", "Swing", 0, 75, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    const Pattern& dp = defaultPattern();
    for (int r = 0; r < kRows; ++r)
    {
        const std::string sy = r == kAccentRow ? std::string("ac") : kInstrInfo[r].sym, nm = r == kAccentRow ? std::string("Accent") : kInstrInfo[r].name;
        v.push_back({ s(sy + "_steps"), s(nm + " Steps"), "Steps", 0, 65535, (float)dp.rows[r], PUnit::None, PScale::Linear, S, nullptr, 1 });
    }
    return v;
    }();
    return v;
}

// a pattern's rows as preset values
inline std::vector<PresetValue> withPattern(const Pattern& p, std::vector<PresetValue> rest)
{
    const auto& ps = params();
    std::vector<PresetValue> v = std::move(rest);
    for (int r = 0; r < kRows; ++r) v.push_back({ ps[(size_t)rowParam(r)].symbol, (float)p.rows[r] });
    return v;
}
// every instrument on one machine
inline std::vector<PresetValue> allOn(int machine, std::vector<PresetValue> rest)
{
    for (int i = 0; i < kInstr; ++i) rest.push_back({ params()[(size_t)ip(i, kMachine)].symbol, (float)machine });
    return rest;
}

// Factory kits: each a set of sounds and a pattern that suits it. (The window's PATTERN menu changes only the steps.)
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    const auto& lib = patternLibrary();
    auto pat = [&](const char* name) -> const Pattern& { for (const auto& np : lib) if (std::strcmp(np.name, name) == 0) return np.p; return lib[0].p; };
    v.push_back({ "Default", {} });
    // one machine each
    F("MACHINES", "Eight: boom", withPattern(pat("Electro"), allOn(kEight, { { "bd_decay", 85 }, { "bd_snap", 15 }, { "bd_tune", -2 }, { "cb_level", -7 } })));
    F("MACHINES", "Nine: punch", withPattern(pat("Techno drive"), allOn(kNine, { { "bd_decay", 50 }, { "bd_tone", 55 }, { "bd_snap", 55 }, { "sd_snap", 65 }, { "ch_level", -8 }, { "oh_decay", 40 } })));
    F("MACHINES", "Six: tight", withPattern(pat("Acid house"), allOn(kSix, { { "bd_decay", 45 }, { "ch_level", -7 }, { "oh_level", -9 } })));
    F("MACHINES", "Ten: lo-fi", withPattern(pat("Shuffle"), allOn(kTen, { { "bd_decay", 60 }, { "cp_snap", 70 }, { "swing", 20 } })));
    F("MACHINES", "Seven: crunch", withPattern(pat("Miami bass"), allOn(kSeven, { { "bd_decay", 70 }, { "cb_level", -8 } })));
    // the classic mixes
    F("KITS", "Acid house", withPattern(pat("Acid house"), { { "bd_machine", kNine }, { "sd_machine", kNine }, { "ch_machine", kNine }, { "oh_machine", kNine },
        { "cp_machine", kEight }, { "cb_machine", kEight }, { "cy_machine", kNine }, { "bd_decay", 55 }, { "bd_snap", 45 } }));
    F("KITS", "Warehouse", withPattern(pat("Jack"), { { "bd_machine", kNine }, { "bd_decay", 75 }, { "bd_tone", 35 }, { "cp_machine", kNine }, { "cp_snap", 70 },
        { "ch_machine", kNine }, { "oh_machine", kNine }, { "rs_machine", kNine }, { "drive", 25 } }));
    F("KITS", "Detroit", withPattern(pat("Detroit"), { { "bd_machine", kNine }, { "sd_machine", kNine }, { "ch_machine", kEight }, { "oh_machine", kEight },
        { "lt_machine", kEight }, { "mt_machine", kEight }, { "ht_machine", kEight }, { "cb_machine", kEight }, { "swing", 12 } }));
    F("KITS", "Electro boom", withPattern(pat("Electro"), { { "bd_decay", 92 }, { "bd_tune", -3 }, { "bd_snap", 10 }, { "sd_snap", 75 }, { "ch_decay", 20 } }));
    F("KITS", "Miami", withPattern(pat("Miami bass"), { { "bd_decay", 100 }, { "bd_tune", -5 }, { "bd_tone", 25 }, { "cb_level", -6 }, { "cp_level", -2 } }));
    F("KITS", "Minimal", withPattern(pat("Four on the floor"), { { "bd_machine", kSix }, { "ch_machine", kSix }, { "oh_machine", kSix }, { "cp_machine", kSix },
        { "rs_machine", kEight }, { "bd_decay", 35 }, { "ch_decay", 15 }, { "oh_decay", 25 } }));
    F("KITS", "Bedroom box", withPattern(pat("Breakbeat"), { { "bd_machine", kTen }, { "sd_machine", kTen }, { "ch_machine", kTen }, { "oh_machine", kTen },
        { "cp_machine", kTen }, { "drive", 30 }, { "swing", 8 } }));
    F("KITS", "Crunch house", withPattern(pat("Four on the floor"), { { "bd_machine", kNine }, { "sd_machine", kSeven }, { "cp_machine", kSeven }, { "ch_machine", kSeven },
        { "oh_machine", kSeven }, { "cb_machine", kSeven }, { "rs_machine", kSeven } }));
    F("KITS", "Tom jam", withPattern(pat("Tom groove"), { { "lt_machine", kNine }, { "mt_machine", kNine }, { "ht_machine", kNine }, { "lt_snap", 60 }, { "mt_snap", 60 }, { "ht_snap", 60 },
        { "bd_machine", kNine } }));
    F("KITS", "Driven", withPattern(pat("Techno drive"), { { "bd_machine", kNine }, { "bd_decay", 65 }, { "bd_snap", 70 }, { "ch_machine", kNine }, { "oh_machine", kNine },
        { "cp_machine", kNine }, { "drive", 65 }, { "volume", -3 } }));
    return v;
    }();
    return v;
}

// the pattern held by the parameters
template <class Get> inline Pattern patternFrom(Get&& get)
{
    Pattern p;
    for (int r = 0; r < kRows; ++r) p.rows[r] = (uint32_t)clampv((long)std::lround(get(rowParam(r))), 0L, 65535L);
    return p;
}

}} // namespace pump::x0x
