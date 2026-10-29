// pump ACID: parameters, the pattern format, the random pattern generator and the factory presets (shared by the
// DSP, the UI and the tests).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Fx.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace acid {

constexpr int kSteps = 16;
constexpr int kNoteLo = -12, kNoteHi = 24;   // a step's note, in semitones from the pattern's C (an octave down to two up)
constexpr int kBaseNote = 36;                // the pattern's C as written: C2 (MIDI 36)
constexpr int kKeyRoot = 48;                 // KEYS mode: this key (Renoise's C-4) plays the pattern as written

// ---- the voices ---------------------------------------------------------------------------------------------------
// Each model is a whole bass synth of its own (oscillators, filter, envelopes), played by the same controls:
//   Acid     one oscillator (saw, or the square a transistor shapes from it), a four-pole diode ladder whose stages load
//            each other (a soft, rubbery resonance that thins the bass), a filter envelope that only decays and an amp
//            envelope that dies slowly while the note is held; ACCENT shortens the filter decay, opens the filter
//            through a capacitor that charges up over consecutive accents (the "wow"), and lifts the level
//   Solo     saw, pulse (with PWM) and a square sub an octave down, noise; a clean OTA filter (SLOPE: four poles or
//            two) that sings at full resonance; one ADSR for the filter and the amp
//   Pocket   three oscillators (the second and third detuned either side), a two-pole resonant low-pass, an
//            attack / decay envelope with sustain on or off
//   Station  two oscillators and a sub into a hot transistor ladder (SLOPE: four poles or two), its own overdrive
//            before the filter
//   Scream   a high-pass then a two-pole low-pass whose resonance loop clips: it screams when pushed
enum Model : int { kAcid = 0, kSolo, kPocket, kStation, kScream, kNumModels };
static const char* const kModelNames[] = { "Acid", "Solo", "Pocket", "Station", "Scream" };
static const char* const kModelKeys[] = { "ACID", "SOLO", "POCKET", "STATION", "SCREAM" };

static const char* const kSlopeNames[] = { "24 dB", "12 dB" };
enum LfoShape : int { kLfoTri = 0, kLfoSquare, kLfoSaw, kLfoSh, kNumLfo };
static const char* const kLfoNames[] = { "Triangle", "Square", "Saw", "S&H" };

// ---- the sequencer ------------------------------------------------------------------------------------------------
//   Off    the sequencer is silent: MIDI plays the voice (overlapping notes slide, high velocity accents)
//   Song   the pattern plays while the host plays, locked to the song position
//   Keys   the pattern plays while a key is held, from its first step, transposed by the key (C-4 = as written)
//   Free   the pattern always plays, at the host tempo (locked to the song while it plays)
enum SeqMode : int { kSeqOff = 0, kSeqSong, kSeqKeys, kSeqFree, kNumSeqModes };
static const char* const kSeqModeNames[] = { "Off", "Song", "Keys", "Free" };
static const char* const kRateNames[] = { "1/32", "1/16T", "1/16", "1/8T", "1/8" };
constexpr int kNumRates = 5;
inline double stepsPerQuarter(int rate) { static const double s[kNumRates] = { 8.0, 6.0, 4.0, 3.0, 2.0 }; return s[clampv(rate, 0, kNumRates - 1)]; }

enum Gate : int { kRest = 0, kNote, kTie, kNumGates };
static const char* const kGateNames[] = { "Rest", "Note", "Tie" };

// ---- the random generator's scales ----------------------------------------------------------------------------------
enum Scale : int { kMinor = 0, kPhrygian, kDorian, kHarmonic, kPentatonic, kBlues, kMajor, kChromatic, kPower, kOctaves, kNumScales };
static const char* const kScaleNames[] = { "Minor", "Phrygian", "Dorian", "Harmonic minor", "Minor pentatonic", "Blues", "Major",
                                           "Chromatic", "Root & fifth", "Octaves" };
inline std::vector<int> scaleSteps(int scale)
{
    switch (scale)
    {
    case kPhrygian: return { 0, 1, 3, 5, 7, 8, 10 };
    case kDorian: return { 0, 2, 3, 5, 7, 9, 10 };
    case kHarmonic: return { 0, 2, 3, 5, 7, 8, 11 };
    case kPentatonic: return { 0, 3, 5, 7, 10 };
    case kBlues: return { 0, 3, 5, 6, 7, 10 };
    case kMajor: return { 0, 2, 4, 5, 7, 9, 11 };
    case kChromatic: return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    case kPower: return { 0, 7 };
    case kOctaves: return { 0 };
    default: return { 0, 2, 3, 5, 7, 8, 10 };
    }
}
static const char* const kRootNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// Parameter indices. Symbols never change once released; append only. The 16 steps follow the globals, four
// parameters each (note, gate, accent, slide): stepParam(step, k).
enum Param : int {
    kModel = 0, kTune, kWave, kCutoff, kRes, kEnvMod, kDecay, kAccent, kSlide, kDrive, kVolume,
    kAttack, kSustain, kRelease, kPw, kSub, kNoise, kDetune, kOsc2, kKeyTrack, kAccentVel,
    kLfoShape, kLfoRate, kLfoPitch, kLfoCutoff, kLfoPw, kBendRange, kSlope,
    kSeqMode, kSeqLength, kSeqRate, kSwing, kGateLen, kTranspose,
    kRndScale, kRndRoot, kRndDensity, kRndAccent, kRndSlide, kRndOctave,
    kStep1,
    kNumParams = kStep1 + kSteps * 4
};
enum StepField : int { kStepNote = 0, kStepGate, kStepAccent, kStepSlide };
inline int stepParam(int step, int field) { return kStep1 + clampv(step, 0, kSteps - 1) * 4 + field; }

// ---- patterns -------------------------------------------------------------------------------------------------------
struct Step { int note = 0; int gate = kNote; bool accent = false, slide = false; };
struct Pattern { Step s[kSteps]; int length = kSteps; };

// A pattern as text, one token per step (spaces between): "." rest, "-" tie (holds the note before), or a note:
// a letter C..B with an optional '#', then "'" for an octave up ("''" two) or "," for an octave down, then the flags:
// '!' accent, '~' slide into the next step. "C" is the pattern's C. Example: "C! C~ c' . D#, - G!~ A#".
inline Pattern parsePattern(const char* text)
{
    Pattern p;
    for (auto& s : p.s) { s.gate = kRest; s.note = 0; }
    int n = 0;
    const char* c = text;
    static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };
    while (*c && n < kSteps)
    {
        while (*c == ' ') ++c;
        if (!*c) break;
        Step st;
        if (*c == '.') { st.gate = kRest; ++c; }
        else if (*c == '-') { st.gate = kTie; ++c; }
        else
        {
            const char L = (char)(*c & ~0x20);
            st.note = (L >= 'A' && L <= 'G') ? semis[L - 'A'] : 0;
            ++c;
            if (*c == '#') { ++st.note; ++c; }
            while (*c == '\'' || *c == ',') { st.note += *c == '\'' ? 12 : -12; ++c; }
            st.note = clampv(st.note, kNoteLo, kNoteHi);
            st.gate = kNote;
        }
        while (*c == '!' || *c == '~') { if (*c == '!') st.accent = true; else st.slide = true; ++c; }
        while (*c && *c != ' ') ++c;
        p.s[n++] = st;
    }
    p.length = std::max(1, n);
    return p;
}

// ---- the random pattern generator -----------------------------------------------------------------------------------
// Acid-line logic, the way a hardware bass line's random mode writes patterns: the first step always plays the root;
// notes come from the scale (the root and the fifth more often, and often the same note again, as acid lines repeat);
// octave jumps up are likelier than down and likelier on accents; rests and ties come in by DENSITY (a tie only after
// a note); accents land on notes only, never more than three in a row; a slide only goes from a note into a note, and
// likelier into an octave jump. `lanes` picks what is rolled again (the rest of the pattern stays): NOTES (the pitch
// within the octave), OCTAVES, ACCENTS, SLIDES, TIME (which steps play, rest or tie). MUTATE rolls the chosen lanes
// again on a few steps only.
enum Lane : unsigned { kLaneNotes = 1u, kLaneOctaves = 2u, kLaneAccents = 4u, kLaneSlides = 8u, kLaneTime = 16u, kAllLanes = 31u };
struct RandomSettings { int scale = kMinor, root = 0; double density = 0.75, accent = 0.3, slide = 0.25, octave = 0.3; };

inline int pitchClassOf(int note) { return ((note % 12) + 12) % 12; }
inline int octaveOf(int note) { return note >= 12 ? (note >= 24 ? 2 : 1) : (note < 0 ? -1 : 0); }

inline void randomizeSteps(Pattern& p, unsigned lanes, const RandomSettings& r, fx::Rng& rng, const bool* only = nullptr)
{
    const std::vector<int> sc = scaleSteps(r.scale);
    auto pick = [&](int prev) -> int {
        const double u = rng.uni();
        if (prev >= 0 && u < 0.22) return prev;                        // the same note again
        if (u < 0.45) return 0;                                         // the root
        if (u < 0.58 && sc.size() > 1) for (int s : sc) if (s == 7) return 7;   // the fifth
        return sc[(size_t)(rng.next() % sc.size())];
    };
    const int L = clampv(p.length, 1, kSteps);
    auto want = [&](int i) { return !only || only[i]; };
    // TIME: which steps play
    if (lanes & kLaneTime)
        for (int i = 0; i < L; ++i)
        {
            if (!want(i)) continue;
            if (i == 0 && !only) { p.s[i].gate = kNote; continue; }
            const double u = rng.uni();
            const bool prevSounds = i > 0 && p.s[i - 1].gate != kRest;
            if (u < r.density) p.s[i].gate = kNote;
            else if (prevSounds && u < r.density + (1.0 - r.density) * 0.3) p.s[i].gate = kTie;
            else p.s[i].gate = kRest;
        }
    // NOTES and OCTAVES
    int prevPc = -1;
    for (int i = 0; i < L; ++i)
    {
        Step& s = p.s[i];
        const int pc = pitchClassOf(s.note - r.root), oc = octaveOf(s.note - r.root);
        int newPc = pc, newOc = oc;
        if (want(i))
        {
            if (lanes & kLaneNotes) newPc = (i == 0 && !only) ? 0 : pick(prevPc);
            if (lanes & kLaneOctaves)
            {
                const double u = rng.uni();
                const double up = r.octave * (s.accent ? 0.85 : 0.6), down = r.octave * 0.25;
                newOc = u < up ? 1 : (u < up + down ? -1 : 0);
                if (r.scale == kOctaves && u < 0.08 + r.octave * 0.3) newOc = 2;
            }
        }
        int nn = r.root + newPc + 12 * newOc;
        while (nn > kNoteHi) nn -= 12;
        while (nn < kNoteLo) nn += 12;
        s.note = nn;
        if (s.gate == kNote) prevPc = newPc;
    }
    // ACCENTS
    if (lanes & kLaneAccents)
    {
        int run = 0;
        for (int i = 0; i < L; ++i)
        {
            Step& s = p.s[i];
            if (!want(i)) { run = s.accent ? run + 1 : 0; continue; }
            s.accent = s.gate == kNote && run < 3 && rng.uni() < r.accent * (octaveOf(s.note - r.root) > 0 ? 1.4 : 1.0);
            run = s.accent ? run + 1 : 0;
        }
    }
    // SLIDES (into a note: the next step must sound)
    if (lanes & (kLaneSlides | kLaneTime | kLaneNotes | kLaneOctaves))
        for (int i = 0; i < L; ++i)
        {
            Step& s = p.s[i];
            const Step& nx = p.s[(i + 1) % L];
            if (s.gate == kRest || nx.gate != kNote) { if (lanes & (kLaneSlides | kLaneTime)) s.slide = false; continue; }
            if (!(lanes & kLaneSlides) || !want(i)) continue;
            const bool jump = std::abs(nx.note - s.note) >= 7;
            s.slide = rng.uni() < r.slide * (jump ? 1.5 : 1.0);
        }
    // no accent or slide on a rest
    for (int i = 0; i < L; ++i) if (p.s[i].gate == kRest) { p.s[i].accent = false; p.s[i].slide = false; }
}
// MUTATE: the chosen lanes again on about a quarter of the steps
inline void mutateSteps(Pattern& p, unsigned lanes, const RandomSettings& r, fx::Rng& rng)
{
    bool only[kSteps] = {};
    const int L = clampv(p.length, 1, kSteps);
    const int n = std::max(2, L / 4);
    for (int k = 0; k < n; ++k) only[rng.next() % (uint32_t)L] = true;
    randomizeSteps(p, lanes, r, rng, only);
}

// ---- the parameter table ------------------------------------------------------------------------------------------
// The default pattern: a sixteen-step line in C minor.
static const char* const kDefaultPattern = "C! C c'~ C D#! . C~ G, C! - A#,~ C c' C~ D#'! .";

inline const std::vector<ParamSpec>& params()
{
    static std::deque<std::string> text;
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean, S = kPInteger;
    auto s = [&](const std::string& x) { text.push_back(x); return text.back().c_str(); };
    v.push_back({ "model", "Model", "Model", 0, kNumModels - 1, kAcid, PUnit::None, PScale::Linear, I, kModelNames, 1 });
    v.push_back({ "tune", "Tune", "Tune", -12, 12, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "wave", "Waveform", "Wave", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "cutoff", "Cutoff", "Cutoff", 40, 12000, 420, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "res", "Resonance", "Reso", 0, 100, 72, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "env_mod", "Envelope Mod", "Env Mod", 0, 100, 55, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "Decay", 30, 3000, 380, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "accent", "Accent", "Accent", 0, 100, 65, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "slide", "Slide Time", "Slide", 10, 600, 60, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "Attack", 0.5f, 2000, 1, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sustain", "Sustain", "Sustain", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 1, 3000, 40, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "pw", "Pulse Width", "PW", 5, 50, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sub", "Sub Oscillator", "Sub", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "noise", "Noise", "Noise", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "detune", "Detune", "Detune", 0, 50, 8, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "osc2", "Second Oscillators", "Osc 2", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "key_track", "Key Tracking", "Key", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "accent_vel", "Accent From Velocity", "Acc Vel", 1, 127, 100, PUnit::None, PScale::Linear, S, nullptr, 1 });
    v.push_back({ "lfo_shape", "LFO Shape", "Shape", 0, kNumLfo - 1, kLfoTri, PUnit::None, PScale::Linear, I, kLfoNames, 1 });
    v.push_back({ "lfo_rate", "LFO Rate", "Rate", 0.05f, 30, 4, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo_pitch", "LFO to Pitch", "Pitch", 0, 100, 0, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "lfo_cutoff", "LFO to Cutoff", "Cutoff", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_pw", "LFO to Pulse Width", "PWM", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bend_range", "Pitch Bend Range", "Bend", 0, 24, 2, PUnit::None, PScale::Linear, S, nullptr, 1 });
    v.push_back({ "slope", "Filter Slope", "Slope", 0, 1, 0, PUnit::None, PScale::Linear, I, kSlopeNames, 1 });
    v.push_back({ "seq_mode", "Sequencer", "Seq", 0, kNumSeqModes - 1, kSeqSong, PUnit::None, PScale::Linear, I, kSeqModeNames, 1 });
    v.push_back({ "seq_length", "Pattern Length", "Length", 1, kSteps, kSteps, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "seq_rate", "Step Rate", "Rate", 0, kNumRates - 1, 2, PUnit::None, PScale::Linear, I, kRateNames, 1 });
    v.push_back({ "swing", "Swing", "Swing", 0, 75, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "gate_len", "Gate Length", "Gate", 10, 95, 55, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "transpose", "Transpose", "Transp", -12, 12, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "rnd_scale", "Random Scale", "Scale", 0, kNumScales - 1, kMinor, PUnit::None, PScale::Linear, S, kScaleNames, 1 });
    v.push_back({ "rnd_root", "Random Root", "Root", 0, 11, 0, PUnit::None, PScale::Linear, S, kRootNames, 1 });
    v.push_back({ "rnd_density", "Random Density", "Density", 0, 100, 75, PUnit::Percent, PScale::Linear, 0, nullptr, 1 });
    v.push_back({ "rnd_accent", "Random Accents", "Accents", 0, 100, 30, PUnit::Percent, PScale::Linear, 0, nullptr, 1 });
    v.push_back({ "rnd_slide", "Random Slides", "Slides", 0, 100, 25, PUnit::Percent, PScale::Linear, 0, nullptr, 1 });
    v.push_back({ "rnd_octave", "Random Octaves", "Octaves", 0, 100, 30, PUnit::Percent, PScale::Linear, 0, nullptr, 1 });
    const Pattern def = parsePattern(kDefaultPattern);
    for (int k = 0; k < kSteps; ++k)
    {
        char sy[16], nm[24];
        const Step& st = def.s[k];
        std::snprintf(sy, sizeof sy, "s%02d_note", k + 1); std::snprintf(nm, sizeof nm, "Step %d Note", k + 1);
        v.push_back({ s(sy), s(nm), "Note", (float)kNoteLo, (float)kNoteHi, (float)st.note, PUnit::None, PScale::Linear, I, nullptr, 1 });
        std::snprintf(sy, sizeof sy, "s%02d_gate", k + 1); std::snprintf(nm, sizeof nm, "Step %d Gate", k + 1);
        v.push_back({ s(sy), s(nm), "Gate", 0, kNumGates - 1, (float)st.gate, PUnit::None, PScale::Linear, I, kGateNames, 1 });
        std::snprintf(sy, sizeof sy, "s%02d_acc", k + 1); std::snprintf(nm, sizeof nm, "Step %d Accent", k + 1);
        v.push_back({ s(sy), s(nm), "Accent", 0, 1, st.accent ? 1.0f : 0.0f, PUnit::None, PScale::Linear, B, nullptr, 1 });
        std::snprintf(sy, sizeof sy, "s%02d_slide", k + 1); std::snprintf(nm, sizeof nm, "Step %d Slide", k + 1);
        v.push_back({ s(sy), s(nm), "Slide", 0, 1, st.slide ? 1.0f : 0.0f, PUnit::None, PScale::Linear, B, nullptr, 1 });
    }
    return v;
    }();
    return v;
}

// a pattern's steps as preset values (every step, so a preset never inherits a step from another)
inline std::vector<PresetValue> withPattern(const char* text, std::vector<PresetValue> rest)
{
    const Pattern p = parsePattern(text);
    const auto& ps = params();
    std::vector<PresetValue> v = std::move(rest);
    v.push_back({ "seq_length", (float)p.length });
    for (int k = 0; k < kSteps; ++k)
    {
        const Step& st = p.s[k];
        v.push_back({ ps[(size_t)stepParam(k, kStepNote)].symbol, (float)st.note });
        v.push_back({ ps[(size_t)stepParam(k, kStepGate)].symbol, (float)st.gate });
        v.push_back({ ps[(size_t)stepParam(k, kStepAccent)].symbol, st.accent ? 1.0f : 0.0f });
        v.push_back({ ps[(size_t)stepParam(k, kStepSlide)].symbol, st.slide ? 1.0f : 0.0f });
    }
    return v;
}

// Factory presets: SOUNDS (a voice with the default line), LINES (a voice and a pattern of its own).
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    // the voices
    F("SOUNDS", "Acid square", { { "wave", 100 }, { "cutoff", 380 }, { "res", 78 }, { "env_mod", 60 }, { "decay", 320 } });
    F("SOUNDS", "Squelch", { { "cutoff", 260 }, { "res", 92 }, { "env_mod", 85 }, { "decay", 250 }, { "accent", 90 }, { "drive", 35 } });
    F("SOUNDS", "Rubber low", { { "cutoff", 180 }, { "res", 45 }, { "env_mod", 35 }, { "decay", 900 }, { "accent", 40 } });
    F("SOUNDS", "Distorted acid", { { "cutoff", 520 }, { "res", 85 }, { "env_mod", 70 }, { "decay", 450 }, { "accent", 80 }, { "drive", 80 }, { "volume", -4 } });
    F("SOUNDS", "Solo saw bass", { { "model", kSolo }, { "cutoff", 600 }, { "res", 35 }, { "env_mod", 50 }, { "decay", 260 }, { "sustain", 30 }, { "sub", 60 }, { "release", 80 } });
    F("SOUNDS", "Solo PWM bass", { { "model", kSolo }, { "wave", 100 }, { "pw", 30 }, { "lfo_pw", 45 }, { "lfo_rate", 0.8f }, { "cutoff", 900 }, { "res", 25 }, { "env_mod", 35 }, { "sustain", 50 }, { "sub", 40 } });
    F("SOUNDS", "Solo resonant pluck", { { "model", kSolo }, { "cutoff", 300 }, { "res", 88 }, { "env_mod", 75 }, { "decay", 180 }, { "sustain", 0 }, { "key_track", 50 } });
    F("SOUNDS", "Pocket three-osc", { { "model", kPocket }, { "osc2", 80 }, { "detune", 12 }, { "cutoff", 500 }, { "res", 55 }, { "env_mod", 55 }, { "decay", 400 } });
    F("SOUNDS", "Pocket square stack", { { "model", kPocket }, { "wave", 100 }, { "osc2", 70 }, { "detune", 5 }, { "cutoff", 700 }, { "res", 40 }, { "sustain", 60 } });
    F("SOUNDS", "Station growl", { { "model", kStation }, { "osc2", 60 }, { "detune", 15 }, { "sub", 50 }, { "cutoff", 450 }, { "res", 60 }, { "env_mod", 65 }, { "drive", 40 }, { "sustain", 20 } });
    F("SOUNDS", "Station two-pole", { { "model", kStation }, { "slope", 1 }, { "wave", 30 }, { "cutoff", 900 }, { "res", 50 }, { "env_mod", 40 }, { "sustain", 40 } });
    F("SOUNDS", "Scream", { { "model", kScream }, { "cutoff", 700 }, { "res", 85 }, { "env_mod", 50 }, { "decay", 350 }, { "sustain", 20 }, { "drive", 30 } });
    F("SOUNDS", "Scream sub", { { "model", kScream }, { "wave", 100 }, { "cutoff", 350 }, { "res", 70 }, { "env_mod", 60 }, { "sub", 50 } });
    // the lines
    F("LINES", "Rolling sixteenths", withPattern("C C c' C C~ D# C c' C C G, C C~ A#, C c'", { { "cutoff", 300 }, { "res", 80 }, { "env_mod", 65 }, { "decay", 280 } }));
    F("LINES", "Hypnotic octaves", withPattern("C c'! C c'~ C c'! C c' C c'! C c'~ C c'! C~ c'", { { "cutoff", 340 }, { "res", 84 }, { "env_mod", 70 }, { "decay", 220 } }));
    F("LINES", "Slide madness", withPattern("C~ D#~ G~ c'! . C~ A#,~ C! . F~ G~ A#! c'~ G D# C!", { { "cutoff", 260 }, { "res", 90 }, { "env_mod", 75 }, { "slide", 80 } }));
    F("LINES", "Sparse squelch", withPattern("C! . . C . - D#'! . C . . G,! . C~ c' .", { { "cutoff", 220 }, { "res", 93 }, { "env_mod", 88 }, { "decay", 500 }, { "accent", 95 } }));
    F("LINES", "Phrygian stab", withPattern("C! C# C . C c'! C# . C C# C! . G,~ C C# .", { { "rnd_scale", kPhrygian }, { "wave", 100 }, { "cutoff", 400 }, { "res", 75 } }));
    F("LINES", "Acid house walk", withPattern("C . C' C D# . F F# G . G c' A# G F D#", { { "cutoff", 500 }, { "res", 70 }, { "env_mod", 55 }, { "decay", 350 }, { "swing", 20 } }));
    F("LINES", "Triplet roll", withPattern("C c'! C D# C! G, C C c'~ D#' C! -", { { "seq_rate", 1 }, { "cutoff", 300 }, { "res", 82 } }));
    F("LINES", "Half-time drone", withPattern("C,! - - - C~ D#' - . C,! - G,~ C - - . .", { { "seq_rate", 4 }, { "cutoff", 160 }, { "res", 88 }, { "env_mod", 80 }, { "decay", 1500 } }));
    F("LINES", "Solo arp", withPattern("C D# G c' C D# G c' A#, D G A# C D# G c'", { { "model", kSolo }, { "cutoff", 1200 }, { "res", 40 }, { "env_mod", 45 }, { "decay", 150 }, { "sustain", 0 }, { "sub", 40 } }));
    F("LINES", "Pocket bounce", withPattern("C! - C . C c' . C D#! - C . G, . C~ C'", { { "model", kPocket }, { "osc2", 75 }, { "detune", 10 }, { "cutoff", 450 }, { "res", 60 } }));
    F("LINES", "Screamer run", withPattern("C~ c'~ C~ c'! D#~ d#'~ G~ g'! C~ c'~ A#,~ a#! G~ g'~ D#~ d#'!", { { "model", kScream }, { "cutoff", 600 }, { "res", 88 }, { "drive", 40 } }));
    return v;
    }();
    return v;
}

// The pattern held by the parameters (values in parameter order, e.g. a plugin's or the UI's).
template <class Get> inline Pattern patternFrom(Get&& get)
{
    Pattern p;
    p.length = clampv((int)std::lround(get(kSeqLength)), 1, kSteps);
    for (int k = 0; k < kSteps; ++k)
    {
        Step& s = p.s[k];
        s.note = clampv((int)std::lround(get(stepParam(k, kStepNote))), kNoteLo, kNoteHi);
        s.gate = clampv((int)std::lround(get(stepParam(k, kStepGate))), 0, kNumGates - 1);
        s.accent = get(stepParam(k, kStepAccent)) >= 0.5f;
        s.slide = get(stepParam(k, kStepSlide)) >= 0.5f;
    }
    return p;
}

}} // namespace pump::acid
