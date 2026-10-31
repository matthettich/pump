// pump VOX: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace vox {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kMode = 0, kRoute, kBands, kLow, kHigh, kWidth, kDetail, kFftSize, kTube, kDrive, kSmear,
    kFormant, kAttack, kRelease, kFlatten, kEmphasis, kSibilance, kNoise, kGate, kFreeze,
    kSource, kWave, kOctave, kChord, kKey, kScale, kGlide, kDetune,
    kSpread, kEnsemble, kVoiceDry, kCarrierDry, kOutGain, kBypass,
    // ENGINE: the vocoder (everything above) or VOICE FX (VoxFx.hpp: pitch, grit, robot, character)
    kEngine,
    kFxPitchOn, kFxPitch, kFxFormant, kFxSub,
    kFxGritOn, kFxFry, kFxRasp, kFxRattle, kFxRattleHz, kFxDrive, kFxTone,
    kFxRobotOn, kFxMono, kFxNote, kFxGlide, kFxRing, kFxRingHz, kFxComb, kFxCrush,
    kFxCharOn, kFxFilter, kFxStack, kFxStackMix, kFxDouble, kFxSlap, kFxSlapMs,
    kFxMix,
    kNumParams
};

enum EngineKind : int { kEngineVocoder = 0, kEngineVoiceFx, kNumEngines };
static const char* const kEngineNames[] = { "Vocoder", "Voice FX" };
static const char* const kFxFilterNamesP[] = { "Off", "Radio", "Phone", "Megaphone", "Walkie" };
static const char* const kFxStackNamesP[] = { "Off", "Octave up", "Octave down", "Up + down" };

enum Mode : int { kClassic = 0, kSpectral, kTalkbox, kConvolve, kNumModes };
static const char* const kModeNames[] = { "Classic", "Spectral", "Talkbox", "Convolve" };

// Where the voice (modulator) and the carrier come from.
enum Route : int { kRouteSynth = 0, kRouteSidechain, kRouteSidechainVoice, kRouteSplit, kNumRoutes };
static const char* const kRouteNames[] = { "Synth carrier", "Sidechain carrier", "Sidechain voice", "Split L/R" };

// Classic band counts
static const char* const kBandNames[] = { "8", "12", "16", "20", "24", "32", "40" };
static const int kBandCounts[] = { 8, 12, 16, 20, 24, 32, 40 };
constexpr int kNumBandChoices = 7;
constexpr int kMaxBands = 40;

// FFT sizes of SPECTRAL and CONVOLVE at 44.1/48 kHz (doubled at 88.2/96 kHz, x4 above)
static const char* const kFftNames[] = { "512", "1024", "2048", "4096" };
constexpr int kNumFft = 4;

// The built-in carrier synth
enum Source : int { kSourceAuto = 0, kSourceMidi, kSourceFollow, kNumSources };
static const char* const kSourceNames[] = { "Auto", "MIDI", "Follow" };
enum Wave : int { kWaveSaw = 0, kWaveSquare, kWaveSuper, kWaveBuzz, kWaveNoise, kNumWaves };
static const char* const kWaveNames[] = { "Saw", "Square", "Super", "Buzz", "Noise" };
enum Chord : int { kChordOff = 0, kChordOctave, kChordFifth, kChordTriad, kChordSeventh, kChordSus, kChordStack, kNumChords };
static const char* const kChordNames[] = { "Off", "Octave", "Fifth", "Triad", "Seventh", "Sus", "Stack" };
static const char* const kKeyNamesV[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
// Scale of the voice-following carrier: Exact follows the voice as it is; the rest snap to the scale
// (pitch::ScaleId + 1).
static const char* const kScaleNamesV[] = { "Exact", "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor", "Dorian",
    "Phrygian", "Lydian", "Mixolydian", "Locrian", "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone" };
constexpr int kNumScaleChoices = 15;

constexpr float kGateOff = -90.0f;   // the gate's bottom end stop means "off"

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    // mode, routing and FFT size change the signal path and the latency: not automatable (they fade out and back in)
    v.push_back({ "mode", "Mode", "Mode", 0, kNumModes - 1, kClassic, PUnit::None, PScale::Linear, kPInteger, kModeNames, 1 });
    v.push_back({ "route", "Routing", "Routing", 0, kNumRoutes - 1, kRouteSynth, PUnit::None, PScale::Linear, kPInteger, kRouteNames, 1 });
    v.push_back({ "bands", "Bands", "Bands", 0, kNumBandChoices - 1, 2, PUnit::None, PScale::Linear, A | kPInteger, kBandNames, 1 });
    v.push_back({ "low", "Lowest Band", "Low", 40, 1000, 100, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "high", "Highest Band", "High", 1000, 16000, 8000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "width", "Band Width", "Width", 0.5f, 2.0f, 1.0f, PUnit::None, PScale::Log, A, nullptr, 1 });
    v.push_back({ "detail", "Detail", "Detail", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fft_size", "FFT Size", "FFT", 0, kNumFft - 1, 1, PUnit::None, PScale::Linear, kPInteger, kFftNames, 1 });
    v.push_back({ "tube", "Tube", "Tube", 0, 100, 35, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Driver Drive", "Drive", 0, 24, 6, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "smear", "Smear", "Smear", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "formant", "Formant", "Formant", -12, 12, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "attack", "Attack", "Attack", 0.5f, 100, 4, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "release", "Release", "Release", 5, 1000, 60, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "flatten", "Carrier Flatten", "Flatten", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "emphasis", "Emphasis", "Emphasis", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sibilance", "Sibilance", "Sibilance", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "noise", "Unvoiced Noise", "Noise", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "gate", "Voice Gate", "Gate", kGateOff, -20, kGateOff, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "freeze", "Freeze", "Freeze", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "source", "Synth Source", "Source", 0, kNumSources - 1, kSourceAuto, PUnit::None, PScale::Linear, A | kPInteger, kSourceNames, 1 });
    v.push_back({ "wave", "Synth Wave", "Wave", 0, kNumWaves - 1, kWaveSaw, PUnit::None, PScale::Linear, A | kPInteger, kWaveNames, 1 });
    v.push_back({ "octave", "Synth Octave", "Octave", -2, 2, 0, PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    v.push_back({ "chord", "Follow Chord", "Chord", 0, kNumChords - 1, kChordOff, PUnit::None, PScale::Linear, A | kPInteger, kChordNames, 1 });
    v.push_back({ "key", "Key", "Key", 0, 11, 0, PUnit::None, PScale::Linear, A | kPInteger, kKeyNamesV, 1 });
    v.push_back({ "scale", "Scale", "Scale", 0, kNumScaleChoices - 1, 1, PUnit::None, PScale::Linear, A | kPInteger, kScaleNamesV, 1 });
    v.push_back({ "glide", "Follow Glide", "Glide", 0, 500, 40, PUnit::ms, PScale::Power, A, nullptr, 2.0f });
    v.push_back({ "detune", "Synth Detune", "Detune", 0, 100, 25, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spread", "Stereo Spread", "Spread", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "ensemble", "Ensemble", "Ensemble", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voice_dry", "Dry Voice", "Voice", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "carrier_dry", "Dry Carrier", "Carrier", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    // ENGINE changes the signal path and the latency: not automatable (it fades out and back in)
    v.push_back({ "engine", "Engine", "Engine", 0, kNumEngines - 1, kEngineVocoder, PUnit::None, PScale::Linear, kPInteger, kEngineNames, 1 });
    v.push_back({ "fx_pitch_on", "FX Pitch On", "Pitch", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "fx_pitch", "FX Pitch", "Pitch", -24, 24, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_formant", "FX Formant", "Formant", -12, 12, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_sub", "FX Sub Octave", "Sub", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_grit_on", "FX Grit On", "Grit", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "fx_fry", "FX Fry", "Fry", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_rasp", "FX Rasp", "Rasp", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_rattle", "FX Rattle", "Rattle", 0, 100, 20, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_rattle_hz", "FX Rattle Rate", "Rate", 10, 90, 38, PUnit::Hz, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_drive", "FX Drive", "Drive", 0, 36, 9, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_tone", "FX Tone", "Tone", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_robot_on", "FX Robot On", "Robot", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "fx_mono", "FX Monotone", "Mono", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_note", "FX Robot Note", "Note", 24, 84, 45, PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    v.push_back({ "fx_glide", "FX Robot Glide", "Glide", 0, 500, 30, PUnit::ms, PScale::Power, A, nullptr, 2.0f });
    v.push_back({ "fx_ring", "FX Ring", "Ring", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_ring_hz", "FX Ring Frequency", "Ring Hz", 10, 2000, 110, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "fx_comb", "FX Comb", "Comb", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_crush", "FX Crush", "Crush", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_char_on", "FX Character On", "Character", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "fx_filter", "FX Filter", "Filter", 0, 4, 0, PUnit::None, PScale::Linear, A | kPInteger, kFxFilterNamesP, 1 });
    v.push_back({ "fx_stack", "FX Stack", "Stack", 0, 3, 0, PUnit::None, PScale::Linear, A | kPInteger, kFxStackNamesP, 1 });
    v.push_back({ "fx_stack_mix", "FX Stack Mix", "Stack Mix", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_double", "FX Double", "Double", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_slap", "FX Slap", "Slap", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_slap_ms", "FX Slap Time", "Slap ms", 20, 280, 110, PUnit::ms, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "fx_mix", "FX Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. mode: 0 classic 1 spectral 2 talkbox 3 convolve; route: 0 synth 1 sidechain carrier 2 sidechain
// voice 3 split; bands: 0 8, 1 12, 2 16, 3 20, 4 24, 5 32, 6 40; wave: 0 saw 1 square 2 super 3 buzz 4 noise;
// chord: 0 off 1 octave 2 fifth 3 triad 4 seventh 5 sus 6 stack; scale: 0 exact 1 chromatic 2 major 3 minor ...
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto R = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "ROUTING" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Robot voice", { { "bands", 2 }, { "scale", 1 }, { "glide", 0 }, { "wave", 0 }, { "detune", 10 } });
    F("Smooth follow", { { "bands", 4 }, { "scale", 0 }, { "glide", 60 }, { "wave", 2 }, { "release", 90 } });
    F("Vintage strings", { { "bands", 1 }, { "wave", 2 }, { "chord", 3 }, { "scale", 2 }, { "ensemble", 70 }, { "release", 140 }, { "spread", 60 }, { "out_gain", -5 } });
    F("Crisp 32-band", { { "bands", 5 }, { "high", 12000 }, { "emphasis", 55 }, { "sibilance", 40 }, { "attack", 2 }, { "release", 40 } });
    F("Lo-fi 8-band", { { "bands", 0 }, { "high", 4000 }, { "width", 0.7f }, { "noise", 10 }, { "sibilance", 5 }, { "release", 80 }, { "out_gain", -12 } });
    F("Talkbox lead", { { "mode", 2 }, { "wave", 1 }, { "tube", 35 }, { "drive", 6 }, { "detail", 60 }, { "spread", 15 } });
    F("Talkbox funk", { { "mode", 2 }, { "wave", 3 }, { "tube", 60 }, { "drive", 12 }, { "glide", 25 }, { "scale", 3 }, { "key", 9 } });
    F("Spectral clear", { { "mode", 1 }, { "detail", 45 }, { "flatten", 70 }, { "emphasis", 40 }, { "wave", 2 } });
    F("Spectral whisper", { { "mode", 1 }, { "wave", 4 }, { "detail", 55 }, { "noise", 0 }, { "sibilance", 20 }, { "release", 90 } });
    F("Convolve metal", { { "mode", 3 }, { "detail", 100 }, { "smear", 0 }, { "wave", 0 } });
    F("Convolve haze", { { "mode", 3 }, { "fft_size", 2 }, { "detail", 80 }, { "smear", 85 }, { "wave", 2 }, { "ensemble", 40 } });
    F("Harmony stack", { { "bands", 4 }, { "chord", 6 }, { "scale", 2 }, { "wave", 2 }, { "spread", 70 }, { "ensemble", 30 } });
    R("Synth carrier on sidechain", { { "route", 1 }, { "bands", 4 }, { "flatten", 50 } });
    R("Voice on sidechain", { { "route", 2 }, { "bands", 4 }, { "flatten", 50 } });
    R("Split L/R (one stereo track)", { { "route", 3 }, { "bands", 2 }, { "flatten", 0 }, { "noise", 0 }, { "out_gain", -7 } });
    R("Sidechain talkbox", { { "route", 1 }, { "mode", 2 }, { "tube", 45 }, { "drive", 8 }, { "out_gain", -4 } });
    C("Giant", { { "formant", -7 }, { "octave", -1 }, { "bands", 4 }, { "release", 90 } });
    C("Chipmunk choir", { { "formant", 7 }, { "octave", 1 }, { "chord", 3 }, { "scale", 2 }, { "wave", 2 }, { "out_gain", -5 } });
    C("Frozen vowel pad", { { "mode", 1 }, { "attack", 60 }, { "release", 1000 }, { "wave", 2 }, { "chord", 4 }, { "ensemble", 60 } });
    C("Octave-up formants", { { "bands", 0 }, { "width", 2 }, { "formant", 12 }, { "glide", 0 }, { "scale", 1 }, { "out_gain", -4 } });
    C("Broken radio", { { "mode", 3 }, { "fft_size", 0 }, { "detail", 30 }, { "smear", 100 }, { "wave", 3 }, { "out_gain", 4 } });
    // VOICE FX (engine 1)
    auto X = [&](const char* n, std::vector<PresetValue> p) { p.insert(p.begin(), { "engine", 1 }); v.push_back({ n, std::move(p), "VOICE FX" }); };
    X("Electro gravel", { { "fx_pitch", -3 }, { "fx_formant", -3 }, { "fx_grit_on", 1 }, { "fx_fry", 45 }, { "fx_rasp", 30 }, { "fx_rattle", 25 }, { "fx_drive", 12 } });
    X("Deep monster", { { "fx_pitch", -7 }, { "fx_formant", -5 }, { "fx_sub", 40 }, { "fx_grit_on", 1 }, { "fx_fry", 30 }, { "fx_rasp", 15 }, { "fx_rattle", 15 }, { "fx_drive", 6 }, { "fx_tone", -30 } });
    X("Low and dirty", { { "fx_pitch", -5 }, { "fx_formant", -2 }, { "fx_grit_on", 1 }, { "fx_fry", 20 }, { "fx_rasp", 10 }, { "fx_rattle", 5 }, { "fx_drive", 24 }, { "fx_char_on", 1 }, { "fx_filter", 1 } });
    X("Monotone robot", { { "fx_robot_on", 1 }, { "fx_mono", 100 }, { "fx_glide", 10 }, { "fx_comb", 25 }, { "fx_char_on", 1 }, { "fx_double", 30 } });
    X("Sci-fi ring robot", { { "fx_pitch", -2 }, { "fx_robot_on", 1 }, { "fx_mono", 80 }, { "fx_ring", 60 }, { "fx_ring_hz", 70 } });
    X("Tin can droid", { { "fx_robot_on", 1 }, { "fx_mono", 100 }, { "fx_comb", 70 }, { "fx_crush", 30 }, { "fx_char_on", 1 }, { "fx_filter", 2 } });
    X("Megaphone", { { "fx_pitch_on", 0 }, { "fx_char_on", 1 }, { "fx_filter", 3 }, { "fx_slap", 25 }, { "fx_slap_ms", 90 } });
    X("Radio host", { { "fx_pitch_on", 0 }, { "fx_char_on", 1 }, { "fx_filter", 1 }, { "fx_double", 20 } });
    X("Chipmunk", { { "fx_pitch", 7 }, { "fx_formant", 6 } });
    X("Gender bend down", { { "fx_pitch", -4 }, { "fx_formant", -4 } });
    X("Octave stack", { { "fx_pitch", 0 }, { "fx_char_on", 1 }, { "fx_stack", 3 }, { "fx_stack_mix", 60 }, { "fx_double", 40 } });
    X("Crushed droid", { { "fx_pitch", -2 }, { "fx_robot_on", 1 }, { "fx_mono", 70 }, { "fx_crush", 60 }, { "fx_ring", 20 }, { "fx_ring_hz", 300 } });
    return v;
    }();
    return v;
}

}} // namespace pump::vox
