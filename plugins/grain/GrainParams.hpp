// pump GRAIN: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pump { namespace grain {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kPosition = 0, kSize, kDensity, kSpray, kSync, kDivision, kEnvelope, kReverse, kStereo,
    kPitch, kPitchSpread, kQuantize,
    kFeedback, kFilter, kCutoff, kResonance, kSpace, kMotionRate, kMotionDepth,
    kMorphX, kMorphY, kFreeze, kMix, kOutGain, kBypass,
    kNumParams
};

enum Envelope : int { kEnvSmooth = 0, kEnvSoft, kEnvPerc, kEnvSwell, kEnvSquare, kNumEnvelopes };
static const char* const kEnvelopeNames[] = { "Smooth", "Soft", "Percussive", "Swell", "Square" };
enum Scale : int { kScaleOff = 0, kScaleChromatic, kScaleMajor, kScaleMinor, kScalePentatonic, kScaleOctaves, kScaleFifths, kNumScales };
static const char* const kScaleNames[] = { "Off", "Chromatic", "Major", "Minor", "Pentatonic", "Octaves", "Fifths" };
enum FilterMode : int { kFilterOff = 0, kFilterLP, kFilterBP, kFilterHP, kNumFilterModes };
static const char* const kFilterNames[] = { "Off", "Low Pass", "Band Pass", "High Pass" };

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable;
    // every control here is sampled per grain or smoothed, so all of them are automatable
    v.push_back({ "position", "Position", "Position", 0, 4000, 250, PUnit::ms, PScale::Power, A, nullptr, 2.5f });
    v.push_back({ "size", "Grain Size", "Size", 5, 1000, 120, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "density", "Density", "Density", 1, 100, 12, PUnit::None, PScale::Log, A, nullptr, 1 });
    v.push_back({ "spray", "Spray", "Spray", 0, 100, 10, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sync", "Tempo Sync", "Sync", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "division", "Sync Division", "Division", 0, kNumNotes - 1, kNote16, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "envelope", "Envelope", "Envelope", 0, kNumEnvelopes - 1, kEnvSmooth, PUnit::None, PScale::Linear, A | kPInteger, kEnvelopeNames, 1 });
    v.push_back({ "reverse", "Reverse", "Reverse", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "stereo", "Stereo Spread", "Stereo", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pitch", "Pitch", "Pitch", -24, 24, 0, PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    v.push_back({ "pitch_spread", "Pitch Spread", "Spread", 0, 24, 0, PUnit::None, PScale::Power, A, nullptr, 2.0f });
    v.push_back({ "quantize", "Quantize", "Quantize", 0, kNumScales - 1, kScaleOff, PUnit::None, PScale::Linear, A | kPInteger, kScaleNames, 1 });
    v.push_back({ "feedback", "Feedback", "Feedback", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "filter", "Filter", "Filter", 0, kNumFilterModes - 1, kFilterOff, PUnit::None, PScale::Linear, A | kPInteger, kFilterNames, 1 });
    v.push_back({ "cutoff", "Cutoff", "Cutoff", 20, 20000, 2000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "resonance", "Resonance", "Reso", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "space", "Space", "Space", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "motion_rate", "Motion Rate", "Rate", 0.02f, 10, 0.25f, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "motion_depth", "Motion Depth", "Depth", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "morph_x", "Morph X (Smooth - Glitchy)", "Morph X", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "morph_y", "Morph Y (Steady - Wild)", "Morph Y", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "freeze", "Freeze", "Freeze", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
}

// Factory presets. Envelope: 0 smooth 1 soft 2 percussive 3 swell 4 square; quantize: 0 off 1 chromatic 2 major
// 3 minor 4 pentatonic 5 octaves 6 fifths; filter: 0 off 1 LP 2 BP 3 HP; division: 5 = 1/16, 8 = 1/8, 11 = 1/4.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Soft cloud", { { "size", 300 }, { "density", 18 }, { "spray", 30 }, { "stereo", 80 }, { "space", 45 }, { "motion_depth", 30 }, { "mix", 60 } });
    F("Octave shimmer", { { "pitch", 12 }, { "size", 180 }, { "density", 16 }, { "spray", 15 }, { "feedback", 35 }, { "space", 60 },
                          { "filter", 3 }, { "cutoff", 300 }, { "resonance", 10 }, { "mix", 40 } });
    F("Sub-octave haze", { { "pitch", -12 }, { "size", 240 }, { "density", 14 }, { "spray", 25 }, { "filter", 1 }, { "cutoff", 2500 }, { "space", 40 }, { "mix", 45 } });
    F("Fifths choir", { { "pitch_spread", 12 }, { "quantize", 6 }, { "size", 260 }, { "density", 22 }, { "spray", 20 }, { "envelope", 1 },
                        { "stereo", 70 }, { "space", 50 }, { "mix", 50 } });
    F("Reverse bloom", { { "reverse", 100 }, { "envelope", 3 }, { "size", 420 }, { "density", 8 }, { "position", 600 }, { "spray", 20 }, { "space", 45 }, { "mix", 50 } });
    F("Tempo grains 1/16", { { "sync", 1 }, { "division", 5 }, { "size", 90 }, { "envelope", 4 }, { "position", 60 }, { "spray", 0 }, { "space", 15 },
                             { "motion_depth", 0 }, { "mix", 60 } });
    F("Glitch scatter", { { "size", 25 }, { "density", 55 }, { "spray", 60 }, { "envelope", 2 }, { "reverse", 30 }, { "stereo", 90 }, { "morph_x", 50 },
                          { "space", 20 }, { "mix", 55 } });
    F("Granular delay", { { "position", 375 }, { "size", 200 }, { "density", 8 }, { "spray", 4 }, { "feedback", 55 }, { "space", 20 }, { "motion_depth", 10 }, { "mix", 40 } });
    F("Slow scan", { { "position", 1800 }, { "spray", 70 }, { "size", 500 }, { "density", 10 }, { "stereo", 70 }, { "motion_rate", 0.08f }, { "motion_depth", 50 },
                     { "space", 50 }, { "mix", 55 } });
    S("Drums: tempo stutter", { { "sync", 1 }, { "division", 5 }, { "size", 70 }, { "envelope", 2 }, { "position", 20 }, { "spray", 0 }, { "motion_depth", 0 },
                                { "stereo", 30 }, { "space", 10 }, { "mix", 50 } });
    S("Drums: granular room", { { "size", 40 }, { "density", 50 }, { "spray", 25 }, { "envelope", 2 }, { "space", 50 }, { "stereo", 90 }, { "mix", 30 } });
    S("Vocals: shimmer halo", { { "pitch", 12 }, { "size", 220 }, { "density", 16 }, { "spray", 12 }, { "feedback", 25 }, { "filter", 3 }, { "cutoff", 250 },
                                { "space", 55 }, { "stereo", 80 }, { "mix", 30 } });
    S("Vocals: whisper cloud", { { "size", 90 }, { "density", 40 }, { "spray", 50 }, { "filter", 3 }, { "cutoff", 1500 }, { "stereo", 100 }, { "space", 50 }, { "mix", 35 } });
    S("Guitar: ambient swell", { { "envelope", 3 }, { "size", 450 }, { "density", 10 }, { "spray", 30 }, { "space", 60 }, { "motion_depth", 25 }, { "mix", 50 } });
    S("Guitar: octave sparkle", { { "pitch", 12 }, { "pitch_spread", 12 }, { "quantize", 5 }, { "envelope", 1 }, { "size", 140 }, { "density", 20 },
                                  { "spray", 20 }, { "space", 45 }, { "mix", 35 } });
    S("Bass: sub thickener", { { "pitch", -12 }, { "size", 150 }, { "density", 16 }, { "spray", 3 }, { "stereo", 0 }, { "filter", 1 }, { "cutoff", 400 },
                               { "space", 0 }, { "motion_depth", 0 }, { "mix", 30 } });
    S("Keys: chorus cloud", { { "pitch_spread", 0.3f }, { "size", 120 }, { "density", 24 }, { "spray", 8 }, { "stereo", 80 }, { "motion_depth", 30 }, { "mix", 45 } });
    S("Synths: pentatonic sparkle", { { "pitch", 12 }, { "pitch_spread", 12 }, { "quantize", 4 }, { "size", 80 }, { "density", 25 }, { "envelope", 2 },
                                      { "spray", 30 }, { "space", 45 }, { "stereo", 80 }, { "mix", 40 } });
    S("Pads: endless texture", { { "size", 800 }, { "density", 10 }, { "spray", 60 }, { "feedback", 40 }, { "space", 70 }, { "motion_depth", 40 }, { "mix", 70 } });
    S("Bus: subtle air", { { "pitch", 12 }, { "quantize", 5 }, { "filter", 3 }, { "cutoff", 3000 }, { "size", 160 }, { "density", 18 }, { "space", 40 }, { "mix", 15 } });
    C("Worn tape cloud", { { "reverse", 40 }, { "pitch_spread", 1 }, { "motion_rate", 0.12f }, { "motion_depth", 60 }, { "filter", 1 }, { "cutoff", 2500 },
                           { "resonance", 30 }, { "feedback", 30 }, { "size", 220 }, { "mix", 60 } });
    C("Minor-key rain", { { "quantize", 3 }, { "pitch_spread", 24 }, { "size", 40 }, { "density", 30 }, { "envelope", 2 }, { "spray", 70 }, { "space", 55 },
                          { "stereo", 100 }, { "mix", 50 } });
    C("Rising fifths", { { "pitch", 7 }, { "quantize", 6 }, { "feedback", 70 }, { "size", 300 }, { "density", 12 }, { "spray", 20 }, { "space", 60 }, { "mix", 50 } });
    C("Resonant swarm", { { "filter", 2 }, { "cutoff", 1200 }, { "resonance", 75 }, { "density", 70 }, { "size", 15 }, { "spray", 80 }, { "motion_rate", 1.5f },
                          { "motion_depth", 60 }, { "stereo", 100 }, { "mix", 60 } });
    C("Time smear", { { "position", 2000 }, { "spray", 100 }, { "size", 600 }, { "density", 16 }, { "space", 50 }, { "mix", 60 } });
    C("Wild corner", { { "morph_x", 70 }, { "morph_y", 85 }, { "quantize", 2 }, { "space", 40 }, { "mix", 55 } });
    return v;
}

}} // namespace pump::grain
