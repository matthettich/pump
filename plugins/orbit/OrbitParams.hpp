// pump ORBIT: parameters and factory voices (shared by the DSP and the UI).
//
// A strange-attractor synth. Each voice runs a chaotic system (Lorenz, Rössler, a double scroll, Thomas, Halvorsen or
// a forced Duffing oscillator), integrated fast enough that one turn of its orbit lasts one period of the note, and
// listens to it from an angle (AXIS: x, y or z, and between). CHAOS sweeps the system's parameter from a clean,
// periodic orbit through period doubling into full chaos; SYNC pulls the orbit back to where it started every period
// of the note (0: free and drifting, 100: locked to the pitch, a waveform that repeats); WIDTH listens to the right
// channel from a different angle. After it: drive, a ladder low-pass with its own envelope (which can also push the
// chaos), the amp envelope, and DRIFT: a slow attractor per voice wandering the pitch, the cutoff and the chaos.
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace orbit {

enum System : int { kLorenz = 0, kRossler, kScroll, kThomas, kHalvorsen, kDuffing, kNumSystems };
static const char* const kSystemNames[] = { "Lorenz", "Rossler", "Double scroll", "Thomas", "Halvorsen", "Duffing" };
static const char* const kSystemKeys[] = { "LORENZ", "ROSSLER", "SCROLL", "THOMAS", "HALVORSEN", "DUFFING" };
static const char* const kVoiceModeNames[] = { "Poly", "Mono", "Legato" };

enum Param : int {
    kSystem = 0, kChaos, kSync, kAxis, kWidth, kDrive,
    kCutoff, kReso, kKeytrack, kEnvCut, kEnvChaos, kModA, kModD, kModS, kModR,
    kDriftRate, kDriftPitch, kDriftCut, kDriftChaos,
    kAttack, kDecay, kSustain, kRelease, kVelocity, kVoices, kVoiceMode, kGlide, kOctave, kFine, kVolume,
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    v.push_back({ "system", "System", "System", 0, kNumSystems - 1, kRossler, PUnit::None, PScale::Linear, I, kSystemNames, 1 });
    v.push_back({ "chaos", "Chaos", "Chaos", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sync", "Sync", "Sync", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "axis", "Axis", "Axis", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "width", "Width", "Width", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "cutoff", "Cutoff", "Cutoff", 30, 18000, 6000, PUnit::Hz, PScale::Log, A, nullptr, 0 });
    v.push_back({ "reso", "Resonance", "Reso", 0, 100, 15, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "keytrack", "Key Track", "Keytrack", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "env_cut", "Env to Cutoff", "Env>Cut", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "env_chaos", "Env to Chaos", "Env>Chaos", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mod_attack", "Mod Env Attack", "A", 0.5f, 5000, 5, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mod_decay", "Mod Env Decay", "D", 1, 10000, 400, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mod_sustain", "Mod Env Sustain", "S", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mod_release", "Mod Env Release", "R", 1, 10000, 300, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "drift_rate", "Drift Rate", "Rate", 0.02f, 10, 0.3f, PUnit::Hz, PScale::Log, A, nullptr, 2 });
    v.push_back({ "drift_pitch", "Drift to Pitch", "Pitch", 0, 100, 0, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "drift_cut", "Drift to Cutoff", "Cutoff", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drift_chaos", "Drift to Chaos", "Chaos", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "A", 0.5f, 5000, 4, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "D", 1, 10000, 600, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sustain", "Sustain", "S", 0, 100, 80, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "release", "Release", "R", 1, 10000, 300, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "velocity", "Velocity", "Velocity", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voices", "Voices", "Voices", 1, 8, 8, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "voice_mode", "Voice Mode", "Mode", 0, 2, 0, PUnit::None, PScale::Linear, I, kVoiceModeNames, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "octave", "Octave", "Oct", -3, 3, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "fine", "Fine Tune", "Fine", -100, 100, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
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
    F("BASS", "Rossler bass", { { "system", kRossler }, { "chaos", 10 }, { "sync", 90 }, { "octave", -1 }, { "cutoff", 1800 }, { "reso", 30 }, { "env_cut", 50 }, { "voice_mode", 2 }, { "glide", 50 }, { "drive", 45 }, { "volume", 3 } });
    F("BASS", "Doubling bass", { { "system", kRossler }, { "chaos", 35 }, { "sync", 70 }, { "octave", -1 }, { "cutoff", 2500 }, { "env_chaos", 40 }, { "mod_decay", 250 }, { "voice_mode", 1 }, { "drive", 50 }, { "volume", 3 } });
    F("BASS", "Duffing growl", { { "system", kDuffing }, { "chaos", 55 }, { "sync", 40 }, { "octave", -1 }, { "cutoff", 1400 }, { "reso", 45 }, { "env_cut", 40 }, { "voice_mode", 2 }, { "drive", 60 }, { "volume", 6 } });
    F("LEAD", "Lorenz lead", { { "system", kLorenz }, { "chaos", 20 }, { "sync", 85 }, { "cutoff", 5000 }, { "reso", 25 }, { "drift_pitch", 8 }, { "drift_rate", 4 }, { "voice_mode", 2 }, { "glide", 40 }, { "volume", 2 } });
    F("LEAD", "Scroll screamer", { { "system", kScroll }, { "chaos", 70 }, { "sync", 75 }, { "cutoff", 7000 }, { "reso", 40 }, { "drive", 70 }, { "voice_mode", 1 }, { "volume", 7 } });
    F("LEAD", "Chaos pluck", { { "system", kRossler }, { "chaos", 15 }, { "sync", 80 }, { "env_chaos", 80 }, { "env_cut", 60 }, { "cutoff", 1500 }, { "mod_decay", 300 }, { "sustain", 0 }, { "decay", 700 }, { "volume", 4 } });
    F("PAD", "Thomas pad", { { "system", kThomas }, { "chaos", 40 }, { "sync", 50 }, { "width", 80 }, { "attack", 700 }, { "release", 1800 }, { "cutoff", 3500 }, { "drift_cut", 30 }, { "drift_chaos", 25 } });
    F("PAD", "Halvorsen haze", { { "system", kHalvorsen }, { "chaos", 50 }, { "sync", 30 }, { "width", 100 }, { "attack", 900 }, { "release", 2500 }, { "cutoff", 2800 }, { "drift_chaos", 40 }, { "drift_rate", 0.15f } });
    F("PAD", "Weather system", { { "system", kLorenz }, { "chaos", 75 }, { "sync", 20 }, { "width", 70 }, { "attack", 1200 }, { "release", 3000 }, { "cutoff", 2200 }, { "reso", 35 }, { "drift_cut", 50 }, { "drift_chaos", 30 } });
    F("TEXTURE", "Free orbit", { { "system", kRossler }, { "chaos", 85 }, { "sync", 0 }, { "width", 90 }, { "attack", 300 }, { "release", 1500 } });
    F("TEXTURE", "Strange noise", { { "system", kScroll }, { "chaos", 100 }, { "sync", 0 }, { "width", 100 }, { "drive", 80 }, { "cutoff", 9000 }, { "volume", 3 } });
    F("TEXTURE", "Period doubling", { { "system", kRossler }, { "chaos", 0 }, { "sync", 60 }, { "drift_chaos", 100 }, { "drift_rate", 0.08f }, { "attack", 400 }, { "release", 2000 }, { "width", 60 } });
    F("TEXTURE", "Duffing bells", { { "system", kDuffing }, { "chaos", 20 }, { "sync", 50 }, { "octave", 1 }, { "sustain", 0 }, { "decay", 1500 }, { "release", 1500 }, { "env_chaos", 60 }, { "width", 60 }, { "volume", 2 } });
    return v;
    }();
    return v;
}

}} // namespace pump::orbit
