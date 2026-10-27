// pump VECTOR: parameters and factory voices (shared by the DSP and the UI).
//
// A vector synth. Four sources sit at the corners of a square (A top left, B top right, C bottom left, D bottom
// right) and a point on the square (the joystick, X and Y) mixes them. The vector envelope moves the point along a
// path of four points (P1 -> P2 -> P3 -> P4, then holding at P4, or looping), and the LFO can sway it. Each source
// is a single-cycle WAVE from a bank of 32, a wave SEQUENCE (it steps through the bank: STEPS waves, STRIDE apart,
// RATE ms each, crossfading, with an optional transposition per step) or a SCANNED string (a ring of masses and
// springs, struck at the note by a hammer shaped like its wave, and read round at the note's pitch as it vibrates
// slowly: the timbre moves by itself). Then a ladder low-pass with its envelope, the amp envelope.
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace vector {

constexpr int kSources = 4;
constexpr int kNumWaves = 32;
static const char* const kWaveNames[kNumWaves] = {
    "Sine", "Triangle", "Saw", "Square", "Pulse 25", "Pulse 10", "Organ 1", "Organ 2",
    "Vowel A", "Vowel E", "Vowel I", "Vowel O", "Vowel U", "Choir", "Bell", "Glass",
    "Clav", "Reed", "Brass", "Strings", "Hollow", "Comb", "Sync 1", "Sync 2",
    "Fold 1", "Fold 2", "Digital 1", "Digital 2", "Digital 3", "Dense", "Formant sweep", "Bright saw" };
enum SourceType : int { kWave = 0, kSequence, kScanned, kNumTypes };
static const char* const kTypeNames[] = { "Wave", "Sequence", "Scanned" };
enum SrcField : int { kType = 0, kWaveP, kTune, kFine, kLevel, kSrcFields };
static const char* const kSrcName[kSources] = { "A", "B", "C", "D" };
enum VecLoop : int { kVecOnce = 0, kVecRepeat, kVecBounce, kNumVecLoops };
static const char* const kVecLoopNames[] = { "Once", "Loop", "Bounce" };
enum SeqLoop : int { kSeqLoop = 0, kSeqBounce, kSeqOnce, kNumSeqLoops };
static const char* const kSeqLoopNames[] = { "Loop", "Bounce", "Once" };
static const char* const kVoiceModeNames[] = { "Poly", "Mono", "Legato" };

constexpr int kSrcBase = 0;
constexpr int ps(int src, int field) { return kSrcBase + src * kSrcFields + field; }
enum Param : int {
    kX = kSources * kSrcFields, kY, kVecAmt, kVecTime, kVecLoopP,
    kP1x, kP1y, kP2x, kP2y, kP3x, kP3y, kP4x, kP4y,
    kSeqRate, kSeqSteps, kSeqStride, kSeqXfade, kSeqLoopP, kSeqTranspose,
    kScanTension, kScanDamp, kScanCentre, kScanHammer, kScanSpeed,
    kCutoff, kReso, kEnvCut, kKeytrack, kFA, kFD, kFS, kFR,
    kAttack, kDecay, kSustain, kRelease,
    kLfoRate, kLfoPitch, kLfoX, kLfoY, kLfoCut,
    kVelocity, kSpread, kVoices, kVoiceMode, kGlide, kOctave, kVolume,
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    static std::vector<std::string> sym, nm;
    sym.reserve(64); nm.reserve(64);
    const int defWave[kSources] = { 2, 8, 14, 26 };
    for (int s = 0; s < kSources; ++s)
    {
        auto S = [&](const char* a) { sym.push_back(std::string("src") + (char)('a' + s) + "_" + a); return sym.back().c_str(); };
        auto N = [&](const char* a) { nm.push_back(std::string("Source ") + kSrcName[s] + " " + a); return nm.back().c_str(); };
        v.push_back({ S("type"), N("Type"), "Type", 0, kNumTypes - 1, kWave, PUnit::None, PScale::Linear, I, kTypeNames, 1 });
        v.push_back({ S("wave"), N("Wave"), "Wave", 0, kNumWaves - 1, (float)defWave[s], PUnit::None, PScale::Linear, I, kWaveNames, 1 });
        v.push_back({ S("tune"), N("Tune"), "Tune", -24, 24, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
        v.push_back({ S("fine"), N("Fine"), "Fine", -50, 50, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ S("level"), N("Level"), "Level", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    }
    v.push_back({ "x", "Vector X", "X", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "y", "Vector Y", "Y", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vec_amount", "Vector Envelope", "Env", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vec_time", "Vector Step Time", "Time", 10, 8000, 800, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "vec_loop", "Vector Loop", "Loop", 0, kNumVecLoops - 1, kVecOnce, PUnit::None, PScale::Linear, I, kVecLoopNames, 1 });
    const float px[4] = { 0, 100, 100, 0 }, py[4] = { 0, 0, 100, 100 };
    static const char* const pxs[4] = { "p1_x", "p2_x", "p3_x", "p4_x" }, *pys[4] = { "p1_y", "p2_y", "p3_y", "p4_y" };
    static const char* const pxn[4] = { "Path 1 X", "Path 2 X", "Path 3 X", "Path 4 X" }, *pyn[4] = { "Path 1 Y", "Path 2 Y", "Path 3 Y", "Path 4 Y" };
    for (int k = 0; k < 4; ++k)
    {
        v.push_back({ pxs[k], pxn[k], "X", 0, 100, px[k], PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ pys[k], pyn[k], "Y", 0, 100, py[k], PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    }
    v.push_back({ "seq_rate", "Sequence Step", "Step", 10, 2000, 180, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "seq_steps", "Sequence Steps", "Steps", 1, 16, 8, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "seq_stride", "Sequence Stride", "Stride", -8, 8, 1, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "seq_xfade", "Sequence Crossfade", "Xfade", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "seq_loop", "Sequence Loop", "Loop", 0, kNumSeqLoops - 1, kSeqLoop, PUnit::None, PScale::Linear, I, kSeqLoopNames, 1 });
    v.push_back({ "seq_transpose", "Sequence Transpose", "Transp", -12, 12, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "scan_tension", "Scan Tension", "Tension", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "scan_damping", "Scan Damping", "Damping", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "scan_centre", "Scan Centring", "Centring", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "scan_hammer", "Scan Hammer", "Hammer", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "scan_speed", "Scan Speed", "Speed", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "cutoff", "Cutoff", "Cutoff", 30, 18000, 9000, PUnit::Hz, PScale::Log, A, nullptr, 0 });
    v.push_back({ "reso", "Resonance", "Reso", 0, 100, 10, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "env_cut", "Env to Cutoff", "Env", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "keytrack", "Key Track", "Keytrack", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "f_attack", "Filter Attack", "A", 0.5f, 5000, 5, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "f_decay", "Filter Decay", "D", 1, 10000, 500, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "f_sustain", "Filter Sustain", "S", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "f_release", "Filter Release", "R", 1, 10000, 400, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "A", 0.5f, 5000, 8, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "D", 1, 10000, 800, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sustain", "Sustain", "S", 0, 100, 80, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "release", "Release", "R", 1, 10000, 500, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo_rate", "LFO Rate", "Rate", 0.05f, 20, 0.5f, PUnit::Hz, PScale::Log, A, nullptr, 2 });
    v.push_back({ "lfo_pitch", "LFO to Pitch", "Pitch", 0, 100, 0, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "lfo_x", "LFO to X", "X", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_y", "LFO to Y", "Y", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_cut", "LFO to Cutoff", "Cutoff", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "velocity", "Velocity", "Velocity", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spread", "Stereo Spread", "Spread", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voices", "Voices", "Voices", 1, 8, 8, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "voice_mode", "Voice Mode", "Mode", 0, 2, 0, PUnit::None, PScale::Linear, I, kVoiceModeNames, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "octave", "Octave", "Oct", -3, 3, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
}

inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto P = [](int src, int f, float x) { return PresetValue { params()[(size_t)ps(src, f)].symbol, x }; };
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    // vector pads: a path round the four corners
    F("VECTOR", "Vector pad", { P(0, kWaveP, 19), P(1, kWaveP, 13), P(2, kWaveP, 15), P(3, kWaveP, 6), { "vec_amount", 100 }, { "vec_time", 1800 }, { "vec_loop", kVecBounce }, { "attack", 600 }, { "release", 1800 }, { "cutoff", 5000 } });
    F("VECTOR", "Vowel morph", { P(0, kWaveP, 8), P(1, kWaveP, 9), P(2, kWaveP, 11), P(3, kWaveP, 12), { "vec_amount", 100 }, { "vec_time", 600 }, { "vec_loop", kVecRepeat }, { "attack", 150 }, { "release", 900 } });
    F("VECTOR", "Glass and reed", { P(0, kWaveP, 15), P(1, kWaveP, 17), P(2, kWaveP, 14), P(3, kWaveP, 20), { "vec_amount", 100 }, { "vec_time", 1200 }, { "p2_x", 30 }, { "p3_y", 70 }, { "attack", 300 }, { "release", 1500 }, { "lfo_x", 20 }, { "lfo_rate", 0.2f } });
    F("VECTOR", "Joystick brass", { P(0, kWaveP, 18), P(1, kWaveP, 31), P(2, kWaveP, 2), P(3, kWaveP, 16), { "x", 30 }, { "y", 30 }, { "env_cut", 50 }, { "cutoff", 1500 }, { "attack", 30 }, { "voice_mode", 1 }, { "glide", 30 }, { "volume", 9 } });
    // wave sequences
    F("SEQUENCE", "Wave sequence", { P(0, kType, kSequence), P(0, kWaveP, 0), { "x", 0 }, { "y", 0 }, { "seq_steps", 12 }, { "seq_stride", 3 }, { "seq_rate", 140 }, { "seq_xfade", 40 }, { "release", 800 } });
    F("SEQUENCE", "Rhythmic waves", { P(0, kType, kSequence), P(0, kWaveP, 26), P(1, kType, kSequence), P(1, kWaveP, 2), { "x", 50 }, { "y", 0 }, { "seq_steps", 8 }, { "seq_stride", 5 }, { "seq_rate", 125 }, { "seq_xfade", 0 }, { "env_cut", 40 }, { "cutoff", 3000 }, { "sustain", 60 }, { "volume", 2 } });
    F("SEQUENCE", "Arp sequence", { P(0, kType, kSequence), P(0, kWaveP, 31), { "x", 0 }, { "y", 0 }, { "seq_steps", 4 }, { "seq_stride", 0 }, { "seq_transpose", 7 }, { "seq_rate", 120 }, { "seq_xfade", 0 }, { "voice_mode", 1 }, { "cutoff", 4000 }, { "env_cut", 30 }, { "volume", 7 } });
    F("SEQUENCE", "Slow evolution", { P(0, kType, kSequence), P(1, kType, kSequence), P(1, kWaveP, 14), { "x", 50 }, { "y", 0 }, { "seq_steps", 16 }, { "seq_stride", 2 }, { "seq_rate", 700 }, { "seq_xfade", 100 }, { "seq_loop", kSeqBounce }, { "attack", 800 }, { "release", 2000 }, { "spread", 90 }, { "volume", 2 } });
    // scanned strings
    F("SCANNED", "Scanned string", { P(0, kType, kScanned), P(0, kWaveP, 0), { "x", 0 }, { "y", 0 }, { "scan_tension", 55 }, { "scan_damping", 25 }, { "scan_speed", 40 }, { "release", 1200 }, { "volume", -6 } });
    F("SCANNED", "Scanned bell", { P(0, kType, kScanned), P(0, kWaveP, 14), { "x", 0 }, { "y", 0 }, { "scan_tension", 80 }, { "scan_damping", 10 }, { "scan_centre", 5 }, { "scan_speed", 70 }, { "sustain", 0 }, { "decay", 3000 }, { "release", 2000 } });
    F("SCANNED", "Slow scan pad", { P(0, kType, kScanned), P(1, kType, kScanned), P(1, kWaveP, 3), { "x", 50 }, { "y", 0 }, { "scan_tension", 30 }, { "scan_damping", 5 }, { "scan_speed", 15 }, { "attack", 900 }, { "release", 2500 }, { "spread", 90 }, { "volume", -3 } });
    F("SCANNED", "Plucked scan", { P(0, kType, kScanned), P(0, kWaveP, 2), { "x", 0 }, { "y", 0 }, { "scan_tension", 65 }, { "scan_damping", 60 }, { "scan_hammer", 100 }, { "scan_speed", 80 }, { "sustain", 20 }, { "decay", 600 }, { "cutoff", 6000 }, { "volume", 2 } });
    // all of it
    F("HYBRID", "Scan meets sequence", { P(0, kType, kScanned), P(1, kType, kSequence), P(1, kWaveP, 8), P(2, kWaveP, 19), P(3, kType, kSequence), P(3, kWaveP, 22), { "vec_amount", 100 }, { "vec_time", 1500 }, { "vec_loop", kVecBounce }, { "seq_stride", 2 }, { "seq_rate", 250 }, { "attack", 400 }, { "release", 2000 }, { "volume", -1 } });
    F("HYBRID", "Vector bass", { P(0, kWaveP, 2), P(1, kWaveP, 24), P(2, kWaveP, 3), P(3, kWaveP, 22), P(0, kTune, -12), P(2, kTune, -12), { "vec_amount", 100 }, { "vec_time", 200 }, { "p1_x", 0 }, { "p1_y", 100 }, { "p2_x", 100 }, { "p2_y", 0 }, { "octave", -1 }, { "cutoff", 1200 }, { "env_cut", 60 }, { "voice_mode", 2 }, { "glide", 40 }, { "volume", 3 } });
    return v;
}

}} // namespace pump::vector
