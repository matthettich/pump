// pump TRACT: parameters and factory voices (shared by the DSP and the UI).
//
// A vocal-tract pad. Every singer is a physical model of a voice: a glottis (the vocal folds' pulse of air, tense or
// breathy, with vibrato and a little jitter, and aspiration noise) feeding a tube of 44 sections from the glottis to
// the lips (a Kelly-Lochbaum waveguide: waves passing along it and reflecting where its width changes). The vowel is
// the tube's shape: where the tongue narrows it, how much, and how round the lips are; VOWEL moves between A, E, I, O,
// U, and MOTION sways it. SIZE is the tract's length (a child's to a giant's). Each note is up to four singers, each
// a little different (detuned, sized, voweled, placed apart). Then a gentle low-pass (BRIGHT) and the amp envelope.
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace tract {

static const char* const kVoiceModeNames[] = { "Poly", "Mono", "Legato" };

enum Param : int {
    kVowel = 0, kMotion, kMotionRate, kSize, kTense, kBreath, kVibrato, kVibRate, kJitter,
    kSingers, kDetune, kSizeSpread, kVowelSpread, kSpread, kBright,
    kAttack, kDecay, kSustain, kRelease, kVelocity, kVoices, kVoiceMode, kGlide, kOctave, kVolume,
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    v.push_back({ "vowel", "Vowel", "Vowel", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "motion", "Vowel Motion", "Motion", 0, 100, 10, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "motion_rate", "Motion Rate", "Rate", 0.02f, 8, 0.2f, PUnit::Hz, PScale::Log, A, nullptr, 2 });
    v.push_back({ "size", "Tract Size", "Size", 60, 150, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tense", "Tenseness", "Tense", 0, 100, 55, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "breath", "Breath", "Breath", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vibrato", "Vibrato", "Vibrato", 0, 100, 25, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "vib_rate", "Vibrato Rate", "Rate", 2, 9, 5.2f, PUnit::Hz, PScale::Linear, A, nullptr, 2 });
    v.push_back({ "jitter", "Jitter", "Jitter", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "singers", "Singers", "Singers", 1, 4, 2, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "detune", "Detune", "Detune", 0, 50, 12, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "size_spread", "Size Spread", "Sizes", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vowel_spread", "Vowel Spread", "Vowels", 0, 100, 10, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spread", "Stereo Spread", "Spread", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bright", "Brightness", "Bright", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "A", 1, 8000, 400, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "D", 1, 10000, 800, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sustain", "Sustain", "S", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "release", "Release", "R", 1, 12000, 1200, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "velocity", "Velocity", "Velocity", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voices", "Voices", "Voices", 1, 8, 6, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "voice_mode", "Voice Mode", "Mode", 0, 2, 0, PUnit::None, PScale::Linear, I, kVoiceModeNames, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "octave", "Octave", "Oct", -3, 3, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    return v;
    }();
    return v;
}

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("CHOIR", "Ah choir", { { "vowel", 0 }, { "singers", 4 }, { "detune", 15 }, { "size_spread", 40 }, { "motion", 5 } });
    F("CHOIR", "Ooh choir", { { "vowel", 100 }, { "singers", 4 }, { "detune", 12 }, { "breath", 35 }, { "bright", 40 } });
    F("CHOIR", "Low men", { { "vowel", 75 }, { "size", 125 }, { "octave", -1 }, { "singers", 3 }, { "tense", 45 }, { "bright", 45 } });
    F("CHOIR", "Children", { { "vowel", 50 }, { "size", 70 }, { "octave", 1 }, { "singers", 4 }, { "breath", 40 }, { "tense", 40 } });
    F("PAD", "Vowel drift", { { "motion", 60 }, { "motion_rate", 0.12f }, { "singers", 3 }, { "vowel_spread", 40 }, { "attack", 900 }, { "release", 2500 } });
    F("PAD", "Breath pad", { { "breath", 80 }, { "tense", 20 }, { "vowel", 25 }, { "singers", 3 }, { "bright", 50 }, { "attack", 700 }, { "volume", 3 } });
    F("PAD", "Giant hum", { { "size", 150 }, { "vowel", 100 }, { "octave", -2 }, { "singers", 2 }, { "vibrato", 10 }, { "attack", 1500 }, { "release", 3000 }, { "volume", 3 } });
    F("PAD", "Strange throats", { { "size", 140 }, { "size_spread", 100 }, { "vowel_spread", 100 }, { "motion", 40 }, { "motion_rate", 0.5f }, { "singers", 4 }, { "detune", 30 } });
    F("LEAD", "Solo voice", { { "singers", 1 }, { "vowel", 30 }, { "voice_mode", 2 }, { "glide", 80 }, { "vibrato", 35 }, { "attack", 80 }, { "tense", 65 } });
    F("LEAD", "Talking lead", { { "singers", 1 }, { "motion", 100 }, { "motion_rate", 3.0f }, { "voice_mode", 1 }, { "tense", 75 }, { "bright", 80 }, { "attack", 20 } });
    F("LEAD", "Pressed voice", { { "singers", 2 }, { "tense", 95 }, { "breath", 5 }, { "vowel", 20 }, { "voice_mode", 2 }, { "glide", 40 }, { "attack", 30 } });
    return v;
    }();
    return v;
}

}} // namespace pump::tract
