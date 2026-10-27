// pump PHASE: parameters and factory voices (shared by the DSP and the UI).
//
// Six engines, each the way its hardware did it (with CHIP on: the chip's integer sine and exponent tables, its
// envelope steps, its sample rate and its DAC; CHIP off: the same patch in clean floating point):
//   Console 4-op   the 4-operator chip of a 16-bit home console: eight algorithms, sine only, feedback on op 1,
//                  a 9-bit DAC with its crossover step (the gritty low end)
//   Arcade 4-op    the arcade boards' 4-operator chip: the same algorithms, a floating-point DAC, a noise
//                  generator on op 4
//   PC card 2-op   the 2-operator chip of PC sound cards: FM or additive, eight waveforms (sine, half, abs,
//                  quarter, alternating, camel, square, log saw)
//   PC card 4-op   the same chip's 4-operator mode: four algorithms
//   Studio         2 to 6 operators: eighteen algorithms (pairs, stacks, Y's, branches, organ), every waveform on
//                  every operator; a 12-bit floating DAC
//   Phase dist.    phase distortion: a cosine read through a bent phase (saw, square, pulse, double sine, saw-pulse
//                  and three resonant windows), two waves per line alternating, a DCW envelope that bends it, two
//                  lines (1, 2, 1+1', 1+2'), ring or noise modulation
// Every engine: fixed-frequency operators (FIXED: 1 / 10 / 100 / 1000 Hz times FINE), a pitch envelope, the LFO to
// level (per operator) and pitch, poly / mono / legato with glide.
#pragma once

#include "PumpParams.hpp"
#include <vector>
#include <string>

namespace pump { namespace phase {

enum EngineType : int { kConsole = 0, kArcade, kPc2, kPc4, kSix, kPd, kNumEngines };
static const char* const kEngineNames[] = { "Console 4-op", "Arcade 4-op", "PC card 2-op", "PC card 4-op", "Studio", "Phase distortion" };
static const char* const kEngineKeys[] = { "CONSOLE", "ARCADE", "PC 2-OP", "PC 4-OP", "STUDIO", "PHASE DIST" };
static const int kEngineOps[kNumEngines] = { 4, 4, 2, 4, 6, 2 };
static const int kEngineAlgos[kNumEngines] = { 8, 8, 2, 4, 18, 4 };
static const char* const kWaveNames[] = { "Sine", "Half sine", "Abs sine", "Quarter sine", "Alternating", "Camel", "Square", "Log saw" };
static const char* const kWaveKeys[] = { "SINE", "HALF", "ABS", "QUARTER", "ALT", "CAMEL", "SQUARE", "LOG SAW" };
enum PdWave : int { kPdSaw = 0, kPdSquare, kPdPulse, kPdDouble, kPdSawPulse, kPdResSaw, kPdResTri, kPdResTrap, kNumPdWaves };
static const char* const kPdWaveNames[] = { "Saw", "Square", "Pulse", "Double sine", "Saw-pulse", "Resonance saw", "Resonance triangle", "Resonance trapezoid" };
static const char* const kPdWaveBNames[] = { "None", "Saw", "Square", "Pulse", "Double sine", "Saw-pulse", "Resonance saw", "Resonance triangle", "Resonance trapezoid" };
static const char* const kPdLineNames[] = { "1", "2", "1+1'", "1+2'" };
static const char* const kPdModNames[] = { "Off", "Ring", "Noise" };
static const char* const kVoiceModeNames[] = { "Poly", "Mono", "Legato" };
static const char* const kOnOff[] = { "Off", "On" };

// Global parameters, then six operators (kOpFields each), then phase distortion. Symbols never change; append only.
enum Param : int {
    kEngine = 0, kAlgo, kFeedback, kChip, kTone, kLfoRate, kLfoAm, kLfoPm, kWheelVib,
    kVoices, kVoiceMode, kGlide, kBend, kTranspose, kVolume, kPitchEnv, kPitchDecay,
    kOpBase
};
enum OpField : int { kOpWave = 0, kOpRatio, kOpFine, kOpDetune, kOpLevel, kOpA, kOpD, kOpS, kOpR, kOpVel, kOpAm, kOpFixed, kOpFields };
static constexpr int kOps = 6;
constexpr int op(int k, int f) { return kOpBase + k * kOpFields + f; }
enum PdParam : int {
    kPdWave1 = kOpBase + kOps * kOpFields, kPdWave1b, kPdWave2, kPdWave2b, kPdDcw1, kPdDcw2, kPdKey,
    kPdA, kPdD, kPdS, kPdR, kPdDetune, kPdOctave, kPdMod,
    kNumParams
};

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    static std::vector<std::string> names;   // keeps the operators' names alive
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean;
    v.push_back({ "engine", "Engine", "Engine", 0, kNumEngines - 1, kConsole, PUnit::None, PScale::Linear, I, kEngineNames, 1 });
    v.push_back({ "algorithm", "Algorithm", "Algo", 0, 17, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "feedback", "Feedback", "FB", 0, 7, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "chip", "Chip Emulation", "Chip", 0, 1, 1, PUnit::None, PScale::Linear, B, kOnOff, 1 });
    v.push_back({ "tone", "Tone", "Tone", 0, 100, 100, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_rate", "LFO Rate", "Rate", 0.1f, 40, 5, PUnit::Hz, PScale::Log, A, nullptr, 1 });
    v.push_back({ "lfo_am", "LFO to Level", "AM", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "lfo_pm", "LFO to Pitch", "PM", 0, 100, 0, PUnit::Percent, PScale::Power, A, nullptr, 2 });
    v.push_back({ "wheel_vib", "Wheel Vibrato", "Wheel", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "voices", "Voices", "Voices", 1, 16, 8, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "voice_mode", "Voice Mode", "Mode", 0, 2, 0, PUnit::None, PScale::Linear, I, kVoiceModeNames, 1 });
    v.push_back({ "glide", "Glide", "Glide", 0, 2000, 0, PUnit::ms, PScale::Power, A, nullptr, 2 });
    v.push_back({ "bend", "Pitch Bend Range", "Bend", 0, 24, 2, PUnit::None, PScale::Linear, kPInteger, nullptr, 1 });
    v.push_back({ "transpose", "Transpose", "Trans", -24, 24, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 6, -6, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pitch_env", "Pitch Envelope", "P-Env", -48, 48, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pitch_decay", "Pitch Envelope Decay", "P-Dec", 1, 5000, 80, PUnit::ms, PScale::Log, A, nullptr, 1 });
    names.reserve(kOps * kOpFields * 2);
    for (int k = 0; k < kOps; ++k)
    {
        auto nm = [&](const char* sym, const char* full) -> std::pair<const char*, const char*> {
            names.push_back("op" + std::to_string(k + 1) + "_" + sym);
            const char* s = names.back().c_str();
            names.push_back("Op " + std::to_string(k + 1) + " " + full);
            return { s, names.back().c_str() };
        };
        const bool car = k == 0;   // the default patch: op 1 modulates op 2
        auto p = nm("wave", "Wave");     v.push_back({ p.first, p.second, "Wave", 0, 7, 0, PUnit::None, PScale::Linear, I, kWaveNames, 1 });
        p = nm("ratio", "Ratio");        v.push_back({ p.first, p.second, "Ratio", 0, 31, 1, PUnit::None, PScale::Linear, I, nullptr, 1 });
        p = nm("fine", "Fine");          v.push_back({ p.first, p.second, "Fine", 0, 99, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
        p = nm("detune", "Detune");      v.push_back({ p.first, p.second, "Detune", -7, 7, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
        p = nm("level", "Level");        v.push_back({ p.first, p.second, "Level", 0, 99, car ? 72.0f : 99.0f, PUnit::None, PScale::Linear, A, nullptr, 1 });
        p = nm("attack", "Attack");      v.push_back({ p.first, p.second, "A", 0.5f, 20000, 1, PUnit::ms, PScale::Log, A, nullptr, 1 });
        p = nm("decay", "Decay");        v.push_back({ p.first, p.second, "D", 1, 20000, car ? 900.0f : 2500.0f, PUnit::ms, PScale::Log, A, nullptr, 1 });
        p = nm("sustain", "Sustain");    v.push_back({ p.first, p.second, "S", 0, 100, car ? 40.0f : 70.0f, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        p = nm("release", "Release");    v.push_back({ p.first, p.second, "R", 1, 20000, 300, PUnit::ms, PScale::Log, A, nullptr, 1 });
        p = nm("velocity", "Velocity");  v.push_back({ p.first, p.second, "Vel", 0, 100, car ? 60.0f : 30.0f, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        p = nm("am", "LFO Level");       v.push_back({ p.first, p.second, "AM", 0, 1, 0, PUnit::None, PScale::Linear, B, kOnOff, 1 });
        p = nm("fixed", "Fixed Frequency"); v.push_back({ p.first, p.second, "Fixed", 0, 1, 0, PUnit::None, PScale::Linear, B, kOnOff, 1 });
    }
    v.push_back({ "pd_wave1", "PD Line 1 Wave", "Wave 1", 0, kNumPdWaves - 1, kPdSaw, PUnit::None, PScale::Linear, I, kPdWaveNames, 1 });
    v.push_back({ "pd_wave1b", "PD Line 1 Second Wave", "Wave 1b", 0, kNumPdWaves, 0, PUnit::None, PScale::Linear, I, kPdWaveBNames, 1 });
    v.push_back({ "pd_wave2", "PD Line 2 Wave", "Wave 2", 0, kNumPdWaves - 1, kPdSquare, PUnit::None, PScale::Linear, I, kPdWaveNames, 1 });
    v.push_back({ "pd_wave2b", "PD Line 2 Second Wave", "Wave 2b", 0, kNumPdWaves, 0, PUnit::None, PScale::Linear, I, kPdWaveBNames, 1 });
    v.push_back({ "pd_dcw1", "PD Line 1 Bend", "DCW 1", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pd_dcw2", "PD Line 2 Bend", "DCW 2", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pd_key", "PD Bend Key Follow", "Key", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pd_attack", "PD Bend Attack", "A", 0.5f, 20000, 5, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "pd_decay", "PD Bend Decay", "D", 1, 20000, 800, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "pd_sustain", "PD Bend Sustain", "S", 0, 100, 30, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pd_release", "PD Bend Release", "R", 1, 20000, 300, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "pd_detune", "PD Line 2 Detune", "Detune", -50, 50, 6, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pd_octave", "PD Line 2 Octave", "Oct", -2, 2, 0, PUnit::None, PScale::Linear, I, nullptr, 1 });
    v.push_back({ "pd_mod", "PD Line 2 Modulation", "Mod", 0, 2, 0, PUnit::None, PScale::Linear, I, kPdModNames, 1 });
    return v;
}

// Factory voices. A voice is a list of (symbol, value). The helpers below keep them readable:
//   o(k, ratio, level, a, d, s, r)  operator k (1-based) with its envelope (ms, ms, %, ms)
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    using PV = std::vector<PresetValue>;
    static std::vector<std::string> keep;
    keep.reserve(4096);
    auto sym = [&](int k, const char* f) { keep.push_back("op" + std::to_string(k) + "_" + f); return keep.back().c_str(); };
    auto o = [&](PV& p, int k, float ratio, float level, float a, float d, float s, float r, float vel = -1, float fine = -1, float wave = -1) {
        p.push_back({ sym(k, "ratio"), ratio }); p.push_back({ sym(k, "level"), level });
        p.push_back({ sym(k, "attack"), a }); p.push_back({ sym(k, "decay"), d }); p.push_back({ sym(k, "sustain"), s }); p.push_back({ sym(k, "release"), r });
        if (vel >= 0) p.push_back({ sym(k, "velocity"), vel });
        if (fine >= 0) p.push_back({ sym(k, "fine"), fine });
        if (wave >= 0) p.push_back({ sym(k, "wave"), wave });
    };
    auto F = [&](const char* sec, const char* n, PV p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    { PV p { { "engine", kConsole }, { "algorithm", 0 }, { "feedback", 5 } };   // slap bass: a stack, op 1 rings with feedback
      o(p, 1, 0, 80, 1, 300, 0, 80); o(p, 2, 1, 76, 1, 180, 10, 80); o(p, 3, 1, 74, 1, 500, 20, 80); o(p, 4, 1, 99, 1, 1200, 40, 60, 40);
      p.push_back({ "transpose", -12 }); p.push_back({ "voice_mode", 1 }); p.push_back({ "volume", -2 }); F("CONSOLE", "Slap bass", p); }
    { PV p { { "engine", kConsole }, { "algorithm", 4 }, { "feedback", 3 } };   // two pairs, bright attack
      o(p, 1, 1, 82, 1, 900, 30, 300, 70); o(p, 2, 1, 99, 1, 2500, 60, 300, 40); o(p, 3, 14, 60, 1, 250, 0, 200, 80); o(p, 4, 1, 92, 1, 2000, 40, 400, 40);
      F("CONSOLE", "Console e-piano", p); }
    { PV p { { "engine", kConsole }, { "algorithm", 3 }, { "feedback", 6 } };
      o(p, 1, 1, 78, 60, 400, 85, 150); o(p, 2, 1, 80, 40, 400, 80, 150); o(p, 3, 1, 70, 40, 400, 80, 150); o(p, 4, 1, 99, 30, 400, 90, 180);
      p.push_back({ "lfo_pm", 15 }); p.push_back({ "lfo_rate", 5.5f }); F("CONSOLE", "Console brass", p); }
    { PV p { { "engine", kConsole }, { "algorithm", 6 }, { "feedback", 6 } };
      o(p, 1, 2, 84, 1, 300, 70, 100); o(p, 2, 1, 99, 1, 300, 90, 100); o(p, 3, 1, 90, 1, 300, 90, 100, 30, 1); o(p, 4, 2, 80, 1, 300, 90, 100);
      p.push_back({ "voice_mode", 2 }); p.push_back({ "glide", 40 }); p.push_back({ "volume", -2 }); F("CONSOLE", "Fighting game lead", p); }
    { PV p { { "engine", kConsole }, { "algorithm", 0 }, { "feedback", 7 } };
      o(p, 1, 1, 90, 1, 60, 0, 40); o(p, 2, 3, 85, 1, 90, 0, 40); o(p, 3, 1, 80, 1, 120, 0, 40); o(p, 4, 1, 99, 1, 180, 0, 60);
      p.push_back({ "transpose", -12 }); F("CONSOLE", "Console drum hit", p); }
    { PV p { { "engine", kArcade }, { "algorithm", 4 }, { "feedback", 4 } };
      o(p, 1, 1, 80, 30, 500, 75, 200); o(p, 2, 1, 99, 20, 500, 85, 200); o(p, 3, 2, 72, 30, 500, 75, 200); o(p, 4, 1, 94, 20, 500, 85, 200, 30, 3);
      p.push_back({ "lfo_pm", 12 }); F("ARCADE", "Arcade brass", p); }
    { PV p { { "engine", kArcade }, { "algorithm", 5 }, { "feedback", 5 } };
      o(p, 1, 1, 82, 1, 800, 40, 300); o(p, 2, 1, 96, 1, 800, 80, 300); o(p, 3, 2, 90, 1, 800, 80, 300, 30, 2); o(p, 4, 4, 84, 1, 800, 80, 300);
      F("ARCADE", "Arcade organ lead", p); }
    { PV p { { "engine", kArcade }, { "algorithm", 7 }, { "feedback", 0 } };
      o(p, 1, 1, 0, 1, 1, 0, 1); o(p, 2, 1, 0, 1, 1, 0, 1); o(p, 3, 1, 86, 1, 120, 0, 60); o(p, 4, 1, 92, 1, 250, 0, 120, 20, -1, 1);
      F("ARCADE", "Arcade noise snare", p); }
    { PV p { { "engine", kPc2 }, { "algorithm", 0 }, { "feedback", 3 } };
      o(p, 1, 1, 76, 1, 600, 40, 200, 40, -1, 1); o(p, 2, 1, 99, 1, 1500, 60, 300, 40);
      p.push_back({ "volume", -2 }); F("PC CARD", "Sound card piano", p); }
    { PV p { { "engine", kPc2 }, { "algorithm", 1 }, { "feedback", 4 } };
      o(p, 1, 2, 90, 5, 300, 100, 60, 0); o(p, 2, 1, 95, 5, 300, 100, 60, 0, -1, 0);
      p.push_back({ "lfo_am", 20 }); p.push_back({ "op1_am", 1 }); F("PC CARD", "Sound card organ", p); }
    { PV p { { "engine", kPc2 }, { "algorithm", 0 }, { "feedback", 0 } };
      o(p, 1, 1, 84, 1, 300, 60, 100, 30, -1, 6); o(p, 2, 1, 99, 1, 300, 90, 100, 30, -1, 0);
      p.push_back({ "voice_mode", 2 }); p.push_back({ "volume", -2 }); F("PC CARD", "Square lead", p); }
    { PV p { { "engine", kPc2 }, { "algorithm", 0 }, { "feedback", 6 } };
      o(p, 1, 0, 80, 1, 400, 20, 100, 30, -1, 2); o(p, 2, 1, 99, 1, 800, 50, 120, 30, -1, 0);
      p.push_back({ "transpose", -12 }); F("PC CARD", "Sound card bass", p); }
    { PV p { { "engine", kPc4 }, { "algorithm", 1 }, { "feedback", 2 } };
      o(p, 1, 1, 70, 80, 600, 80, 400); o(p, 2, 1, 76, 120, 800, 70, 400, -1, -1, 1); o(p, 3, 1, 70, 80, 800, 70, 400); o(p, 4, 1, 99, 80, 800, 90, 500);
      p.push_back({ "lfo_pm", 10 }); F("PC CARD", "Four-op flute", p); }
    { PV p { { "engine", kSix }, { "algorithm", 10 }, { "feedback", 4 } };   // three pairs
      o(p, 1, 1, 99, 1, 3200, 0, 600, 50); o(p, 2, 1, 70, 1, 1800, 0, 600, 80); o(p, 3, 1, 94, 1, 3200, 0, 600, 50, 2);
      o(p, 4, 14, 62, 1, 400, 0, 300, 90); o(p, 5, 1, 90, 1, 2200, 0, 600, 40); o(p, 6, 1, 64, 1, 900, 0, 400, 70);
      F("STUDIO", "Tine e-piano", p); }
    { PV p { { "engine", kSix }, { "algorithm", 14 }, { "feedback", 0 } };   // one modulator into five carriers
      o(p, 1, 1, 90, 1, 4000, 0, 2000); o(p, 2, 3, 82, 1, 3000, 0, 2000, -1, 50); o(p, 3, 5, 76, 1, 2000, 0, 2000, -1, 41);
      o(p, 4, 7, 70, 1, 1600, 0, 1500); o(p, 5, 9, 64, 1, 1200, 0, 1200); o(p, 6, 3, 80, 1, 2500, 0, 1500, -1, 50);
      p.push_back({ "volume", 3 }); F("STUDIO", "Glass bells", p); }
    { PV p { { "engine", kSix }, { "algorithm", 11 }, { "feedback", 5 } };   // two stacks of three
      o(p, 1, 1, 99, 1, 500, 0, 200, 60); o(p, 2, 3, 80, 1, 250, 0, 150, 80); o(p, 3, 4, 60, 1, 120, 0, 100, 80);
      o(p, 4, 1, 90, 1, 400, 0, 200, 60); o(p, 5, 10, 60, 1, 100, 0, 80, 80); o(p, 6, 1, 50, 1, 80, 0, 60);
      F("STUDIO", "Marimba", p); }
    { PV p { { "engine", kSix }, { "algorithm", 9 }, { "feedback", 6 } };   // a pair and a stack of four
      o(p, 1, 1, 99, 1, 600, 40, 120); o(p, 2, 1, 80, 1, 200, 20, 100); o(p, 3, 1, 96, 1, 600, 50, 120);
      o(p, 4, 1, 84, 1, 300, 30, 100); o(p, 5, 1, 76, 1, 300, 30, 100); o(p, 6, 1, 80, 1, 200, 20, 80);
      p.push_back({ "transpose", -12 }); p.push_back({ "voice_mode", 1 }); F("STUDIO", "Solid bass", p); }
    { PV p { { "engine", kSix }, { "algorithm", 10 }, { "feedback", 3 }, { "lfo_pm", 8 }, { "lfo_rate", 0.6f } };
      o(p, 1, 1, 90, 900, 2000, 80, 1500); o(p, 2, 1, 70, 1200, 2000, 70, 1500, -1, 1); o(p, 3, 2, 86, 900, 2000, 80, 1500, -1, 1);
      o(p, 4, 1, 68, 1500, 2000, 70, 1500); o(p, 5, 1, 88, 900, 2000, 80, 1500, -1, 2); o(p, 6, 3, 60, 1500, 2000, 70, 1500);
      F("STUDIO", "Warm pad", p); }
    { PV p { { "engine", kSix }, { "algorithm", 0 }, { "feedback", 2 } };   // the plainest FM: one pair
      o(p, 1, 1, 99, 1, 1500, 60, 300, 40); o(p, 2, 2, 78, 1, 600, 30, 300, 70);
      F("STUDIO", "Two-op pluck", p); }
    { PV p { { "engine", kSix }, { "algorithm", 3 }, { "feedback", 0 } };   // a Y: two modulators at odd ratios
      o(p, 1, 1, 99, 1, 2500, 0, 800, 40); o(p, 2, 3, 70, 1, 1200, 0, 600, 70, 50); o(p, 3, 7, 62, 1, 600, 0, 400, 80);
      F("STUDIO", "Three-op clav", p); }
    { PV p { { "engine", kSix }, { "algorithm", 7 }, { "feedback", 4 }, { "pitch_env", 24 }, { "pitch_decay", 40 } };
      o(p, 1, 1, 99, 1, 400, 0, 200, 50); o(p, 2, 1, 80, 1, 90, 0, 60, 80); o(p, 3, 2, 76, 1, 150, 0, 80, 80, 37); o(p, 4, 3, 70, 1, 60, 0, 60, 80, 20);
      p.push_back({ "op2_fixed", 1 }); p.push_back({ "op3_fixed", 1 }); p.push_back({ "op4_fixed", 1 }); p.push_back({ "transpose", -12 });
      p.push_back({ "volume", -2 }); F("STUDIO", "Fixed-frequency tom", p); }
    { PV p { { "engine", kPd }, { "algorithm", 0 }, { "pd_wave1", kPdSaw }, { "pd_dcw1", 85 }, { "pd_attack", 1 }, { "pd_decay", 350 }, { "pd_sustain", 10 }, { "pd_key", 20 } };
      o(p, 1, 1, 99, 1, 2000, 90, 80); p.push_back({ "transpose", -12 }); p.push_back({ "voice_mode", 1 }); p.push_back({ "volume", -2 }); F("PHASE DIST", "Phase saw bass", p); }
    { PV p { { "engine", kPd }, { "algorithm", 0 }, { "pd_wave1", kPdResSaw }, { "pd_dcw1", 90 }, { "pd_attack", 600 }, { "pd_decay", 2500 }, { "pd_sustain", 20 } };
      o(p, 1, 1, 99, 5, 3000, 80, 600); F("PHASE DIST", "Resonant sweep", p); }
    { PV p { { "engine", kPd }, { "algorithm", 3 }, { "pd_wave1", kPdSaw }, { "pd_wave2", kPdSaw }, { "pd_dcw1", 55 }, { "pd_dcw2", 45 }, { "pd_detune", 9 }, { "pd_attack", 400 }, { "pd_sustain", 60 } };
      o(p, 1, 1, 92, 300, 2000, 90, 800); o(p, 2, 1, 92, 300, 2000, 90, 800); F("PHASE DIST", "Phase strings", p); }
    { PV p { { "engine", kPd }, { "algorithm", 0 }, { "pd_wave1", kPdPulse }, { "pd_wave1b", 2 }, { "pd_dcw1", 75 }, { "pd_sustain", 50 } };
      o(p, 1, 1, 99, 1, 1000, 85, 200); p.push_back({ "voice_mode", 2 }); p.push_back({ "glide", 60 }); F("PHASE DIST", "Pulse lead", p); }
    { PV p { { "engine", kPd }, { "algorithm", 3 }, { "pd_wave1", kPdDouble }, { "pd_wave2", kPdResTrap }, { "pd_dcw1", 40 }, { "pd_dcw2", 80 }, { "pd_octave", 1 }, { "pd_mod", 1 }, { "pd_decay", 1200 } };
      o(p, 1, 1, 90, 1, 1800, 30, 500); o(p, 2, 1, 80, 1, 1200, 20, 500); p.push_back({ "volume", 3 }); F("PHASE DIST", "Ring bell", p); }
    { PV p { { "engine", kPd }, { "algorithm", 2 }, { "pd_wave1", kPdSquare }, { "pd_dcw1", 60 }, { "pd_detune", 4 }, { "pd_attack", 20 }, { "pd_sustain", 70 }, { "chip", 0 } };
      o(p, 1, 1, 90, 10, 1000, 95, 150); F("PHASE DIST", "Clean organ", p); }
    return v;
}

}} // namespace pump::phase
