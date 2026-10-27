// pump SING: parameters and factory voices (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace sing {

// Parameter indices. Symbols never change once released; append only.
enum Param : int {
    kMode = 0, kChoir, kOctave, kSize, kRobot, kVoice, kBreath, kBright, kSoft, kClarity, kRough,
    kVib, kVibRate, kVibDelay, kGlide, kWheel, kHiss, kDiction, kAttack, kRelease,
    kSpread, kWidth, kVelocity, kDrive, kVolume,
    kEngine, kGender,   // the voicebank engine (added 2026-10-27)
    kNumParams
};
enum EngineKind : int { kEngineFormant = 0, kEngineBank, kNumEngineKinds };
static const char* const kEngineKindNames[] = { "Formant", "Voicebank" };
enum Mode : int { kModeMono = 0, kModePoly, kNumModes };
static const char* const kModeNames[] = { "Mono", "Poly" };
static const char* const kDefaultLyrics = "hello world, I am a singing machine. la la la ooh ~ yeah";

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    v.push_back({ "mode", "Mode", "Mode", 0, kNumModes - 1, kModeMono, PUnit::None, PScale::Linear, I, kModeNames, 1 });
    v.push_back({ "choir", "Choir Voices", "Choir", 1, 8, 1, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "octave", "Octave", "Oct", -2, 1, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "size", "Size", "Size", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "robot", "Robot", "Robot", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voice", "Voice", "Voice", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "breath", "Breath", "Breath", 0, 100, 12, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bright", "Bright", "Bright", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "soft", "Soft", "Soft", 0, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "clarity", "Clarity", "Clarity", 0, 100, 75, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "rough", "Rough", "Rough", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vibrato", "Vibrato", "Vibrato", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vib_rate", "Vibrato Rate", "Rate", 2, 9, 5.5f, PUnit::Hz, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "vib_delay", "Vibrato Delay", "Delay", 0, 1500, 280, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "glide", "Glide", "Glide", 0, 600, 45, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "wheel", "Wheel to Vibrato", "Wheel", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "hiss", "Consonant Noise", "Consonant", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "diction", "Diction", "Diction", 50, 200, 110, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "Attack", 0.5f, 400, 12, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 1, 3000, 160, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "spread", "Choir Spread", "Spread", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "width", "Stereo Width", "Width", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "velocity", "Velocity to Level", "Velocity", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "engine", "Voice Engine", "Engine", 0, kNumEngineKinds - 1, kEngineFormant, PUnit::None, PScale::Linear, I, kEngineKindNames, 1 });
    v.push_back({ "gender", "Voicebank Gender", "Gender", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory voices (styles). Size: + smaller (younger, higher formants), - bigger.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    // singers
    F("SINGERS", "Clear singer", { { "robot", 22 }, { "breath", 8 }, { "bright", 62 }, { "soft", 30 }, { "clarity", 80 }, { "vibrato", 30 }, { "glide", 45 }, { "diction", 110 } });
    F("SINGERS", "Soft singer", { { "robot", 12 }, { "breath", 30 }, { "vibrato", 45 }, { "vib_delay", 300 }, { "bright", 50 }, { "soft", 75 }, { "glide", 70 }, { "attack", 30 }, { "release", 260 } });
    F("SINGERS", "Breathy pop", { { "robot", 10 }, { "breath", 55 }, { "bright", 68 }, { "soft", 55 }, { "clarity", 70 }, { "vibrato", 15 }, { "glide", 35 }, { "size", 15 } });
    F("SINGERS", "Bright pop", { { "robot", 30 }, { "breath", 10 }, { "bright", 82 }, { "soft", 20 }, { "clarity", 90 }, { "vibrato", 20 }, { "glide", 30 }, { "size", 25 }, { "diction", 125 } });
    F("SINGERS", "Child", { { "size", 55 }, { "robot", 18 }, { "breath", 20 }, { "bright", 70 }, { "vibrato", 10 }, { "glide", 40 }, { "diction", 120 } });
    F("SINGERS", "Soprano", { { "size", 30 }, { "robot", 10 }, { "vibrato", 55 }, { "vib_rate", 5.8f }, { "vib_delay", 200 }, { "clarity", 85 }, { "soft", 30 }, { "glide", 60 } });
    F("SINGERS", "Tenor", { { "size", -25 }, { "robot", 15 }, { "vibrato", 40 }, { "vib_rate", 5.4f }, { "clarity", 80 }, { "bright", 58 } });
    F("SINGERS", "Baritone", { { "octave", -1 }, { "size", -45 }, { "robot", 15 }, { "vibrato", 35 }, { "bright", 50 }, { "soft", 45 } });
    F("SINGERS", "Deep bass", { { "octave", -1 }, { "size", -70 }, { "robot", 20 }, { "vibrato", 25 }, { "bright", 42 }, { "soft", 50 }, { "glide", 80 } });
    F("SINGERS", "Opera", { { "vibrato", 75 }, { "vib_rate", 5.9f }, { "vib_delay", 120 }, { "clarity", 95 }, { "soft", 15 }, { "bright", 70 }, { "robot", 8 }, { "glide", 90 } });
    F("SINGERS", "Crooner", { { "size", -20 }, { "vibrato", 35 }, { "vib_delay", 400 }, { "glide", 120 }, { "soft", 70 }, { "breath", 25 }, { "bright", 45 }, { "attack", 25 } });
    F("SINGERS", "Folk", { { "vibrato", 8 }, { "clarity", 72 }, { "robot", 14 }, { "breath", 18 }, { "glide", 25 }, { "soft", 40 } });
    F("SINGERS", "Raspy rock", { { "rough", 45 }, { "drive", 30 }, { "size", -15 }, { "bright", 66 }, { "vibrato", 15 }, { "breath", 22 } });
    F("SINGERS", "Growl", { { "rough", 90 }, { "size", -50 }, { "drive", 60 }, { "bright", 55 }, { "vibrato", 0 }, { "octave", -1 } });
    // whispers
    F("WHISPERS", "Whisper", { { "voice", 0 }, { "breath", 100 }, { "robot", 0 }, { "vibrato", 0 }, { "bright", 65 }, { "release", 200 } });
    F("WHISPERS", "Half whisper", { { "voice", 35 }, { "breath", 80 }, { "robot", 5 }, { "vibrato", 10 }, { "bright", 62 } });
    F("WHISPERS", "Ghost", { { "voice", 10 }, { "breath", 90 }, { "choir", 4 }, { "spread", 70 }, { "width", 90 }, { "release", 900 }, { "attack", 120 }, { "mode", 1 } });
    // choirs
    F("CHOIRS", "Choir", { { "mode", 1 }, { "choir", 6 }, { "spread", 50 }, { "width", 80 }, { "vibrato", 30 }, { "attack", 60 }, { "release", 500 }, { "robot", 10 }, { "soft", 50 } });
    F("CHOIRS", "Children's choir", { { "mode", 1 }, { "choir", 6 }, { "size", 45 }, { "spread", 45 }, { "width", 80 }, { "vibrato", 15 }, { "attack", 50 }, { "release", 500 } });
    F("CHOIRS", "Male choir", { { "mode", 1 }, { "choir", 6 }, { "octave", -1 }, { "size", -40 }, { "spread", 45 }, { "width", 70 }, { "vibrato", 25 }, { "attack", 60 }, { "release", 600 } });
    F("CHOIRS", "Aah pad", { { "mode", 1 }, { "choir", 8 }, { "spread", 60 }, { "width", 100 }, { "attack", 250 }, { "release", 1200 }, { "vibrato", 20 }, { "soft", 70 }, { "breath", 25 } });
    F("CHOIRS", "Duet", { { "choir", 2 }, { "spread", 35 }, { "width", 60 }, { "vibrato", 30 } });
    // machines
    F("MACHINES", "Robot", { { "robot", 85 }, { "breath", 5 }, { "vibrato", 6 }, { "bright", 65 }, { "soft", 30 }, { "glide", 30 } });
    F("MACHINES", "Robot choir", { { "robot", 90 }, { "breath", 5 }, { "vibrato", 8 }, { "bright", 70 }, { "soft", 30 }, { "glide", 30 }, { "mode", 1 }, { "choir", 4 }, { "spread", 30 } });
    F("MACHINES", "Talking toy", { { "robot", 100 }, { "breath", 0 }, { "vibrato", 0 }, { "glide", 0 }, { "bright", 40 }, { "soft", 15 }, { "size", 15 }, { "diction", 125 } });
    F("MACHINES", "Deep bot", { { "octave", -1 }, { "size", -60 }, { "robot", 80 }, { "vibrato", 10 }, { "bright", 45 } });
    F("MACHINES", "Chipmunk", { { "octave", 1 }, { "size", 80 }, { "robot", 35 }, { "vibrato", 30 }, { "diction", 130 } });
    F("MACHINES", "Laser choir", { { "robot", 100 }, { "bright", 95 }, { "soft", 10 }, { "vibrato", 0 }, { "glide", 25 }, { "size", 20 }, { "mode", 1 }, { "choir", 3 } });
    F("MACHINES", "Android", { { "robot", 70 }, { "vibrato", 0 }, { "glide", 15 }, { "bright", 75 }, { "clarity", 95 }, { "soft", 20 }, { "diction", 115 } });
    F("MACHINES", "Buzz box", { { "robot", 100 }, { "soft", 0 }, { "clarity", 100 }, { "bright", 85 }, { "choir", 2 }, { "spread", 15 }, { "vibrato", 0 } });
    F("MACHINES", "Monster", { { "octave", -1 }, { "size", -100 }, { "rough", 60 }, { "robot", 40 }, { "drive", 40 }, { "vibrato", 10 } });
    F("MACHINES", "Fairy", { { "size", 70 }, { "breath", 50 }, { "choir", 3 }, { "spread", 40 }, { "width", 80 }, { "vibrato", 40 }, { "robot", 10 } });
    return v;
    }();
    return v;
}

}} // namespace pump::sing
