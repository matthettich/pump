// pump BREAK: parameters, the built-in breaks' scores and the factory presets (shared by the DSP, the UI and the
// tests).
//
// A break (one of the built-in ones, or a .wav of your own) is cut into sixteen slices. A sixteen-step sequencer plays
// them: per step the SLICE (or none: the slice before plays on), its PITCH, a BEND (the pitch moves by that many
// semitones across the step: an envelope on the pitch), its LEVEL, a DECAY (how fast the step fades: an envelope on
// the volume; 100 % holds until the next step) and REVERSE. FIT plays the break at the song's tempo (faster or slower,
// and higher or lower with it, as a sampler does). Eight momentary effects, held from the window, the computer keys 1-8,
// MIDI notes or host parameters: STUTTER, REPEAT, REVERSE, TAPE STOP, FILTER, HIGH-PASS, CRUSH, ECHO, each with an
// amount.
#pragma once

#include "PumpParams.hpp"
#include "dsp/Util.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace brk {

constexpr int kSlices = 16, kSteps = 16, kFx = 8;

// ---- the built-in breaks ------------------------------------------------------------------------------------------
// Each is a bar of a drummer in the soul and funk style, written as sixteenths and played by pump's own acoustic-style
// kit (BreakSounds.hpp) with its swing, feel, room and tape: K kick, S snare ('X' a hard hit, 'x' a normal one, 'g' a
// ghost note), H hats ('x' closed, 'o' open, '.' none), R ride, T toms ('1' high '2' mid '3' low).
struct BreakScore {
    const char* name; double bpm, swing; const char* k; const char* s; const char* h; const char* r; const char* t;
    double room, tape, bright, tight;   // room 0..1, tape 0..1 (saturation, hiss, wobble), brightness 0..1, tightness 0..1 (less timing drift)
};
static const BreakScore kBreaks[] = {
    { "Pocket",          96, 0.10, "x.x.......x..x..", "....X..g.g..X..g", "xxxxxxxxxxxxxxox", "................", "................", 0.25, 0.35, 0.55, 0.70 },
    { "Ghost notes",     98, 0.12, "x..x..x...x..x..", "g...X.gg.g.gX.gg", "x.x.x.x.x.x.x.o.", "................", "................", 0.20, 0.30, 0.60, 0.65 },
    { "Hot butter",     100, 0.08, "x.x...x..x...x..", "....X..g.g.gX..g", "xxxxxxxoxxxxxoxx", "................", "................", 0.30, 0.45, 0.55, 0.60 },
    { "Street funk",     92, 0.06, "x......xx.x.....", "....X.......X...", "x.x.x.x.x.x.x.x.", "................", "................", 0.45, 0.55, 0.45, 0.70 },
    { "Gospel run",     104, 0.15, "x.x.......xx....", "....X..x.x..X..x", "x.x.x.x.x.x.x.x.", "................", "................", 0.35, 0.40, 0.65, 0.55 },
    { "Soul shuffle",    88, 0.33, "x.....x.x.....x.", "....X.......X..g", "x.xxx.xxx.xxx.xx", "................", "................", 0.40, 0.50, 0.45, 0.60 },
    { "Heavy one",       90, 0.05, "x..x.xx...x.....", "....X.......X...", "x.x.x.x.x.x.o.x.", "................", "................", 0.30, 0.60, 0.40, 0.75 },
    { "Slow grind",      84, 0.20, "x.........x.....", "........X.......", "x.x.x.x.x.x.x.x.", "................", "................", 0.50, 0.55, 0.40, 0.60 },
    { "Double time",    112, 0.04, "x.x...x.x.x...x.", "....X..g....X.g.", "xxxxxxxxxxxxxxxx", "................", "................", 0.20, 0.30, 0.70, 0.80 },
    { "Ride strut",     102, 0.18, "x......xx.....x.", "....X..g....X..g", "................", "x.xxx.xxx.xxx.xx", "................", 0.35, 0.35, 0.60, 0.60 },
    { "Open hat strut", 106, 0.10, "x..x..x.x..x....", "....X.......X...", "x.o.x.o.x.o.x.o.", "................", "................", 0.30, 0.40, 0.60, 0.65 },
    { "Tom break",      100, 0.10, "x.x.......x.....", "....X.......X.X.", "x.x.x.x.x.......", "................", "..........1122.3", 0.45, 0.45, 0.50, 0.55 },
    { "Rimshot drive",   96, 0.12, "x.x..x....x..x..", "....X..x.x..X..x", "xoxxxoxxxoxxxoxx", "................", "................", 0.25, 0.45, 0.70, 0.70 },
    { "Lazy sunday",     86, 0.28, "x..x......x.....", "....X.....g.X...", "x.x.x.x.x.x.x.x.", "................", "................", 0.55, 0.60, 0.35, 0.50 },
};
constexpr int kNumBreaks = (int)(sizeof(kBreaks) / sizeof(kBreaks[0]));
// the SOURCE choices: the built-in breaks, then the loaded file
inline const char* const* sourceNames()
{
    static const char* names[kNumBreaks + 1];
    for (int i = 0; i < kNumBreaks; ++i) names[i] = kBreaks[i].name;
    names[kNumBreaks] = "File";
    return names;
}
constexpr int kSourceFile = kNumBreaks;

// ---- the effects ---------------------------------------------------------------------------------------------------
enum FxKind : int { kStutter = 0, kRepeat, kReverse, kTapeStop, kFilter, kHighpass, kCrush, kEcho };
static const char* const kFxNames[kFx] = { "Stutter", "Repeat", "Reverse", "Tape stop", "Filter", "High-pass", "Crush", "Echo" };
static const char* const kFxKeys[kFx] = { "STUTTER", "REPEAT", "REVERSE", "TAPE", "FILTER", "HI-PASS", "CRUSH", "ECHO" };
// the effects answer MIDI notes on the white keys up from Renoise's C-2 (24); the slices the white keys up from C-4 (48)
inline int fxNote(int k) { return 24 + 12 * (k / 7) + kWhiteKeySteps[k % 7]; }
inline int sliceNote(int s) { return whiteKeyNote(s); }

enum SeqMode : int { kSeqOff = 0, kSeqSong, kSeqFree, kNumSeqModes };
static const char* const kSeqModeNames[] = { "Off", "Song", "Free" };
static const char* const kRateNames[] = { "1/32", "1/16T", "1/16", "1/8T", "1/8" };
constexpr int kNumRates = 5;
inline double stepsPerQuarter(int rate) { static const double s[kNumRates] = { 8.0, 6.0, 4.0, 3.0, 2.0 }; return s[clampv(rate, 0, kNumRates - 1)]; }
static const char* const kBarsNames[] = { "1 bar", "2 bars", "4 bars" };

// Parameter indices: the kit's, the effects' amounts, the effects' holds (momentary), then six per step. Symbols never
// change once released; append only.
enum StepField : int { kSlice = 0, kPitch, kBend, kLevel, kDecay, kReverseStep, kPerStep };
enum Param : int {
    kSource = 0, kFit, kFileBars, kTranspose, kTone, kDrive, kVolume,
    kSeqMode, kSeqLength, kSeqRate, kSwing,
    kFxAmount1,
    kFxHold1 = kFxAmount1 + kFx,
    kStep1 = kFxHold1 + kFx,
    kNumParams = kStep1 + kSteps * kPerStep
};
inline int stepParam(int step, int field) { return kStep1 + clampv(step, 0, kSteps - 1) * kPerStep + field; }

struct Step { int slice = 0; double pitch = 0, bend = 0, level = 1.0, decay = 1.0; bool reverse = false; };
struct Pattern { Step s[kSteps]; };

// A chop as text: one token per step. A slice number 1..16, or '.' (no new slice: the one before plays on); then
// optional modifiers: 'r' reverse, '+N' / '-N' pitch, '^N' / 'vN' a bend up / down over the step, '@N' level %,
// '~N' decay %. "1 1~30 3 . 5r 6^12 7v7@70 ."
inline Pattern parseChop(const char* text)
{
    Pattern p;
    int n = 0;
    const char* c = text;
    auto num = [&](double& out) { char* e = nullptr; const double v = std::strtod(c, &e); if (e != c) { out = v; c = e; } };
    while (*c && n < kSteps)
    {
        while (*c == ' ') ++c;
        if (!*c) break;
        Step st;
        if (*c == '.') { st.slice = 0; ++c; }
        else { double v = 0; num(v); st.slice = clampv((int)v, 0, kSlices); }
        while (*c && *c != ' ')
        {
            const char m = *c++;
            double v = 0;
            switch (m)
            {
            case 'r': st.reverse = true; break;
            case '+': num(v); st.pitch = v; break;
            case '-': num(v); st.pitch = -v; break;
            case '^': num(v); st.bend = v; break;
            case 'v': num(v); st.bend = -v; break;
            case '@': num(v); st.level = v * 0.01; break;
            case '~': num(v); st.decay = v * 0.01; break;
            default: break;
            }
        }
        p.s[n++] = st;
    }
    return p;
}
static const char* const kStraight = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16";

inline const std::vector<ParamSpec>& params()
{
    static std::deque<std::string> text;
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean;
    auto s = [&](const std::string& x) { text.push_back(x); return text.back().c_str(); };
    v.push_back({ "source", "Break", "Break", 0, kNumBreaks, 0, PUnit::None, PScale::Linear, I, sourceNames(), 1 });
    v.push_back({ "fit", "Fit to Tempo", "Fit", 0, 1, 1, PUnit::None, PScale::Linear, B, nullptr, 1 });
    v.push_back({ "file_bars", "File Length", "Bars", 0, 2, 0, PUnit::None, PScale::Linear, I, kBarsNames, 1 });
    v.push_back({ "transpose", "Transpose", "Transp", -24, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tone", "Tone", "Tone", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "seq_mode", "Sequencer", "Seq", 0, kNumSeqModes - 1, kSeqSong, PUnit::None, PScale::Linear, I, kSeqModeNames, 1 });
    v.push_back({ "seq_length", "Pattern Length", "Length", 1, kSteps, kSteps, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "seq_rate", "Step Rate", "Rate", 0, kNumRates - 1, 2, PUnit::None, PScale::Linear, I, kRateNames, 1 });
    v.push_back({ "swing", "Swing", "Swing", 0, 75, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    static const float fxDef[kFx] = { 50, 50, 100, 50, 70, 60, 60, 50 };
    for (int k = 0; k < kFx; ++k)
        v.push_back({ s("fx" + std::to_string(k + 1) + "_amount"), s(std::string(kFxNames[k]) + " Amount"), "Amount", 0, 100, fxDef[k], PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    for (int k = 0; k < kFx; ++k)   // momentary: held while 1 (never part of presets or undo)
        v.push_back({ s("fx" + std::to_string(k + 1) + "_hold"), s(std::string(kFxNames[k]) + " Hold"), s(kFxKeys[k]), 0, 1, 0, PUnit::None, PScale::Linear, B | kPNoUndo, nullptr, 1 });
    const Pattern d = parseChop(kStraight);
    for (int k = 0; k < kSteps; ++k)
    {
        const std::string sy = "s" + std::string(k < 9 ? "0" : "") + std::to_string(k + 1), nm = "Step " + std::to_string(k + 1);
        v.push_back({ s(sy + "_slice"), s(nm + " Slice"), "Slice", 0, kSlices, (float)d.s[k].slice, PUnit::None, PScale::Linear, I, nullptr, 1 });
        v.push_back({ s(sy + "_pitch"), s(nm + " Pitch"), "Pitch", -24, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_bend"), s(nm + " Bend"), "Bend", -24, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_level"), s(nm + " Level"), "Level", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_decay"), s(nm + " Decay"), "Decay", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_rev"), s(nm + " Reverse"), "Rev", 0, 1, 0, PUnit::None, PScale::Linear, B, nullptr, 1 });
    }
    return v;
    }();
    return v;
}

// a chop's steps as preset values (every step)
inline std::vector<PresetValue> withChop(const char* text, std::vector<PresetValue> rest)
{
    const Pattern p = parseChop(text);
    const auto& ps = params();
    std::vector<PresetValue> v = std::move(rest);
    for (int k = 0; k < kSteps; ++k)
    {
        const Step& st = p.s[k];
        v.push_back({ ps[(size_t)stepParam(k, kSlice)].symbol, (float)st.slice });
        v.push_back({ ps[(size_t)stepParam(k, kPitch)].symbol, (float)st.pitch });
        v.push_back({ ps[(size_t)stepParam(k, kBend)].symbol, (float)st.bend });
        v.push_back({ ps[(size_t)stepParam(k, kLevel)].symbol, (float)(st.level * 100.0) });
        v.push_back({ ps[(size_t)stepParam(k, kDecay)].symbol, (float)(st.decay * 100.0) });
        v.push_back({ ps[(size_t)stepParam(k, kReverseStep)].symbol, st.reverse ? 1.0f : 0.0f });
    }
    return v;
}

// The chop library (the window's CHOPS menu: writes the steps only, the break and the sound stay).
struct NamedChop { const char* name; const char* text; };
static const NamedChop kChops[] = {
    { "Straight", kStraight },
    { "Kick and snare swap", "1 2 3 4 13 6 7 8 9 10 11 12 5 14 15 16" },
    { "Stutter the one", "1~40 1~40 1 4 5 6 7 8 1~40 1~40 11 12 13 14 15 16" },
    { "Half-time", "1 . 2 . 3 . 4 . 5 . 6 . 7 . 8 ." },
    { "Double up", "1 2 1 2 5 6 5 6 9 10 9 10 13 14 13 14" },
    { "Reverse tails", "1 2 3 4 5 6 7 8r 9 10 11 12 13 14 15r 16r" },
    { "Pitch drops", "1 2 3 4 5v5 6 7 8 9 10 11 12 13v7 14v12 15v12 16v12" },
    { "Rising roll", "13~35 13~35+1 13~35+2 13~35+3 13~35+4 13~35+5 13~35+6 13~35+7 13~35+8 13~35+9 13~35+10 13~35+11 13~35+12 13~35+13 13^12 16" },
    { "Choppy gate", "1~25 2~25 3~25 4~25 5~25 6~25 7~25 8~25 9~25 10~25 11~25 12~25 13~25 14~25 15~25 16~25" },
    { "Lazy drift", "1 2 3 4 5 6@70 7 8 9 10 3 4 13 14@60 15 16" },
    { "Jungle cut", "1 2 3 5 5 6 13 8 9 13 11 12 13 14 15r 13" },
    { "Low and slow", "1-5 . 3-5 . 5-5 . 7-5 . 9-5 . 11-5 . 13-5 . 15-5 ." },
    { "Tape-stop end", "1 2 3 4 5 6 7 8 9 10 11 12 13v12 14v24 . ." },
};
constexpr int kNumChops = (int)(sizeof(kChops) / sizeof(kChops[0]));
inline const char* chopText(const char* name) { for (const auto& c : kChops) if (std::strcmp(c.name, name) == 0) return c.text; return kStraight; }

// Factory presets: a break each (played straight, or with a chop that suits it), and the chops on the first break.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    for (int b = 0; b < kNumBreaks; ++b) F("BREAKS", kBreaks[b].name, withChop(kStraight, { { "source", (float)b } }));
    F("CHOPPED", "Pocket, kick and snare swap", withChop(chopText("Kick and snare swap"), { { "source", 0 } }));
    F("CHOPPED", "Ghost notes, half-time", withChop(chopText("Half-time"), { { "source", 1 }, { "tone", 75 } }));
    F("CHOPPED", "Hot butter, stutter", withChop(chopText("Stutter the one"), { { "source", 2 } }));
    F("CHOPPED", "Street funk, low and slow", withChop(chopText("Low and slow"), { { "source", 3 }, { "drive", 30 } }));
    F("CHOPPED", "Gospel run, jungle cut", withChop(chopText("Jungle cut"), { { "source", 4 }, { "seq_rate", 2 } }));
    F("CHOPPED", "Soul shuffle, lazy drift", withChop(chopText("Lazy drift"), { { "source", 5 }, { "swing", 12 } }));
    F("CHOPPED", "Heavy one, pitch drops", withChop(chopText("Pitch drops"), { { "source", 6 } }));
    F("CHOPPED", "Double time, choppy gate", withChop(chopText("Choppy gate"), { { "source", 8 } }));
    F("CHOPPED", "Tom break, rising roll", withChop(chopText("Rising roll"), { { "source", 11 } }));
    F("CHOPPED", "Lazy sunday, reverse tails", withChop(chopText("Reverse tails"), { { "source", 13 }, { "tone", 65 }, { "drive", 25 } }));
    return v;
    }();
    return v;
}

template <class Get> inline Pattern patternFrom(Get&& get)
{
    Pattern p;
    for (int k = 0; k < kSteps; ++k)
    {
        Step& s = p.s[k];
        s.slice = clampv((int)std::lround(get(stepParam(k, kSlice))), 0, kSlices);
        s.pitch = get(stepParam(k, kPitch)); s.bend = get(stepParam(k, kBend));
        s.level = get(stepParam(k, kLevel)) * 0.01; s.decay = get(stepParam(k, kDecay)) * 0.01;
        s.reverse = get(stepParam(k, kReverseStep)) >= 0.5f;
    }
    return p;
}

}} // namespace pump::brk
