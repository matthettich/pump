// pump CRUSH: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace crush {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kMode = 0, kDrive, kBits, kRate, kJitter, kOrder,
    kWow, kFlutter, kHiss, kCrackle, kDropout, kAge,
    kLowCut, kHighCut, kMix, kOutGain, kBypass,
    kNumParams
};

// Sampler voicings: the converter's character (quantiser, input and output filters). Each has a typical rate and
// word length that clicking it in the window also sets.
enum Mode : int { kClean = 0, kGrit12, kHifi12, kDark12, kCompand8, kToy, kPhone, kNumModes };
static const char* const kModeNames[] = { "Clean", "Grit 12", "Hi-fi 12", "Dark 12", "Companded 8", "Toy", "Phone" };
static const char* const kModeKeys[] = { "CLEAN", "GRIT 12", "HIFI 12", "DARK 12", "8 BIT", "TOY", "PHONE" };
// where the media (wow, flutter, hiss, crackle, dropouts, age) sits: after the sampler (a worn copy of a crushed
// sample) or before it (sampling something already worn)
enum Order : int { kMediaAfter = 0, kMediaBefore, kNumOrders };
static const char* const kOrderNames[] = { "After", "Before" };

inline int clampi(int m) { return m < 0 ? 0 : (m >= kNumModes ? kNumModes - 1 : m); }

struct Voicing {
    double rate, bits;      // the rate and word length the voicing is known for
    int quant;              // 0 linear, 1 companded (mu-law), 2 linear with DAC bit-weight errors
    double dacError;        // relative error of each bit's weight (quant 2)
    int aaPoles;            // input (anti-alias) filter: 0, 2, 4 poles at 0.45 x rate, or 8 at 0.40 x rate
    int reconPoles;         // output (reconstruction) filter: 0, 2 or 4 poles
    double reconAt, reconQ; // output filter cutoff (x rate) and resonance
    double warmth;          // converter-stage saturation 0..1
};
inline const Voicing& voicing(int m)
{
    static const Voicing v[kNumModes] = {
        { 12000.0,  6.0, 0, 0.000, 0, 0, 0.45, 0.707, 0.00 },   // Clean: hold and quantise, nothing else
        { 26040.0, 12.0, 2, 0.006, 0, 2, 0.42, 0.707, 0.20 },   // Grit 12: no input filter (aliases), gentle output
        { 40000.0, 12.0, 0, 0.000, 8, 2, 0.45, 0.707, 0.10 },   // Hi-fi 12: filtered in and out, punchy and clean
        { 30000.0, 12.0, 2, 0.015, 2, 4, 0.33, 1.300, 0.15 },   // Dark 12: a low, resonant four-pole output filter
        { 25000.0,  8.0, 1, 0.000, 2, 2, 0.42, 0.707, 0.10 },   // Companded 8: mu-law 8-bit, quiet parts keep detail
        { 11025.0,  8.0, 2, 0.050, 0, 0, 0.45, 0.707, 0.30 },   // Toy: no filters, badly matched bits
        {  8000.0,  8.0, 1, 0.000, 8, 4, 0.42, 0.707, 0.25 },   // Phone: mu-law 8 kHz, 300 Hz - 3.4 kHz
    };
    return v[clampi(m)];
}

// End stops mean "off".
constexpr float kRateMin = 200.0f, kRateOff = 48000.0f, kBitsOff = 24.0f;
constexpr float kLowCutOff = 10.0f, kHighCutOff = 22000.0f;
inline bool rateActive(double r) { return r < kRateOff - 1.0; }
inline bool bitsActive(double b) { return b < kBitsOff - 0.01; }
inline bool lowCutActive(double f) { return f > kLowCutOff + 0.05; }
inline bool highCutActive(double f) { return f < kHighCutOff - 10.0; }

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    v.push_back({ "mode", "Voicing", "Voicing", 0, kNumModes - 1, kGrit12, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bits", "Bits", "Bits", 1, kBitsOff, 12, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "rate", "Sample Rate", "Rate", kRateMin, kRateOff, 26040, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "jitter", "Jitter", "Jitter", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "order", "Media", "Media", 0, kNumOrders - 1, kMediaAfter, PUnit::None, PScale::Linear, A | kPInteger, kOrderNames, 1 });
    v.push_back({ "wow", "Wow", "Wow", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "flutter", "Flutter", "Flutter", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "hiss", "Hiss", "Hiss", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "crackle", "Crackle", "Crackle", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "dropout", "Dropouts", "Dropouts", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "age", "Age", "Age", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "low_cut", "Low Cut", "Low Cut", kLowCutOff, 1000, kLowCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "high_cut", "High Cut", "High Cut", 1000, kHighCutOff, kHighCutOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. Voicings: 0 clean 1 grit 12 2 hi-fi 12 3 dark 12 4 companded 8 5 toy 6 phone; order 0 after 1 before.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    F("Gritty 12-bit", { { "mode", 1 }, { "bits", 12 }, { "rate", 26040 }, { "drive", 4 } });
    F("Punchy 12-bit", { { "mode", 2 }, { "bits", 12 }, { "rate", 40000 }, { "drive", 3 } });
    F("Dark resonant 12", { { "mode", 3 }, { "bits", 12 }, { "rate", 30000 } });
    F("Companded 8-bit", { { "mode", 4 }, { "bits", 8 }, { "rate", 25000 } });
    F("Toy keyboard", { { "mode", 5 }, { "bits", 8 }, { "rate", 11025 }, { "jitter", 10 } });
    F("Telephone", { { "mode", 6 }, { "bits", 8 }, { "rate", 8000 }, { "drive", 6 } });
    F("Pure bitcrush", { { "mode", 0 }, { "bits", 6 }, { "rate", kRateOff } });
    F("Pure downsample", { { "mode", 0 }, { "bits", kBitsOff }, { "rate", 4000 } });
    F("Worn cassette", { { "mode", 0 }, { "bits", kBitsOff }, { "rate", kRateOff }, { "wow", 35 }, { "flutter", 40 }, { "hiss", 40 }, { "dropout", 25 }, { "age", 45 } });
    F("Dusty record", { { "mode", 0 }, { "bits", kBitsOff }, { "rate", kRateOff }, { "wow", 15 }, { "crackle", 55 }, { "hiss", 15 }, { "age", 35 } });
    F("Sampled from vinyl", { { "mode", 1 }, { "order", 1 }, { "crackle", 45 }, { "wow", 12 }, { "age", 30 }, { "drive", 3 } });
    F("Tape copy of a sampler", { { "mode", 2 }, { "order", 0 }, { "wow", 25 }, { "flutter", 25 }, { "hiss", 30 }, { "age", 30 } });
    S("Drums: 12-bit knock", { { "mode", 1 }, { "drive", 8 }, { "bits", 12 }, { "rate", 26040 }, { "low_cut", 30 } });
    S("Drums: crunchy loop", { { "mode", 3 }, { "drive", 6 }, { "bits", 10 }, { "rate", 22050 }, { "mix", 70 } });
    S("Drums: parallel dirt", { { "mode", 5 }, { "drive", 10 }, { "bits", 6 }, { "rate", 9000 }, { "mix", 30 } });
    S("Bass: soft 12-bit", { { "mode", 2 }, { "bits", 12 }, { "rate", 32000 }, { "drive", 4 }, { "high_cut", 9000 } });
    S("Keys: lo-fi piano", { { "mode", 3 }, { "bits", 12 }, { "rate", 26040 }, { "wow", 20 }, { "hiss", 20 }, { "age", 25 } });
    S("Keys: warbly tape", { { "mode", 0 }, { "bits", kBitsOff }, { "rate", kRateOff }, { "wow", 55 }, { "flutter", 30 }, { "age", 40 } });
    S("Vocals: AM radio", { { "mode", 6 }, { "bits", 10 }, { "rate", 11025 }, { "drive", 8 }, { "hiss", 25 } });
    S("Pads: old sampler", { { "mode", 3 }, { "bits", 12 }, { "rate", 22050 }, { "wow", 15 }, { "age", 20 } });
    S("Bus: lo-fi glue", { { "mode", 2 }, { "bits", 14 }, { "rate", 36000 }, { "drive", 3 }, { "hiss", 10 }, { "mix", 60 } });
    C("Broken walkman", { { "mode", 0 }, { "bits", kBitsOff }, { "rate", kRateOff }, { "wow", 90 }, { "flutter", 70 }, { "hiss", 55 }, { "dropout", 70 }, { "age", 70 } });
    C("Game console", { { "mode", 5 }, { "bits", 4 }, { "rate", 7000 }, { "jitter", 5 } });
    C("Digital decay", { { "mode", 0 }, { "bits", 3 }, { "rate", 2500 }, { "jitter", 60 } });
    C("Radio from the next room", { { "mode", 6 }, { "bits", 8 }, { "rate", 6000 }, { "crackle", 30 }, { "hiss", 45 }, { "age", 60 }, { "high_cut", 2500 } });
    C("Memory of a song", { { "mode", 3 }, { "bits", 10 }, { "rate", 16000 }, { "order", 1 }, { "wow", 45 }, { "crackle", 35 }, { "hiss", 30 }, { "dropout", 30 }, { "age", 65 } });
    return v;
    }();
    return v;
}

}} // namespace pump::crush
