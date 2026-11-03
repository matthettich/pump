// pump STRIP: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <vector>

namespace pump { namespace strip {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
enum Param : int {
    kModel = 0, kInput, kColor, kPhase, kHpf, kLpf,
    kEqOn, kLfGain, kLfFreq, kLfBell, kLmfGain, kLmfFreq, kLmfQ, kHmfGain, kHmfFreq, kHmfQ, kHfGain, kHfFreq, kHfBell,
    kGateOn, kGateThresh, kGateRange, kGateRelease, kGateMode,
    kCompOn, kCompThresh, kCompRatio, kCompAttack, kCompRelease, kCompAuto, kCompMakeup, kCompMix, kScHpf,
    kOrder, kOutGain, kAutoGain, kOversampling, kBypass,
    kNumParams
};

// The consoles. Each is a whole channel: its preamp (harmonics, transformer), filter slopes, EQ curves, dynamics
// and a small fixed voicing (coupling, bandwidth). COLOR scales everything that isn't a control you turned.
enum Model : int { kClean = 0, kClassA, kConsole79, kConsole87, kDiscrete, kValve, kStudio, kBroadcast, kNumModels };
static const char* const kModelNames[] = { "Clean", "Class A '73", "Console '79", "Console '87", "Discrete '70",
                                           "Valve '60", "Studio '68", "Broadcast '55" };
static const char* const kModelKeys[] = { "CLEAN", "CLASS A", "CONSOLE 79", "CONSOLE 87", "DISCRETE", "VALVE", "STUDIO 68", "BROADCAST" };
// one line each, for the window and the status line
static const char* const kModelAbout[] = {
    "Modern and transparent: no colour, constant-Q EQ, clean feed-forward compressor",
    "British class-A module: transformer-coupled discrete preamp (2nd + 3rd), inductor EQ, diode-bridge compressor",
    "Large-format console, first series: op-amp headroom, constant-Q EQ, fast VCA dynamics",
    "Large-format console, second series: proportional-Q EQ with shelf dips, punchier hard-knee VCA",
    "American discrete op-amp channel: proportional-Q EQ, output transformer, feedback compressor with thrust",
    "Valve strip: triode preamp, passive EQ (boost-and-cut lows), optical leveler",
    "London studio desk: crunchy transistor stages, wide shelves and presence, aggressive limiter",
    "Broadcast valve chain: heavy warm preamp, gentle wide EQ, variable-mu compressor",
};

enum GateMode : int { kExpand = 0, kGate };
static const char* const kGateModeNames[] = { "Expand", "Gate" };
enum Order : int { kEqThenDyn = 0, kDynThenEq };
static const char* const kOrderNames[] = { "EQ > Dyn", "Dyn > EQ" };
static const char* const kOsNames[] = { "1x", "2x", "4x" };

// Filter end stops mean "off".
constexpr float kHpfOff = 10.0f, kLpfOff = 22000.0f, kScOff = 10.0f;
inline bool hpfActive(double f) { return f > kHpfOff + 0.05; }
inline bool lpfActive(double f) { return f < kLpfOff - 10.0; }
inline bool scActive(double f) { return f > kScOff + 0.05; }

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    // the model and oversampling change the whole signal path (and the latency): not automatable
    v.push_back({ "model", "Model", "Model", 0, kNumModels - 1, kClassA, PUnit::None, PScale::Linear, kPInteger, kModelNames, 1 });
    v.push_back({ "input", "Input", "Input", -12, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "color", "Colour", "Colour", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "phase", "Phase Invert", "Phase", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "hpf", "High-Pass", "HPF", kHpfOff, 800, kHpfOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lpf", "Low-Pass", "LPF", 1000, kLpfOff, kLpfOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });

    v.push_back({ "eq_on", "EQ On", "EQ", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "lf_gain", "LF Gain", "LF", -16, 16, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lf_freq", "LF Freq", "LF Freq", 20, 800, 100, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lf_bell", "LF Bell", "LF Bell", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "lmf_gain", "LMF Gain", "LMF", -16, 16, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lmf_freq", "LMF Freq", "LMF Freq", 60, 3000, 400, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lmf_q", "LMF Q", "LMF Q", 0.3f, 5, 1, PUnit::None, PScale::Log, A, nullptr, 1 });
    v.push_back({ "hmf_gain", "HMF Gain", "HMF", -16, 16, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "hmf_freq", "HMF Freq", "HMF Freq", 500, 14000, 2500, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "hmf_q", "HMF Q", "HMF Q", 0.3f, 5, 1, PUnit::None, PScale::Log, A, nullptr, 1 });
    v.push_back({ "hf_gain", "HF Gain", "HF", -16, 16, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "hf_freq", "HF Freq", "HF Freq", 1500, 20000, 10000, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "hf_bell", "HF Bell", "HF Bell", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });

    v.push_back({ "gate_on", "Gate On", "Gate", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "gate_thresh", "Gate Threshold", "Threshold", -80, 0, -50, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "gate_range", "Gate Range", "Range", 0, 80, 20, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "gate_release", "Gate Release", "Release", 5, 2000, 150, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "gate_mode", "Gate Mode", "Mode", 0, 1, kExpand, PUnit::None, PScale::Linear, A | kPInteger, kGateModeNames, 1 });

    v.push_back({ "comp_on", "Compressor On", "Comp", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "comp_thresh", "Comp Threshold", "Threshold", -50, 0, -18, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "comp_ratio", "Comp Ratio", "Ratio", 1, 20, 4, PUnit::Ratio, PScale::Power, A, nullptr, 2.2f });
    v.push_back({ "comp_attack", "Comp Attack", "Attack", 0.1f, 100, 10, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "comp_release", "Comp Release", "Release", 20, 2000, 200, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "comp_auto", "Comp Auto Release", "Auto", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "comp_makeup", "Comp Makeup", "Makeup", 0, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "comp_mix", "Comp Mix", "Mix", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "sc_hpf", "Sidechain High-Pass", "SC HPF", kScOff, 500, kScOff, PUnit::Hz, PScale::Log, A, nullptr, 1 });

    v.push_back({ "order", "Order", "Order", 0, 1, kEqThenDyn, PUnit::None, PScale::Linear, A | kPInteger, kOrderNames, 1 });
    v.push_back({ "out_gain", "Output", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "auto_gain", "Auto Gain", "Auto Gain", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "oversampling", "Oversampling", "Oversampling", 0, 2, 1, PUnit::None, PScale::Linear, kPInteger, kOsNames, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. Models: 0 clean 1 class A 2 console 79 3 console 87 4 discrete 5 valve 6 studio 68 7 broadcast.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SOURCES" }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CREATIVE" }); };
    F("Default", {});
    // one starting point per console: its sound with a little of everything it's known for
    F("Clean channel", { { "model", 0 }, { "hpf", 30 }, { "comp_on", 1 }, { "comp_thresh", -20 }, { "comp_ratio", 3 } });
    F("Class A: thick and forward", { { "model", 1 }, { "input", 6 }, { "hpf", 50 }, { "lf_gain", 3 }, { "lf_freq", 60 },
        { "hmf_gain", 3 }, { "hmf_freq", 3200 }, { "hf_gain", 2 }, { "hf_freq", 12000 } });
    F("Console 79: tight and clear", { { "model", 2 }, { "hpf", 80 }, { "lmf_gain", -3 }, { "lmf_freq", 350 }, { "lmf_q", 1.5f },
        { "hf_gain", 3 }, { "hf_freq", 10000 }, { "comp_on", 1 }, { "comp_thresh", -20 }, { "comp_ratio", 4 }, { "comp_attack", 3 }, { "comp_auto", 1 } });
    F("Console 87: punch", { { "model", 3 }, { "input", 4 }, { "lf_gain", 4 }, { "lf_freq", 80 }, { "hmf_gain", 4 }, { "hmf_freq", 4000 },
        { "comp_on", 1 }, { "comp_thresh", -22 }, { "comp_ratio", 4 }, { "comp_attack", 10 }, { "comp_release", 120 } });
    F("Discrete: drive and punch", { { "model", 4 }, { "input", 8 }, { "lf_gain", 4 }, { "lf_freq", 100 }, { "lmf_gain", -2 }, { "lmf_freq", 400 },
        { "hmf_gain", 4 }, { "hmf_freq", 5000 }, { "comp_on", 1 }, { "comp_thresh", -20 }, { "comp_ratio", 4 }, { "comp_attack", 5 } });
    F("Valve: boost and cut lows", { { "model", 5 }, { "lf_gain", 6 }, { "lf_freq", 60 }, { "hf_gain", 4 }, { "hf_freq", 10000 }, { "hf_bell", 1 },
        { "comp_on", 1 }, { "comp_thresh", -24 }, { "comp_ratio", 3 }, { "comp_attack", 10 }, { "comp_release", 500 } });
    F("Studio 68: crunch", { { "model", 6 }, { "input", 10 }, { "hmf_gain", 4 }, { "hmf_freq", 2800 }, { "hmf_q", 0.7f },
        { "comp_on", 1 }, { "comp_thresh", -24 }, { "comp_ratio", 10 }, { "comp_attack", 1 }, { "comp_release", 100 } });
    F("Broadcast: warm and level", { { "model", 7 }, { "input", 6 }, { "lf_gain", 2 }, { "lf_freq", 120 }, { "hf_gain", -2 }, { "hf_freq", 9000 },
        { "comp_on", 1 }, { "comp_thresh", -26 }, { "comp_ratio", 3 }, { "comp_attack", 20 }, { "comp_release", 600 } });
    F("Colour only (no EQ or dynamics)", { { "model", 1 }, { "input", 8 }, { "eq_on", 0 } });

    S("Vocals: pop up front", { { "model", 3 }, { "hpf", 90 }, { "lmf_gain", -2.5f }, { "lmf_freq", 300 }, { "lmf_q", 1.2f },
        { "hmf_gain", 3 }, { "hmf_freq", 3500 }, { "hf_gain", 3 }, { "hf_freq", 12000 }, { "order", 1 },
        { "comp_on", 1 }, { "comp_thresh", -24 }, { "comp_ratio", 4 }, { "comp_attack", 5 }, { "comp_release", 150 }, { "comp_auto", 1 } });
    S("Vocals: vintage leveled", { { "model", 5 }, { "input", 4 }, { "hpf", 80 }, { "hmf_gain", 2 }, { "hmf_freq", 3000 }, { "hf_gain", 3 }, { "hf_freq", 12000 }, { "hf_bell", 1 },
        { "comp_on", 1 }, { "comp_thresh", -28 }, { "comp_ratio", 3 }, { "comp_attack", 10 }, { "comp_release", 600 } });
    S("Vocals: rap presence", { { "model", 4 }, { "input", 6 }, { "hpf", 100 }, { "lmf_gain", -3 }, { "lmf_freq", 250 },
        { "hmf_gain", 4 }, { "hmf_freq", 2500 }, { "comp_on", 1 }, { "comp_thresh", -22 }, { "comp_ratio", 6 }, { "comp_attack", 2 }, { "comp_release", 80 } });
    S("Kick: knock", { { "model", 4 }, { "input", 6 }, { "lf_gain", 5 }, { "lf_freq", 60 }, { "lf_bell", 1 }, { "lmf_gain", -5 }, { "lmf_freq", 350 }, { "lmf_q", 1.5f },
        { "hmf_gain", 4 }, { "hmf_freq", 4000 }, { "comp_on", 1 }, { "comp_thresh", -16 }, { "comp_ratio", 4 }, { "comp_attack", 20 }, { "comp_release", 80 } });
    S("Snare: crack and gate", { { "model", 2 }, { "hpf", 90 }, { "lmf_gain", 3 }, { "lmf_freq", 200 }, { "hmf_gain", -3 }, { "hmf_freq", 900 },
        { "hf_gain", 4 }, { "hf_freq", 8000 }, { "gate_on", 1 }, { "gate_mode", 1 }, { "gate_thresh", -30 }, { "gate_range", 30 }, { "gate_release", 120 },
        { "comp_on", 1 }, { "comp_thresh", -18 }, { "comp_ratio", 4 }, { "comp_attack", 8 }, { "comp_release", 100 } });
    S("Toms: gated and full", { { "model", 1 }, { "input", 4 }, { "hpf", 50 }, { "lmf_gain", -4 }, { "lmf_freq", 400 }, { "hf_gain", 3 }, { "hf_freq", 5000 }, { "hf_bell", 1 },
        { "gate_on", 1 }, { "gate_mode", 1 }, { "gate_thresh", -32 }, { "gate_range", 40 }, { "gate_release", 300 } });
    S("Overheads: air", { { "model", 2 }, { "hpf", 150 }, { "lmf_gain", -2 }, { "lmf_freq", 500 }, { "hf_gain", 3 }, { "hf_freq", 12000 } });
    S("Drum room: smash", { { "model", 6 }, { "input", 12 }, { "lf_gain", 3 }, { "lf_freq", 100 }, { "comp_on", 1 }, { "comp_thresh", -36 }, { "comp_ratio", 20 },
        { "comp_attack", 0.5f }, { "comp_release", 60 }, { "comp_makeup", 8 }, { "comp_mix", 50 } });
    S("Bass: DI weight", { { "model", 1 }, { "input", 8 }, { "hpf", 30 }, { "lf_gain", 3 }, { "lf_freq", 60 }, { "lmf_gain", 2 }, { "lmf_freq", 700 },
        { "comp_on", 1 }, { "comp_thresh", -20 }, { "comp_ratio", 4 }, { "comp_attack", 15 }, { "comp_release", 200 } });
    S("Bass: valve round", { { "model", 5 }, { "input", 6 }, { "lf_gain", 5 }, { "lf_freq", 60 }, { "lpf", 6000 },
        { "comp_on", 1 }, { "comp_thresh", -24 }, { "comp_ratio", 4 }, { "comp_release", 400 } });
    S("Electric guitar: bite", { { "model", 3 }, { "hpf", 90 }, { "lpf", 9000 }, { "lmf_gain", -3 }, { "lmf_freq", 250 }, { "hmf_gain", 3 }, { "hmf_freq", 2200 } });
    S("Acoustic guitar: sparkle", { { "model", 1 }, { "hpf", 80 }, { "lmf_gain", -3 }, { "lmf_freq", 220 }, { "hf_gain", 3 }, { "hf_freq", 12000 },
        { "comp_on", 1 }, { "comp_thresh", -22 }, { "comp_ratio", 3 }, { "comp_attack", 10 } });
    S("Piano: open", { { "model", 5 }, { "hpf", 40 }, { "lmf_gain", -2 }, { "lmf_freq", 300 }, { "hf_gain", 2 }, { "hf_freq", 10000 } });
    S("Synth: warm it up", { { "model", 7 }, { "input", 8 }, { "lpf", 14000 }, { "lf_gain", 2 }, { "lf_freq", 150 } });
    S("Bus: glue", { { "model", 2 }, { "comp_on", 1 }, { "comp_thresh", -14 }, { "comp_ratio", 2 }, { "comp_attack", 30 }, { "comp_release", 100 }, { "comp_auto", 1 } });

    C("Lo-fi radio", { { "model", 7 }, { "input", 18 }, { "hpf", 400 }, { "lpf", 3500 }, { "hmf_gain", 6 }, { "hmf_freq", 1800 },
        { "comp_on", 1 }, { "comp_thresh", -30 }, { "comp_ratio", 8 } });
    C("Pumping drums", { { "model", 6 }, { "input", 6 }, { "comp_on", 1 }, { "comp_thresh", -32 }, { "comp_ratio", 20 }, { "comp_attack", 1 },
        { "comp_release", 250 }, { "comp_makeup", 10 } });
    C("Overdriven desk", { { "model", 4 }, { "input", 22 }, { "lf_gain", 3 }, { "lf_freq", 100 }, { "hmf_gain", 3 }, { "hmf_freq", 3000 } });
    C("Telephone", { { "model", 3 }, { "input", 10 }, { "hpf", 500 }, { "lpf", 3000 }, { "lmf_gain", 6 }, { "lmf_freq", 1200 }, { "lmf_q", 2 } });
    return v;
    }();
    return v;
}

}} // namespace pump::strip
