// pump STRIKE: parameters and factory kits (shared by the DSP and the UI).
//
// A drum synth of the kinds of drum synthesis pump's other drum machines don't have. Eight pads, each any of seven
// engines; TONE, CHAR A and CHAR B mean what suits each:
//   Modal     an object's modes (Bessel membrane, plate, bar, bell), struck: CHAR A the object, CHAR B the material
//             (wood to metal), TONE the mallet's hardness; BEND the skin's tension drop; NOISE the snare wires (noise
//             that rattles with the body)
//   String    a waveguide (a delay line round a filter, Karplus-Strong style), plucked or struck: CHAR A stiffness (a
//             dispersion that bends it toward a bell or a steel drum), CHAR B the excitation (a click to a noise
//             burst), TONE the brightness of the loop
//   Circuit   an analog bridged-T resonator pinged by a pulse (the classic analog kick, toms, congas, claves):
//             CHAR A the squeeze (the pitch rises with the level, as the circuit does), CHAR B a second resonator a
//             fifth-ish up (cowbell, clave, rim), TONE the pulse's brightness
//   FM        three operators: a carrier, a modulator at CHAR A's ratio, and a feedback operator (CHAR B); TONE the
//             index; the index dies faster than the level
//   West      a triangle folded (CHAR A how hard, CHAR B symmetry) into a low-pass gate struck by the hit (TONE the
//             gate's ring)
//   Additive  up to 48 inharmonic partials (cymbals, gongs, bells): CHAR A from a few bell-like partials to a dense
//             cymbal, CHAR B how differently they die (shimmer), TONE the tilt
//   Chaos     a chaotic (Rossler) system run at the pitch: CHAR A the chaos, CHAR B a band-pass's resonance, TONE its
//             centre
// BEND drops the pitch over BEND TIME; NOISE adds noise that follows the body (wires, sizzle, snap).
#pragma once

#include "PumpParams.hpp"
#include <string>
#include <vector>

namespace pump { namespace strike {

constexpr int kPads = 8;
enum EngineType : int { kModal = 0, kString, kCircuit, kFm, kWest, kAdditive, kChaos, kNumEngines };
static const char* const kEngineNames[] = { "Modal", "String", "Circuit", "FM", "West", "Additive", "Chaos" };
static const char* const kEngineKeys[] = { "MODAL", "STRING", "CIRCUIT", "FM", "WEST", "ADDITIVE", "CHAOS" };
enum PadField : int { kEngine = 0, kPitch, kDecay, kTone, kCharA, kCharB, kBend, kBendTime, kNoise, kLevel, kPan, kPerPad };
constexpr int pp(int pad, int field) { return pad * kPerPad + field; }
enum KitParam : int { kChoke = kPads * kPerPad, kVelocity, kDrive, kVolume, kFollow, kNumParams };

struct PadInfo { const char* sym; const char* name; const char* key; int note; int engine; float pitch, decay, tone, a, b, bend, bendT, noise, level, pan; };
static const PadInfo kPadInfo[kPads] = {
    { "kick",  "Kick",       "KICK",   36, kCircuit,  33, 520, 40, 35,  0, 14, 45,  0,  0,   0 },
    { "snare", "Snare",      "SNARE",  38, kModal,    55, 260, 60, 10, 40,  3, 30, 60, -2,   0 },
    { "rim",   "Rim",        "RIM",    37, kCircuit,  86,  60, 80,  0, 70,  0, 10,  0, -6,  20 },
    { "ltom",  "Low Tom",    "LO TOM", 43, kModal,    43, 420, 45,  5, 45,  5, 80,  0, -3, -25 },
    { "htom",  "High Tom",   "HI TOM", 48, kString,   55, 360, 55, 30, 40,  3, 40,  0, -4,  25 },
    { "chat",  "Closed Hat", "C-HAT",  42, kAdditive, 88,  60, 70, 90, 30,  0, 10, 30, -8, -15 },
    { "ohat",  "Open Hat",   "O-HAT",  46, kAdditive, 88, 500, 70, 90, 40,  0, 10, 30, -9, -15 },
    { "cym",   "Cymbal",     "CYMBAL", 49, kAdditive, 76, 2200, 55, 95, 70,  0, 10, 20, -11, 30 },
};

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    static std::vector<std::string> keep;
    keep.reserve(kPads * kPerPad * 2 + 8);
    auto s = [&](std::string x) { keep.push_back(std::move(x)); return keep.back().c_str(); };
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    for (int p = 0; p < kPads; ++p)
    {
        const PadInfo& d = kPadInfo[p];
        const std::string sy = d.sym, nm = d.name;
        v.push_back({ s(sy + "_engine"), s(nm + " Engine"), "Engine", 0, kNumEngines - 1, (float)d.engine, PUnit::None, PScale::Linear, I, kEngineNames, 1 });
        v.push_back({ s(sy + "_pitch"), s(nm + " Pitch"), "Pitch", 12, 108, d.pitch, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_decay"), s(nm + " Decay"), "Decay", 10, 6000, d.decay, PUnit::ms, PScale::Log, A, nullptr, 1 });
        v.push_back({ s(sy + "_tone"), s(nm + " Tone"), "Tone", 0, 100, d.tone, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_char_a"), s(nm + " Character A"), "Char A", 0, 100, d.a, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_char_b"), s(nm + " Character B"), "Char B", 0, 100, d.b, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_bend"), s(nm + " Bend"), "Bend", 0, 48, d.bend, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_bend_time"), s(nm + " Bend Time"), "B-Time", 2, 500, d.bendT, PUnit::ms, PScale::Log, A, nullptr, 1 });
        v.push_back({ s(sy + "_noise"), s(nm + " Noise"), "Noise", 0, 100, d.noise, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_level"), s(nm + " Level"), "Level", -36, 6, d.level, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_pan"), s(nm + " Pan"), "Pan", -100, 100, d.pan, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    }
    v.push_back({ "choke", "Hat Choke", "Choke", 0, 1, 1, PUnit::None, PScale::Linear, kPAutomatable | kPBoolean, nullptr, 1 });
    v.push_back({ "velocity", "Velocity", "Velocity", 0, 100, 70, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Drive", "Drive", 0, 100, 10, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "follow", "Edit the Pad Played", "Follow", 0, 1, 1, PUnit::None, PScale::Linear, kPBoolean | kPNoUndo, nullptr, 1 });   // appended
    return v;
    }();
    return v;
}

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    static std::vector<std::string> keep;
    keep.reserve(1024);
    static const char* const field[kPerPad] = { "engine", "pitch", "decay", "tone", "char_a", "char_b", "bend", "bend_time", "noise", "level", "pan" };
    struct C { int pad, f; float v; };
    auto K = [&](const char* sec, const char* n, std::vector<C> cs, std::vector<PresetValue> extra = {}) {
        std::vector<PresetValue> p = std::move(extra);
        for (const C& c : cs) { keep.push_back(std::string(kPadInfo[c.pad].sym) + "_" + field[c.f]); p.push_back({ keep.back().c_str(), c.v }); }
        v.push_back({ n, std::move(p), sec });
    };
    v.push_back({ "Default", {} });
    K("KITS", "Physical kit", { { 0, kEngine, kModal }, { 0, kPitch, 31 }, { 0, kCharA, 0 }, { 0, kCharB, 30 }, { 0, kBend, 7 }, { 0, kDecay, 450 },
                               { 2, kEngine, kModal }, { 2, kCharA, 60 }, { 2, kPitch, 79 }, { 2, kCharB, 20 }, { 4, kEngine, kModal }, { 4, kCharA, 5 }, { 4, kBend, 5 },
                               { 5, kEngine, kString }, { 5, kCharA, 90 }, { 5, kCharB, 100 }, { 5, kPitch, 96 }, { 6, kEngine, kString }, { 6, kCharA, 90 }, { 6, kCharB, 100 }, { 6, kPitch, 96 }, { 6, kDecay, 700 },
                               { 7, kEngine, kModal }, { 7, kCharA, 75 }, { 7, kCharB, 95 }, { 7, kPitch, 66 } });
    K("KITS", "Analog circuits", { { 1, kEngine, kCircuit }, { 1, kPitch, 58 }, { 1, kDecay, 180 }, { 1, kNoise, 75 }, { 3, kEngine, kCircuit }, { 3, kPitch, 45 }, { 3, kCharA, 30 },
                                   { 4, kEngine, kCircuit }, { 4, kPitch, 52 }, { 4, kCharA, 30 }, { 7, kEngine, kCircuit }, { 7, kPitch, 79 }, { 7, kCharB, 100 }, { 7, kDecay, 400 }, { 7, kTone, 60 } });
    K("KITS", "FM kit", { { 0, kEngine, kFm }, { 0, kCharA, 10 }, { 0, kTone, 30 }, { 1, kEngine, kFm }, { 1, kCharA, 55 }, { 1, kCharB, 80 }, { 1, kTone, 70 }, { 2, kEngine, kFm }, { 2, kCharA, 75 }, { 2, kTone, 50 },
                          { 3, kEngine, kFm }, { 3, kCharA, 20 }, { 4, kEngine, kFm }, { 4, kCharA, 20 }, { 5, kEngine, kFm }, { 5, kCharA, 90 }, { 5, kCharB, 90 }, { 6, kEngine, kFm }, { 6, kCharA, 90 }, { 6, kCharB, 90 },
                          { 7, kEngine, kFm }, { 7, kCharA, 63 }, { 7, kCharB, 40 }, { 7, kTone, 60 } });
    K("KITS", "West coast kit", { { 0, kEngine, kWest }, { 0, kCharA, 20 }, { 0, kTone, 40 }, { 1, kEngine, kWest }, { 1, kCharA, 60 }, { 1, kNoise, 50 }, { 2, kEngine, kWest }, { 2, kCharA, 40 },
                                  { 3, kEngine, kWest }, { 3, kCharA, 30 }, { 3, kTone, 60 }, { 4, kEngine, kWest }, { 4, kCharA, 45 }, { 4, kTone, 65 }, { 4, kCharB, 30 }, { 7, kEngine, kWest }, { 7, kCharA, 90 }, { 7, kPitch, 64 } });
    K("KITS", "Chaos kit", { { 1, kEngine, kChaos }, { 1, kCharA, 85 }, { 1, kTone, 70 }, { 2, kEngine, kChaos }, { 2, kCharA, 40 }, { 3, kEngine, kChaos }, { 3, kCharA, 20 }, { 3, kBend, 12 },
                             { 4, kEngine, kChaos }, { 4, kCharA, 60 }, { 5, kEngine, kChaos }, { 5, kCharA, 100 }, { 5, kTone, 95 }, { 6, kEngine, kChaos }, { 6, kCharA, 100 }, { 6, kTone, 90 } }, { { "drive", 40 } });
    K("PERCUSSION", "Hand drums", { { 0, kEngine, kCircuit }, { 0, kPitch, 45 }, { 0, kCharA, 60 }, { 0, kDecay, 300 }, { 1, kEngine, kModal }, { 1, kPitch, 62 }, { 1, kNoise, 0 }, { 1, kCharB, 20 },
                                    { 2, kEngine, kCircuit }, { 2, kPitch, 72 }, { 2, kCharA, 50 }, { 3, kEngine, kCircuit }, { 3, kPitch, 52 }, { 3, kCharA, 60 }, { 4, kEngine, kCircuit }, { 4, kPitch, 57 }, { 4, kCharA, 60 },
                                    { 5, kEngine, kString }, { 5, kPitch, 84 }, { 5, kCharB, 100 }, { 5, kDecay, 80 }, { 6, kEngine, kWest }, { 6, kPitch, 76 }, { 7, kEngine, kModal }, { 7, kCharA, 100 }, { 7, kPitch, 72 }, { 7, kCharB, 100 } });
    K("PERCUSSION", "Steel and wood", { { 0, kEngine, kModal }, { 0, kCharA, 66 }, { 0, kCharB, 0 }, { 0, kPitch, 45 }, { 1, kEngine, kModal }, { 1, kCharA, 66 }, { 1, kCharB, 5 }, { 1, kPitch, 57 }, { 1, kNoise, 0 },
                                        { 2, kEngine, kModal }, { 2, kCharA, 66 }, { 2, kCharB, 10 }, { 2, kPitch, 64 }, { 3, kEngine, kString }, { 3, kCharA, 60 }, { 3, kPitch, 60 }, { 4, kEngine, kString }, { 4, kCharA, 60 }, { 4, kPitch, 67 },
                                        { 7, kEngine, kModal }, { 7, kCharA, 100 }, { 7, kCharB, 100 }, { 7, kPitch, 60 }, { 7, kDecay, 4000 } });
    K("PERCUSSION", "Gongs and bells", { { 3, kEngine, kAdditive }, { 3, kCharA, 10 }, { 3, kPitch, 48 }, { 3, kDecay, 3000 }, { 4, kEngine, kAdditive }, { 4, kCharA, 15 }, { 4, kPitch, 60 }, { 4, kDecay, 2500 },
                                         { 7, kEngine, kAdditive }, { 7, kCharA, 40 }, { 7, kPitch, 41 }, { 7, kDecay, 6000 }, { 7, kCharB, 80 }, { 2, kEngine, kFm }, { 2, kCharA, 63 }, { 2, kPitch, 84 }, { 2, kDecay, 1500 } });
    K("PERCUSSION", "Plucked strings", { { 0, kEngine, kString }, { 0, kPitch, 28 }, { 0, kCharA, 0 }, { 0, kBend, 5 }, { 1, kEngine, kString }, { 1, kCharB, 100 }, { 1, kNoise, 50 }, { 2, kEngine, kString }, { 2, kPitch, 79 }, { 2, kCharA, 80 },
                                         { 3, kEngine, kString }, { 3, kPitch, 48 }, { 4, kEngine, kString }, { 4, kPitch, 60 }, { 7, kEngine, kString }, { 7, kCharA, 100 }, { 7, kPitch, 72 }, { 7, kDecay, 3000 } });
    return v;
    }();
    return v;
}

}} // namespace pump::strike
