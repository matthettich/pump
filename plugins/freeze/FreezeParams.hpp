// pump FREEZE: parameters and factory voices (shared by the DSP and the UI).
//
// A granular spectral-freeze synth. A sound (one of seven built in, made by pump's own instruments, or a .wav of
// your own) is cut into overlapping spectra. A note plays a spectral cloud from it: every hop, each voice builds a
// spectrum from GRAINS spectra picked around POSITION (SPRAY apart), blurs it over time (BLUR), shifts it to the
// note's pitch (key tracking from ROOT), stretches it (STRETCH: inharmonic), tilts it, jitters it (SHIMMER), keeps
// it between LOW and HIGH, gives it phases (PHASE: all random, the classic frozen wash, to following on from the last
// hop, smoother and more tonal) and plays it back. SPEED moves POSITION on (0: frozen). WIDTH gives the two channels
// different phases.
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace freeze {

static const char* const kSourceNames[] = { "File", "Sung vowels", "Spring harp", "Glass bells", "Morph wobble", "Saw sweep", "Breath flute", "Spring gong" };
constexpr int kNumSources = 8;
static const char* const kVoiceModeNames[] = { "Poly", "Mono", "Legato" };

enum Param : int {
    kSource = 0, kPosition, kSpeed, kSpray, kGrains, kBlur,
    kRoot, kTrack, kStretch, kTilt, kShimmer, kLow, kHigh, kPhase, kWidth,
    kAttack, kDecay, kSustain, kRelease, kVelocity, kVoices, kVoiceMode, kGlide, kOctave, kVolume,
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    v.push_back({ "source", "Source", "Source", 0, kNumSources - 1, 1, PUnit::None, PScale::Linear, I, kSourceNames, 1 });
    v.push_back({ "position", "Position", "Position", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "speed", "Speed", "Speed", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spray", "Spray", "Spray", 0, 100, 10, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "grains", "Grains", "Grains", 1, 8, 3, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "blur", "Blur", "Blur", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "root", "Root Note", "Root", 24, 96, 60, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "track", "Key Tracking", "Track", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "stretch", "Stretch", "Stretch", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tilt", "Tilt", "Tilt", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "shimmer", "Shimmer", "Shimmer", 0, 100, 10, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "low", "Low Cut", "Low", 20, 2000, 30, PUnit::Hz, PScale::Log, A, nullptr, 0 });
    v.push_back({ "high", "High Cut", "High", 500, 20000, 16000, PUnit::Hz, PScale::Log, A, nullptr, 0 });
    v.push_back({ "phase", "Phase", "Phase", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "width", "Width", "Width", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "A", 1, 8000, 300, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "decay", "Decay", "D", 1, 10000, 800, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sustain", "Sustain", "S", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "release", "Release", "R", 1, 12000, 1500, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "velocity", "Velocity", "Velocity", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
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
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("FROZEN", "Frozen choir", { { "source", 1 }, { "position", 35 }, { "grains", 4 }, { "blur", 40 }, { "phase", 0 } });
    F("FROZEN", "Glass cloud", { { "source", 3 }, { "position", 8 }, { "spray", 6 }, { "shimmer", 40 }, { "width", 100 } });
    F("FROZEN", "Gong wash", { { "source", 7 }, { "position", 20 }, { "grains", 6 }, { "blur", 70 }, { "release", 4000 }, { "track", 60 } });
    F("FROZEN", "Still harp", { { "source", 2 }, { "position", 12 }, { "phase", 70 }, { "grains", 2 }, { "blur", 10 }, { "attack", 60 } });
    F("MOVING", "Drifting vowels", { { "source", 1 }, { "speed", 15 }, { "spray", 15 }, { "grains", 5 }, { "blur", 50 } });
    F("MOVING", "Sweep scan", { { "source", 5 }, { "speed", 30 }, { "phase", 40 }, { "tilt", 20 }, { "attack", 100 } });
    F("MOVING", "Wobble smear", { { "source", 4 }, { "speed", -20 }, { "blur", 80 }, { "grains", 6 }, { "octave", -1 } });
    F("MOVING", "Breath fog", { { "source", 6 }, { "speed", 8 }, { "spray", 40 }, { "shimmer", 50 }, { "high", 9000 }, { "width", 100 } });
    F("STRANGE", "Stretched bells", { { "source", 3 }, { "stretch", 40 }, { "position", 5 }, { "shimmer", 30 }, { "release", 3000 } });
    F("STRANGE", "Squeezed choir", { { "source", 1 }, { "stretch", -35 }, { "position", 50 }, { "blur", 60 } });
    F("STRANGE", "Dark drone", { { "source", 7 }, { "tilt", -60 }, { "track", 30 }, { "octave", -1 }, { "attack", 2000 }, { "release", 5000 }, { "blur", 90 } });
    F("STRANGE", "Air only", { { "source", 6 }, { "low", 900 }, { "tilt", 40 }, { "high", 16000 }, { "shimmer", 70 }, { "spray", 30 }, { "phase", 0 }, { "volume", 12 } });
    return v;
}

}} // namespace pump::freeze
