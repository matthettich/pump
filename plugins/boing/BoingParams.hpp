// pump BOING: parameters and factory kits (shared by the DSP and the UI).
//
// A drum machine of springs. Every pad has two layers and a BLEND between them:
//   SPRING   one of COIL's tuned springs (SPRING's six tanks), kicked by the hit: HARMONIC from the spring's clang to a
//            clean tone, SPREAD its springs apart, DRIP the chirp; the noise layer runs through it too (snare wires)
//   ANALOG   a resonant analog-style drum voice: an oscillator (sine, triangle, fold, square, saw or six metallic
//            partials) with its own pitch drop, a noise source through a resonant filter, a click
// Both share PITCH, DECAY, NOISE (and its TONE), and the analog layer has BEND (how far above the pitch a hit starts,
// falling in BEND TIME),
// CLICK, HITS (1-4 quick repeats: claps), LEVEL, PAN and SEND (to a real spring tank shared by the kit).
#pragma once

#include "PumpParams.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace boing {

constexpr int kPads = 8;
enum PadParam : int { kTank = 0, kWave, kBlend, kPitch, kDecay, kHarmonic, kSpread, kDrip, kBend, kBendTime, kNoise, kNoiseTone, kClick, kHits, kLevel, kPan, kSend, kPerPad };
inline int pp(int pad, int what) { return pad * kPerPad + what; }
enum KitParam : int { kTankModel = kPads * kPerPad, kTankDecay, kTankLevel, kChoke, kVelocity, kDrive, kVolume, kFollow, kNumParams };

static const char* const kTankNames[] = { "Two-spring", "Three-spring", "Short tank", "Long tank", "Digital", "Lo-fi digital" };
static const char* const kTankKeys[] = { "TWO", "THREE", "SHORT", "LONG", "DIGITAL", "LO-FI" };
enum Wave : int { kSine = 0, kTri, kFold, kSquare, kSaw, kMetal, kNumWaves };
static const char* const kWaveNames[] = { "Sine", "Triangle", "Fold", "Square", "Saw", "Metal" };
static const char* const kWaveKeys[] = { "SINE", "TRI", "FOLD", "SQUARE", "SAW", "METAL" };
static const char* const kOnOff[] = { "Off", "On" };

struct PadInfo { const char* sym; const char* name; const char* label; int note; float tank, wave, blend, pitch, decay, harm, spread, drip, bend, bendT, noise, ntone, click, hits, level, pan, send; };
// The pads answer the General MIDI drum notes.
static const PadInfo kPadInfo[kPads] = {
    { "kick",  "Kick",       "KICK",   36, 3, kSine,  45, 33, 450,  40,   0, 30, 24, 40,  0, 30, 40, 1,   0,   0,  5 },
    { "snare", "Snare",      "SNARE",  38, 2, kTri,   70, 50, 260,   0,  30, 70,  7, 20, 70, 65, 50, 1,  -2,   0, 20 },
    { "clap",  "Clap",       "CLAP",   39, 1, kTri,   45, 62, 240,   0,  60, 90,  0, 10, 90, 55, 40, 4,  -3,  10, 30 },
    { "ltom",  "Low Tom",    "LO TOM", 43, 0, kSine,  60, 43, 380,  30,  10, 50, 12, 60, 10, 40, 30, 1,  -3, -25, 15 },
    { "htom",  "High Tom",   "HI TOM", 48, 0, kSine,  60, 52, 320,  30,  10, 50, 12, 50, 10, 45, 30, 1,  -4,  25, 15 },
    { "chat",  "Closed Hat", "C-HAT",  42, 4, kMetal, 55, 96,  55,   0, 100,  0,  0,  5, 60, 90, 20, 1,  -8, -15,  5 },
    { "ohat",  "Open Hat",   "O-HAT",  46, 5, kMetal, 55, 94, 480,   0, 100,  0,  0,  5, 60, 85, 15, 1,  -9, -15, 10 },
    { "cym",   "Cymbal",     "CYMBAL", 49, 4, kMetal, 60, 89, 1600,  0,  90, 20,  0,  5, 40, 80, 10, 1, -11,  30, 20 },
};

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    static std::deque<std::string> text;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean;
    auto s = [&](const std::string& x) { text.push_back(x); return text.back().c_str(); };
    for (int p = 0; p < kPads; ++p)
    {
        const PadInfo& d = kPadInfo[p];
        const std::string sy = d.sym, nm = d.name;
        v.push_back({ s(sy + "_tank"), s(nm + " Spring Tank"), "Tank", 0, 5, d.tank, PUnit::None, PScale::Linear, I, kTankNames, 1 });
        v.push_back({ s(sy + "_wave"), s(nm + " Analog Wave"), "Wave", 0, kNumWaves - 1, d.wave, PUnit::None, PScale::Linear, I, kWaveNames, 1 });
        v.push_back({ s(sy + "_blend"), s(nm + " Analog / Spring"), "Blend", 0, 100, d.blend, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_pitch"), s(nm + " Pitch"), "Pitch", 12, 108, d.pitch, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_decay"), s(nm + " Decay"), "Decay", 10, 5000, d.decay, PUnit::ms, PScale::Log, A, nullptr, 1 });
        v.push_back({ s(sy + "_harmonic"), s(nm + " Harmonic"), "Harmonic", 0, 100, d.harm, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_spread"), s(nm + " Spread"), "Spread", 0, 100, d.spread, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_drip"), s(nm + " Drip / Resonance"), "Drip", 0, 100, d.drip, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_bend"), s(nm + " Bend"), "Bend", 0, 48, d.bend, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_bend_time"), s(nm + " Bend Time"), "B-Time", 2, 500, d.bendT, PUnit::ms, PScale::Log, A, nullptr, 1 });
        v.push_back({ s(sy + "_noise"), s(nm + " Noise"), "Noise", 0, 100, d.noise, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_noise_tone"), s(nm + " Noise Tone"), "N-Tone", 0, 100, d.ntone, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_click"), s(nm + " Click"), "Click", 0, 100, d.click, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_hits"), s(nm + " Hits"), "Hits", 1, 4, d.hits, PUnit::None, PScale::Linear, I, nullptr, 1 });
        v.push_back({ s(sy + "_level"), s(nm + " Level"), "Level", -48, 6, d.level, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_pan"), s(nm + " Pan"), "Pan", -100, 100, d.pan, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_send"), s(nm + " Tank Send"), "Send", 0, 100, d.send, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    }
    v.push_back({ "tank_model", "Kit Tank Model", "Tank", 0, 5, 0, PUnit::None, PScale::Linear, I, kTankNames, 1 });
    v.push_back({ "tank_decay", "Kit Tank Decay", "Decay", 0.3f, 8, 2.0f, PUnit::Seconds, PScale::Log, A, nullptr, 1 });
    v.push_back({ "tank_level", "Kit Tank Level", "Return", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "choke", "Closed Hat Chokes Open", "Choke", 0, 1, 1, PUnit::None, PScale::Linear, B, kOnOff, 1 });
    v.push_back({ "velocity", "Velocity Sensitivity", "Velocity", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "drive", "Kit Drive", "Drive", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "follow", "Edit the Pad Played", "Follow", 0, 1, 1, PUnit::None, PScale::Linear, kPBoolean | kPNoUndo, nullptr, 1 });   // appended
    return v;
}

// Factory kits: changes from the default pads, as (pad, field, value) lists.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    static std::deque<std::string> keep;
    static const char* const field[kPerPad] = { "tank", "wave", "blend", "pitch", "decay", "harmonic", "spread", "drip", "bend", "bend_time", "noise", "noise_tone", "click", "hits", "level", "pan", "send" };
    struct C { int pad, f; float v; };
    auto F = [&](const char* sec, const char* n, std::initializer_list<C> cs, std::vector<PresetValue> kit = {}) {
        std::vector<PresetValue> p = std::move(kit);
        for (const C& c : cs) { keep.push_back(std::string(kPadInfo[c.pad].sym) + "_" + field[c.f]); p.push_back({ keep.back().c_str(), c.v }); }
        v.push_back({ n, std::move(p), sec });
    };
    v.push_back({ "Default", {} });
    F("SPRINGS", "Full boing", { { 0, kBlend, 85 }, { 1, kBlend, 90 }, { 3, kBlend, 95 }, { 4, kBlend, 95 }, { 0, kHarmonic, 20 }, { 3, kDrip, 80 }, { 4, kDrip, 80 } }, { { "tank_level", 55 } });
    F("SPRINGS", "Clean pitched kit", { { 0, kHarmonic, 80 }, { 1, kHarmonic, 70 }, { 3, kHarmonic, 90 }, { 4, kHarmonic, 90 }, { 2, kHarmonic, 60 }, { 3, kBlend, 85 }, { 4, kBlend, 85 } });
    F("SPRINGS", "Drip percussion", { { 0, kBlend, 70 }, { 0, kDrip, 100 }, { 3, kDrip, 100 }, { 4, kDrip, 100 }, { 1, kDrip, 100 }, { 3, kTank, 2 }, { 4, kTank, 2 }, { 3, kBend, 0 }, { 4, kBend, 0 } }, { { "tank_model", 1 } });
    F("SPRINGS", "Tank room", { { 0, kSend, 40 }, { 1, kSend, 60 }, { 2, kSend, 70 }, { 3, kSend, 50 }, { 4, kSend, 50 } }, { { "tank_level", 80 }, { "tank_decay", 4.0f }, { "tank_model", 3 } });
    F("ANALOG", "Analog round", { { 0, kBlend, 10 }, { 1, kBlend, 20 }, { 2, kBlend, 15 }, { 3, kBlend, 10 }, { 4, kBlend, 10 }, { 5, kBlend, 15 }, { 6, kBlend, 15 }, { 7, kBlend, 20 } });
    F("ANALOG", "Resonant noise kit", { { 1, kDrip, 95 }, { 1, kBlend, 15 }, { 2, kDrip, 90 }, { 2, kBlend, 10 }, { 5, kDrip, 70 }, { 6, kDrip, 70 }, { 0, kBlend, 15 } });
    F("ANALOG", "Fold and saw", { { 0, kWave, kFold }, { 0, kHarmonic, 70 }, { 0, kBlend, 20 }, { 3, kWave, kSaw }, { 4, kWave, kSaw }, { 3, kBlend, 25 }, { 4, kBlend, 25 }, { 1, kWave, kSquare } });
    F("ANALOG", "Long booming kick", { { 0, kDecay, 1400 }, { 0, kBend, 12 }, { 0, kBendTime, 80 }, { 0, kBlend, 10 }, { 0, kClick, 20 }, { 0, kPitch, 31 } });
    F("METAL", "Gong cymbals", { { 7, kDecay, 4000 }, { 7, kTank, 3 }, { 7, kBlend, 80 }, { 6, kTank, 3 }, { 6, kDecay, 1500 }, { 5, kTank, 3 } }, { { "tank_model", 3 }, { "tank_level", 50 } });
    F("METAL", "Lo-fi hats", { { 5, kTank, 5 }, { 6, kTank, 5 }, { 7, kTank, 5 }, { 5, kBlend, 85 }, { 6, kBlend, 85 }, { 7, kBlend, 85 } });
    F("METAL", "Bell toms", { { 3, kWave, kMetal }, { 4, kWave, kMetal }, { 3, kSpread, 40 }, { 4, kSpread, 40 }, { 3, kDecay, 900 }, { 4, kDecay, 800 }, { 3, kBend, 0 }, { 4, kBend, 0 } });
    F("WILD", "Spring zaps", { { 0, kBend, 48 }, { 0, kBendTime, 200 }, { 1, kBend, 36 }, { 1, kBendTime, 150 }, { 3, kBend, 48 }, { 4, kBend, 48 }, { 3, kBendTime, 300 }, { 4, kBendTime, 250 } }, { { "drive", 30 } });
    F("WILD", "Crushed digital kit", { { 0, kTank, 5 }, { 1, kTank, 5 }, { 2, kTank, 5 }, { 3, kTank, 4 }, { 4, kTank, 4 }, { 0, kBlend, 80 }, { 1, kBlend, 85 } }, { { "drive", 60 } });
    return v;
}

}} // namespace pump::boing
