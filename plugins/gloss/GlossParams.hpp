// pump GLOSS: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace gloss {

// ---- the machines ---------------------------------------------------------------------------------------------------
// GLOSS plays one of seven synthesis machines (the voices of a classic monophonic machine-based synth, the kind the
// hyperpop producers built their plastic, rubbery, glassy sounds on), each with four controls of its own (A..D), then
// the same pitch envelope, filter, amp, drive, crush and LFO.
//   Rubber   two-operator FM: RATIO of the modulator (snaps to whole ratios near them), INDEX, FEEDBACK on the
//            modulator, BOUNCE (the index falls with the pitch envelope): latex basses, boings, squeaks
//   Chip     a sound chip's voice: digital triangle, saw, pulse, noise and their bitwise mixes (WAVE), PULSE WIDTH,
//            SYNC (a slave oscillator up to eight times the note, hard-synced) and RING (ring modulation by a
//            triangle a fifth up), a twelve-bit DAC
//   Voice    a formant voice: a glottal pulse through four formant filters, VOWEL (a e i o u), SIZE (child to giant:
//            the formants shift), BREATH, MOVE (the vowel swept by the pitch envelope): pitched vocal chops
//   Super    seven saws: DETUNE, MIX (centre or the side voices), SHAPE (saw to square), OCTAVE (an octave up on top)
//   Digi     sixteen digital single-cycle waves (WAVE morphs through them), BITS (the wave's own resolution), FOLD,
//            WARP (phase distortion)
//   Pluck    a plucked string (a delay line tuned to the note): DAMP, BRIGHT (the pluck), STRETCH (stiffness: from
//            string to metal), BODY (a second string detuned beside it)
//   Bubble   water drops: a sine that chirps up as it dies, CHIRP (how far), TIME (how fast), WET (resonance and
//            noise), FIZZ (more drops at random while the key is held)
enum Machine : int { kRubber = 0, kChip, kVoice, kSuper, kDigi, kPluck, kBubble, kNumMachines };
static const char* const kMachineNames[] = { "Rubber", "Chip", "Voice", "Super", "Digi", "Pluck", "Bubble" };
static const char* const kMachineKeys[] = { "RUBBER", "CHIP", "VOICE", "SUPER", "DIGI", "PLUCK", "BUBBLE" };
// the four machine controls' names, per machine
static const char* const kMachineControls[kNumMachines][4] = {
    { "RATIO", "INDEX", "FEEDBACK", "BOUNCE" },
    { "WAVE", "PULSE W", "SYNC", "RING" },
    { "VOWEL", "SIZE", "BREATH", "MOVE" },
    { "DETUNE", "MIX", "SHAPE", "OCTAVE" },
    { "WAVE", "BITS", "FOLD", "WARP" },
    { "DAMP", "BRIGHT", "STRETCH", "BODY" },
    { "CHIRP", "TIME", "WET", "FIZZ" },
};
static const char* const kChipWaves[] = { "TRI", "SAW", "PULSE", "NOISE", "TRI&SAW", "TRI&PLS", "SAW&PLS", "ALL" };

enum FilterType : int { kLp = 0, kBp, kHp, kFilterOff, kNumFilters };
static const char* const kFilterNames[] = { "Low-pass", "Band-pass", "High-pass", "Off" };
enum DriveType : int { kSoft = 0, kHard, kFold, kNumDrives };
static const char* const kDriveNames[] = { "Soft", "Hard", "Fold" };
enum VoiceMode : int { kPoly = 0, kMono, kLegato, kNumModes };
static const char* const kModeNames[] = { "Poly", "Mono", "Legato" };
enum LfoShape : int { kLfoSine = 0, kLfoTri, kLfoSquare, kLfoRamp, kLfoSh, kNumLfo };
static const char* const kLfoNames[] = { "Sine", "Triangle", "Square", "Ramp", "S&H" };
enum LfoDest : int { kToPitch = 0, kToCutoff, kToA, kToB, kToAmp, kNumDests };
static const char* const kDestNames[] = { "Pitch", "Cutoff", "Control A", "Control B", "Level" };

// Parameter indices. Symbols never change once released; append only.
enum Param : int {
    kMachine = 0, kA, kB, kC, kD,
    kTune, kPitchEnv, kPitchTime, kGlide, kMode,
    kFilterType, kCutoff, kRes, kFilterEnv, kFilterDecay, kKeyTrack,
    kAttack, kDecay, kSustain, kRelease, kVelAmp,
    kDrive, kDriveType, kCrushBits, kCrushRate, kWidth,
    kLfoShape, kLfoRate, kLfoDepth, kLfoDest,
    kBendRange, kVolume,
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, S = kPInteger;
    v.push_back({ "machine", "Machine", "Machine", 0, kNumMachines - 1, kRubber, PUnit::None, PScale::Linear, I, kMachineNames, 1 });
    v.push_back({ "ctl_a", "Control A", "A", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "ctl_b", "Control B", "B", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "ctl_c", "Control C", "C", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "ctl_d", "Control D", "D", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tune", "Tune", "Tune", -24, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pitch_env", "Pitch Envelope", "P.Env", -48, 48, 12, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pitch_time", "Pitch Envelope Time", "P.Time", 1, 2000, 60, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 1000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "mode", "Voice Mode", "Mode", 0, kNumModes - 1, kPoly, PUnit::None, PScale::Linear, I, kModeNames, 1 });
    v.push_back({ "filter_type", "Filter Type", "Filter", 0, kNumFilters - 1, kLp, PUnit::None, PScale::Linear, I, kFilterNames, 1 });
    v.push_back({ "cutoff", "Cutoff", "Cutoff", 30, 20000, 6000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "res", "Resonance", "Reso", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_env", "Filter Envelope", "F.Env", -100, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_decay", "Filter Decay", "F.Decay", 5, 3000, 250, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "key_track", "Key Tracking", "Key", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "Attack", 0.5f, 3000, 1, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "Decay", 5, 5000, 400, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sustain", "Sustain", "Sustain", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 2, 5000, 120, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "vel_amp", "Velocity to Level", "Vel", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive_type", "Drive Type", "Type", 0, kNumDrives - 1, kHard, PUnit::None, PScale::Linear, I, kDriveNames, 1 });
    v.push_back({ "crush_bits", "Crush Bits", "Bits", 2, 16, 16, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "crush_rate", "Crush Rate", "Rate", 1000, 48000, 48000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "width", "Width", "Width", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_shape", "LFO Shape", "Shape", 0, kNumLfo - 1, kLfoSine, PUnit::None, PScale::Linear, I, kLfoNames, 1 });
    v.push_back({ "lfo_rate", "LFO Rate", "Rate", 0.05f, 40, 5, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo_depth", "LFO Depth", "Depth", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_dest", "LFO Destination", "Dest", 0, kNumDests - 1, kToPitch, PUnit::None, PScale::Linear, I, kDestNames, 1 });
    v.push_back({ "bend_range", "Pitch Bend Range", "Bend", 0, 24, 2, PUnit::None, PScale::Linear, S, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets, by machine.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    // rubber
    F("RUBBER", "Latex bass", { { "mode", kMono }, { "ctl_a", 0 }, { "ctl_b", 55 }, { "ctl_c", 25 }, { "ctl_d", 80 }, { "pitch_env", 19 }, { "pitch_time", 45 },
        { "cutoff", 2500 }, { "res", 35 }, { "filter_env", 50 }, { "filter_decay", 180 }, { "decay", 600 }, { "sustain", 60 }, { "drive", 45 }, { "tune", -12 } });
    F("RUBBER", "Boing", { { "ctl_a", 25 }, { "ctl_b", 35 }, { "ctl_d", 100 }, { "pitch_env", 36 }, { "pitch_time", 140 }, { "cutoff", 9000 }, { "decay", 450 }, { "sustain", 0 }, { "drive", 20 } });
    F("RUBBER", "Squeaky toy", { { "ctl_a", 50 }, { "ctl_b", 65 }, { "ctl_c", 40 }, { "ctl_d", 70 }, { "pitch_env", -10 }, { "pitch_time", 90 }, { "tune", 12 }, { "filter_type", kBp }, { "cutoff", 3200 }, { "res", 60 },
        { "decay", 220 }, { "sustain", 0 }, { "drive", 35 } });
    F("RUBBER", "Metal hammer", { { "ctl_a", 86 }, { "ctl_b", 90 }, { "ctl_c", 55 }, { "ctl_d", 90 }, { "pitch_env", 24 }, { "pitch_time", 12 }, { "decay", 700 }, { "sustain", 0 }, { "release", 600 },
        { "drive", 70 }, { "drive_type", kFold }, { "cutoff", 14000 } });
    F("RUBBER", "Plastic stab", { { "ctl_a", 12 }, { "ctl_b", 50 }, { "ctl_c", 70 }, { "ctl_d", 40 }, { "pitch_env", 7 }, { "pitch_time", 30 }, { "decay", 300 }, { "sustain", 15 }, { "drive", 55 }, { "width", 60 } });
    // chip
    F("CHIP", "Chip lead", { { "machine", kChip }, { "ctl_a", 30 }, { "ctl_b", 25 }, { "ctl_c", 0 }, { "ctl_d", 0 }, { "pitch_env", 0 }, { "glide", 40 }, { "mode", kLegato },
        { "lfo_depth", 8 }, { "lfo_rate", 6 }, { "sustain", 80 }, { "drive", 0 } });
    F("CHIP", "Sync sweep", { { "machine", kChip }, { "ctl_a", 15 }, { "ctl_c", 70 }, { "lfo_dest", kToB }, { "lfo_shape", kLfoTri }, { "lfo_depth", 35 }, { "lfo_rate", 0.4f }, { "sustain", 80 } });
    F("CHIP", "Bitwise bass", { { "machine", kChip }, { "ctl_a", 70 }, { "ctl_b", 40 }, { "mode", kMono }, { "tune", -12 }, { "pitch_env", 12 }, { "pitch_time", 25 }, { "cutoff", 1800 }, { "res", 50 },
        { "filter_env", 60 }, { "filter_decay", 160 }, { "drive", 40 } });
    F("CHIP", "Noise snap", { { "machine", kChip }, { "ctl_a", 43 }, { "pitch_env", 30 }, { "pitch_time", 8 }, { "decay", 90 }, { "sustain", 0 }, { "release", 60 }, { "filter_type", kHp }, { "cutoff", 900 } });
    // voice
    F("VOICE", "Chipmunk", { { "machine", kVoice }, { "ctl_a", 25 }, { "ctl_b", 85 }, { "ctl_c", 10 }, { "ctl_d", 40 }, { "tune", 12 }, { "pitch_env", 5 }, { "pitch_time", 80 }, { "glide", 30 },
        { "mode", kLegato }, { "sustain", 80 }, { "drive", 10 } });
    F("VOICE", "Robot choir", { { "machine", kVoice }, { "ctl_a", 70 }, { "ctl_b", 40 }, { "ctl_c", 20 }, { "ctl_d", 0 }, { "pitch_env", 0 }, { "attack", 60 }, { "sustain", 90 }, { "release", 600 },
        { "width", 80 }, { "crush_bits", 8 } });
    F("VOICE", "Vowel bass", { { "machine", kVoice }, { "ctl_a", 50 }, { "ctl_b", 20 }, { "ctl_d", 80 }, { "mode", kMono }, { "tune", -12 }, { "pitch_env", 12 }, { "pitch_time", 200 }, { "drive", 50 } });
    // super
    F("SUPER", "Sugar lead", { { "machine", kSuper }, { "ctl_a", 35 }, { "ctl_b", 60 }, { "ctl_c", 0 }, { "ctl_d", 30 }, { "pitch_env", 0 }, { "sustain", 85 }, { "width", 90 }, { "glide", 25 }, { "drive", 25 } });
    F("SUPER", "Hyper chord", { { "machine", kSuper }, { "ctl_a", 55 }, { "ctl_b", 70 }, { "ctl_c", 20 }, { "ctl_d", 50 }, { "pitch_env", -3 }, { "pitch_time", 40 }, { "attack", 2 }, { "decay", 800 },
        { "sustain", 30 }, { "width", 100 }, { "drive", 40 }, { "cutoff", 11000 } });
    F("SUPER", "Gated wall", { { "machine", kSuper }, { "ctl_a", 45 }, { "ctl_b", 80 }, { "ctl_c", 50 }, { "lfo_shape", kLfoSquare }, { "lfo_dest", kToAmp }, { "lfo_depth", 100 }, { "lfo_rate", 8 },
        { "sustain", 100 }, { "width", 100 } });
    // digi
    F("DIGI", "Glass keys", { { "machine", kDigi }, { "ctl_a", 60 }, { "ctl_b", 100 }, { "ctl_c", 10 }, { "ctl_d", 20 }, { "pitch_env", 0 }, { "decay", 900 }, { "sustain", 20 }, { "release", 500 }, { "width", 60 } });
    F("DIGI", "Crushed hook", { { "machine", kDigi }, { "ctl_a", 20 }, { "ctl_b", 30 }, { "ctl_c", 35 }, { "ctl_d", 50 }, { "crush_bits", 6 }, { "crush_rate", 12000 }, { "sustain", 70 }, { "drive", 30 } });
    F("DIGI", "Folded bass", { { "machine", kDigi }, { "ctl_a", 5 }, { "ctl_c", 70 }, { "ctl_d", 30 }, { "mode", kMono }, { "tune", -12 }, { "pitch_env", 7 }, { "pitch_time", 30 } });
    // pluck
    F("PLUCK", "Harp pluck", { { "machine", kPluck }, { "ctl_a", 30 }, { "ctl_b", 70 }, { "ctl_c", 0 }, { "ctl_d", 20 }, { "pitch_env", 0 }, { "sustain", 100 }, { "release", 800 }, { "drive", 0 } });
    F("PLUCK", "Steel tine", { { "machine", kPluck }, { "ctl_a", 15 }, { "ctl_b", 90 }, { "ctl_c", 70 }, { "ctl_d", 0 }, { "pitch_env", 12 }, { "pitch_time", 4 }, { "sustain", 100 }, { "release", 1200 } });
    F("PLUCK", "Snapped string", { { "machine", kPluck }, { "ctl_a", 50 }, { "ctl_b", 100 }, { "ctl_d", 60 }, { "drive", 60 }, { "drive_type", kHard }, { "sustain", 100 } });
    // bubble
    F("BUBBLE", "Bubble pop", { { "machine", kBubble }, { "ctl_a", 60 }, { "ctl_b", 30 }, { "ctl_c", 20 }, { "ctl_d", 0 }, { "pitch_env", 0 }, { "decay", 200 }, { "sustain", 0 }, { "drive", 10 } });
    F("BUBBLE", "Fizzy drops", { { "machine", kBubble }, { "ctl_a", 70 }, { "ctl_b", 15 }, { "ctl_c", 40 }, { "ctl_d", 70 }, { "pitch_env", 0 }, { "sustain", 100 }, { "release", 400 }, { "width", 100 } });
    F("BUBBLE", "Water drip bass", { { "machine", kBubble }, { "ctl_a", 40 }, { "ctl_b", 50 }, { "ctl_c", 10 }, { "mode", kMono }, { "tune", -24 }, { "drive", 60 }, { "drive_type", kSoft }, { "sustain", 30 } });
    return v;
    }();
    return v;
}

}} // namespace pump::gloss
