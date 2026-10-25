// pump TABLE: parameters and factory presets (shared by the DSP and the UI). A user wavetable for either
// oscillator is a file, saved as a state ("table_a", "table_b"); TABLE A/B = USER plays it.
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pump { namespace table {

// Parameter indices. Symbols never change once released; append only.
enum Param : int {
    // oscillator A, oscillator B (same layout, kOscB - kOscA apart)
    kTableA = 0, kPosA, kVoicesA, kDetuneA, kSpreadA, kOctA, kSemiA, kFineA, kLevelA, kPanA,
    kTableB, kPosB, kVoicesB, kDetuneB, kSpreadB, kOctB, kSemiB, kFineB, kLevelB, kPanB,
    kSub, kNoise,
    // filter
    kFilterType, kCutoff, kRes, kDrive, kFilterEnv, kKeyTrack,
    // envelopes
    kAmpA, kAmpD, kAmpS, kAmpR, kVelAmp,
    kModA, kModD, kModS, kModR,
    // LFOs (1: per voice, restarts with each note; 2: shared, free or synced)
    kLfo1Shape, kLfo1Rate, kLfo2Shape, kLfo2Rate, kLfo2Sync, kLfo2Div,
    // the mod matrix: 4 slots of source, destination, amount
    kSrc1, kDst1, kAmt1, kSrc2, kDst2, kAmt2, kSrc3, kDst3, kAmt3, kSrc4, kDst4, kAmt4,
    // voice
    kVoiceMode, kPolyphony, kGlide, kBendRange, kVolume,
    kNumParams
};
constexpr int kOscStride = kTableB - kTableA;

static const char* const kTableNames[] = { "Analog", "Harmonic", "Formant", "Sync", "PWM", "FM", "Fold", "Digital", "Odd / even", "Spectral", "User" };
constexpr int kNumTables = 11, kUserTable = 10;
static const char* const kVoiceCountNames[] = { "1", "2", "3", "4", "5", "6", "7" };
enum FilterType : int { kLp24 = 0, kLp12, kBp, kHp, kNumFilterTypes };
static const char* const kFilterNames[] = { "LP 24", "LP 12", "BP", "HP" };
enum LfoShape : int { kSine = 0, kTri, kSawUp, kSawDown, kSquare, kSampleHold, kSmoothRandom, kNumShapes };
static const char* const kShapeNames[] = { "Sine", "Triangle", "Saw up", "Saw down", "Square", "S&H", "Drift" };
enum ModSource : int { kSrcOff = 0, kSrcLfo1, kSrcLfo2, kSrcModEnv, kSrcVelocity, kSrcWheel, kSrcAftertouch, kSrcKey, kSrcRandom, kNumSources };
static const char* const kSourceNames[] = { "Off", "LFO 1", "LFO 2", "Mod env", "Velocity", "Wheel", "Aftertouch", "Key", "Random" };
enum ModDest : int { kDstPosA = 0, kDstPosB, kDstPitch, kDstCutoff, kDstRes, kDstLevelA, kDstLevelB, kDstPan, kDstDetune, kDstLfo1Rate, kNumDests };
static const char* const kDestNames[] = { "Pos A", "Pos B", "Pitch", "Cutoff", "Res", "Level A", "Level B", "Pan", "Detune", "LFO1 rate" };
static const char* const kVoiceModeNames[] = { "Poly", "Mono", "Legato" };

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    for (int o = 0; o < 2; ++o)
    {
        const bool b = o == 1;
        auto S = [&](const char* sa, const char* sb) { return b ? sb : sa; };
        v.push_back({ S("table_a", "table_b"), S("A Table", "B Table"), "Table", 0, kNumTables - 1, b ? 2.0f : 0.0f, PUnit::None, PScale::Linear, I, kTableNames, 1 });
        v.push_back({ S("pos_a", "pos_b"), S("A Position", "B Position"), "Position", 0, 100, b ? 30.0f : 40.0f, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ S("voices_a", "voices_b"), S("A Unison", "B Unison"), "Unison", 1, 7, 1, PUnit::None, PScale::Linear, I, nullptr, 1 });
        v.push_back({ S("detune_a", "detune_b"), S("A Detune", "B Detune"), "Detune", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ S("spread_a", "spread_b"), S("A Spread", "B Spread"), "Spread", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ S("oct_a", "oct_b"), S("A Octave", "B Octave"), "Octave", -3, 3, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
        v.push_back({ S("semi_a", "semi_b"), S("A Semitone", "B Semitone"), "Semi", -12, 12, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
        v.push_back({ S("fine_a", "fine_b"), S("A Fine", "B Fine"), "Fine", -100, 100, b ? 7.0f : 0.0f, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ S("level_a", "level_b"), S("A Level", "B Level"), "Level", 0, 100, b ? 0.0f : 80.0f, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ S("pan_a", "pan_b"), S("A Pan", "B Pan"), "Pan", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    }
    v.push_back({ "sub", "Sub Level", "Sub", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "noise", "Noise Level", "Noise", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_type", "Filter Type", "Type", 0, kNumFilterTypes - 1, kLp24, PUnit::None, PScale::Linear, I, kFilterNames, 1 });
    v.push_back({ "cutoff", "Cutoff", "Cutoff", 20, 20000, 6000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "res", "Resonance", "Res", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Filter Drive", "Drive", 0, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter_env", "Filter Envelope", "Env", -100, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "key_track", "Key Tracking", "Key", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "amp_a", "Amp Attack", "A", 0.5f, 10000, 3, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "amp_d", "Amp Decay", "D", 1, 10000, 400, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "amp_s", "Amp Sustain", "S", 0, 100, 80, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "amp_r", "Amp Release", "R", 1, 15000, 300, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "vel_amp", "Velocity to Level", "Vel", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mod_a", "Mod Attack", "A", 0.5f, 10000, 5, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mod_d", "Mod Decay", "D", 1, 10000, 600, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mod_s", "Mod Sustain", "S", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mod_r", "Mod Release", "R", 1, 15000, 400, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo1_shape", "LFO 1 Shape", "Shape", 0, kNumShapes - 1, kSine, PUnit::None, PScale::Linear, I, kShapeNames, 1 });
    v.push_back({ "lfo1_rate", "LFO 1 Rate", "Rate", 0.02f, 40, 2, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo2_shape", "LFO 2 Shape", "Shape", 0, kNumShapes - 1, kTri, PUnit::None, PScale::Linear, I, kShapeNames, 1 });
    v.push_back({ "lfo2_rate", "LFO 2 Rate", "Rate", 0.02f, 40, 0.3f, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo2_sync", "LFO 2 Sync", "Sync", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "lfo2_div", "LFO 2 Division", "Division", 0, kNumNotes - 1, kNote1, PUnit::None, PScale::Linear, I, kNoteNames, 1 });
    static const int defSrc[4] = { kSrcLfo1, kSrcModEnv, kSrcWheel, kSrcVelocity };
    static const int defDst[4] = { kDstPosA, kDstPosA, kDstCutoff, kDstCutoff };
    static const char* const sym[4][3] = { { "src1", "dst1", "amt1" }, { "src2", "dst2", "amt2" }, { "src3", "dst3", "amt3" }, { "src4", "dst4", "amt4" } };
    static const char* const nm[4][3] = { { "Mod 1 Source", "Mod 1 Destination", "Mod 1 Amount" }, { "Mod 2 Source", "Mod 2 Destination", "Mod 2 Amount" },
                                         { "Mod 3 Source", "Mod 3 Destination", "Mod 3 Amount" }, { "Mod 4 Source", "Mod 4 Destination", "Mod 4 Amount" } };
    for (int k = 0; k < 4; ++k)
    {
        v.push_back({ sym[k][0], nm[k][0], "Source", 0, kNumSources - 1, (float)defSrc[k], PUnit::None, PScale::Linear, I, kSourceNames, 1 });
        v.push_back({ sym[k][1], nm[k][1], "Dest", 0, kNumDests - 1, (float)defDst[k], PUnit::None, PScale::Linear, I, kDestNames, 1 });
        v.push_back({ sym[k][2], nm[k][2], "Amount", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    }
    v.push_back({ "voice_mode", "Voice Mode", "Mode", 0, 2, 0, PUnit::None, PScale::Linear, I, kVoiceModeNames, 1 });
    v.push_back({ "polyphony", "Polyphony", "Voices", 1, 16, 8, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "bend_range", "Pitch Bend Range", "Bend", 0, 24, 2, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 6, -6, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
}

// Factory presets. Tables: 0 analog 1 harmonic 2 formant 3 sync 4 pwm 5 fm 6 fold 7 digital 8 odd/even 9 spectral.
// Sources: 1 lfo1 2 lfo2 3 mod env 4 velocity 5 wheel 6 aftertouch 7 key 8 random. Dests: 0 posA 1 posB 2 pitch
// 3 cutoff 4 res 5 levelA 6 levelB 7 pan 8 detune 9 lfo1 rate.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    // BASS
    F("BASS", "Sub saw", { { "table_a", 0 }, { "pos_a", 50 }, { "sub", 60 }, { "cutoff", 900 }, { "res", 20 }, { "filter_env", 35 }, { "mod_d", 250 }, { "mod_s", 0 }, { "amp_r", 80 }, { "voice_mode", 2 }, { "glide", 40 } });
    F("BASS", "Reese", { { "table_a", 0 }, { "pos_a", 50 }, { "voices_a", 4 }, { "detune_a", 35 }, { "spread_a", 30 }, { "cutoff", 1400 }, { "res", 10 }, { "filter_env", 0 }, { "sub", 40 }, { "voice_mode", 1 } });
    F("BASS", "Growl", { { "table_a", 2 }, { "pos_a", 20 }, { "src1", 1 }, { "dst1", 0 }, { "amt1", 60 }, { "lfo1_rate", 3 }, { "cutoff", 2500 }, { "drive", 8 }, { "sub", 50 }, { "voice_mode", 1 } });
    F("BASS", "FM pluck bass", { { "table_a", 5 }, { "pos_a", 10 }, { "src2", 3 }, { "dst2", 0 }, { "amt2", 70 }, { "mod_d", 180 }, { "mod_s", 0 }, { "cutoff", 3000 }, { "amp_d", 500 }, { "amp_s", 40 }, { "voice_mode", 1 } });
    F("BASS", "Wobble", { { "table_a", 0 }, { "pos_a", 55 }, { "src1", 2 }, { "dst1", 3 }, { "amt1", 70 }, { "lfo2_sync", 1 }, { "lfo2_div", kNote8 }, { "lfo2_shape", 0 }, { "cutoff", 500 }, { "res", 45 }, { "src2", 2 }, { "dst2", 0 }, { "amt2", 30 }, { "sub", 40 }, { "voice_mode", 1 } });
    // LEADS
    F("LEADS", "Super saw lead", { { "table_a", 0 }, { "pos_a", 50 }, { "voices_a", 7 }, { "detune_a", 30 }, { "spread_a", 80 }, { "cutoff", 9000 }, { "filter_env", 15 }, { "glide", 60 }, { "voice_mode", 2 }, { "src1", 5 }, { "dst1", 2 }, { "amt1", 3 } });
    F("LEADS", "Sync scream", { { "table_a", 3 }, { "pos_a", 10 }, { "src2", 3 }, { "dst2", 0 }, { "amt2", 80 }, { "mod_a", 5 }, { "mod_d", 900 }, { "mod_s", 30 }, { "cutoff", 12000 }, { "voice_mode", 2 }, { "glide", 50 } });
    F("LEADS", "Vowel lead", { { "table_a", 2 }, { "pos_a", 0 }, { "src1", 1 }, { "dst1", 0 }, { "amt1", 45 }, { "lfo1_rate", 0.7f }, { "voice_mode", 2 }, { "glide", 80 } });
    F("LEADS", "Digital whistle", { { "table_a", 7 }, { "pos_a", 70 }, { "table_b", 1 }, { "pos_b", 0 }, { "level_b", 40 }, { "oct_b", 1 }, { "cutoff", 14000 } });
    // KEYS
    F("KEYS", "Glass keys", { { "table_a", 5 }, { "pos_a", 30 }, { "table_b", 1 }, { "pos_b", 20 }, { "oct_b", 1 }, { "level_b", 35 }, { "amp_d", 1200 }, { "amp_s", 20 }, { "amp_r", 600 }, { "src2", 3 }, { "dst2", 0 }, { "amt2", -40 } });
    F("KEYS", "Organ build", { { "table_a", 1 }, { "pos_a", 40 }, { "src1", 5 }, { "dst1", 0 }, { "amt1", 60 }, { "amp_a", 5 }, { "amp_s", 100 }, { "amp_r", 60 }, { "filter_env", 0 }, { "cutoff", 16000 } });
    F("KEYS", "Bell", { { "table_a", 8 }, { "pos_a", 80 }, { "table_b", 5 }, { "pos_b", 70 }, { "oct_b", 1 }, { "semi_b", 7 }, { "level_b", 30 }, { "amp_d", 2500 }, { "amp_s", 0 }, { "amp_r", 2000 }, { "cutoff", 16000 }, { "filter_env", 0 } });
    // PADS
    F("PADS", "Moving pad", { { "table_a", 9 }, { "pos_a", 20 }, { "voices_a", 3 }, { "table_b", 2 }, { "level_b", 50 }, { "src1", 2 }, { "dst1", 0 }, { "amt1", 50 }, { "src2", 2 }, { "dst2", 1 }, { "amt2", -40 }, { "lfo2_rate", 0.08f }, { "amp_a", 900 }, { "amp_r", 2500 }, { "cutoff", 5000 } });
    F("PADS", "Choir", { { "table_a", 2 }, { "pos_a", 40 }, { "voices_a", 5 }, { "detune_a", 15 }, { "src1", 2 }, { "dst1", 0 }, { "amt1", 30 }, { "lfo2_rate", 0.15f }, { "amp_a", 700 }, { "amp_r", 1800 } });
    F("PADS", "Warm strings", { { "table_a", 0 }, { "pos_a", 48 }, { "voices_a", 5 }, { "detune_a", 25 }, { "cutoff", 3500 }, { "amp_a", 500 }, { "amp_r", 1500 }, { "src1", 1 }, { "dst1", 2 }, { "amt1", 2 }, { "lfo1_rate", 5 } });
    // PLUCKS AND FX
    F("PLUCKS", "PWM pluck", { { "table_a", 4 }, { "pos_a", 30 }, { "src2", 3 }, { "dst2", 0 }, { "amt2", 50 }, { "mod_d", 300 }, { "mod_s", 0 }, { "amp_d", 400 }, { "amp_s", 0 }, { "amp_r", 300 }, { "cutoff", 4000 }, { "filter_env", 40 } });
    F("PLUCKS", "Fold pluck", { { "table_a", 6 }, { "pos_a", 0 }, { "src2", 3 }, { "dst2", 0 }, { "amt2", 80 }, { "mod_d", 220 }, { "mod_s", 0 }, { "amp_d", 350 }, { "amp_s", 0 } });
    F("PLUCKS", "Random sparkle", { { "table_a", 9 }, { "src1", 8 }, { "dst1", 0 }, { "amt1", 100 }, { "oct_a", 1 }, { "amp_d", 250 }, { "amp_s", 0 }, { "amp_r", 400 } });
    return v;
}

}} // namespace pump::table
