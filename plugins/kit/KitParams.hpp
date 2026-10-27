// pump KIT: parameters and factory kits (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace kit {

// Eight pads, each with the same twelve parameters, then the kit's own. Symbols never change once released.
constexpr int kPads = 8;
enum PadParam : int { kModel = 0, kTune, kDecay, kPitch, kPitchDecay, kTone, kNoise, kClick, kChar, kDrive, kLevel, kPan, kPerPad };
inline int pp(int pad, int what) { return pad * kPerPad + what; }
enum KitParam : int { kKitDrive = kPads * kPerPad, kVelocity, kHumanize, kVolume, kFollow, kNumParams };

// The voice models. Every pad can be any of them; TONE and CHAR mean what suits each:
//   Kick    a sine with a pitch drop; CLICK a noise / pulse transient; CHAR folds it toward a harder tone; TONE a low-pass
//   Snare   two tuned body modes plus noise (NOISE = the mix, CLICK the snap); CHAR the noise's own length; TONE its high-pass
//   Clap    band-passed noise in quick bursts, then a tail; CHAR spreads the bursts; TONE the band
//   Hat     six detuned square partials plus noise through a band- and high-pass; CHAR the metallic spread; TONE the band
//   Tom     sine and triangle with a pitch drop; CHAR the triangle's share; TONE a low-pass
//   Metal   the hat's partials, longer and lower (cymbals, cowbells, ride); CHAR the spread; TONE the band
//   FM      a two-operator FM voice, the index falling with PITCH DECAY (bells, zaps, blips); CHAR the ratio; TONE a low-pass
//   Noise   noise through a resonant filter that sweeps with PITCH (zaps, sweeps, snares); CHAR the resonance; TONE the base
enum Model : int { kKick = 0, kSnare, kClap, kHat, kTom, kMetal, kFm, kNoiseModel, kNumModels };
static const char* const kModelNames[] = { "Kick", "Snare", "Clap", "Hat", "Tom", "Metal", "FM", "Noise" };
static const char* const kModelKeys[] = { "KICK", "SNARE", "CLAP", "HAT", "TOM", "METAL", "FM", "NOISE" };

struct PadInfo { const char* sym; const char* name; const char* label; int note; int model; float tune, decay, pitch, pdecay, tone, noise, click, chr, drive, level, pan; };
// Pads answer the General MIDI drum notes.
static const PadInfo kPadInfo[kPads] = {
    { "kick",  "Kick",       "KICK",   36, kKick,       0, 450, 30, 40, 50,   0, 40, 20, 10,   0,   0 },
    { "snare", "Snare",      "SNARE",  38, kSnare,      0, 220, 12, 30, 55,  60, 40, 50,  0,  -2,   0 },
    { "clap",  "Clap",       "CLAP",   39, kClap,       0, 260,  0, 20, 50, 100,  0, 50,  0,  -4,  10 },
    { "chat",  "Closed Hat", "C-HAT",  42, kHat,        0,  60,  0, 10, 60,  30, 20, 50,  0,  -8, -20 },
    { "ohat",  "Open Hat",   "O-HAT",  46, kHat,        0, 420,  0, 10, 60,  30,  0, 50,  0,  -9, -20 },
    { "tom",   "Tom",        "TOM",    45, kTom,        0, 350, 14, 60, 50,   5, 20, 30,  0,  -4,  25 },
    { "perc",  "Perc",       "PERC",   37, kFm,         0, 160, 24, 40, 60,   0, 30, 35,  0,  -8, -30 },
    { "cym",   "Cymbal",     "CYMBAL", 49, kMetal,      0, 1500, 0, 10, 55,  30,  0, 50,  0, -12,  30 },
};

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    static std::deque<std::string> text;
    if (!v.empty()) return v;
    const unsigned A = kPAutomatable, I = kPAutomatable | kPInteger;
    auto s = [&](const std::string& x) { text.push_back(x); return text.back().c_str(); };
    for (int p = 0; p < kPads; ++p)
    {
        const PadInfo& d = kPadInfo[p];
        const std::string sy = d.sym, nm = d.name;
        v.push_back({ s(sy + "_model"), s(nm + " Model"), "Model", 0, kNumModels - 1, (float)d.model, PUnit::None, PScale::Linear, I, kModelNames, 1 });
        v.push_back({ s(sy + "_tune"), s(nm + " Tune"), "Tune", -24, 24, d.tune, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_decay"), s(nm + " Decay"), "Decay", 10, 4000, d.decay, PUnit::ms, PScale::Log, A, nullptr, 1 });
        v.push_back({ s(sy + "_pitch"), s(nm + " Pitch Envelope"), "Pitch", 0, 48, d.pitch, PUnit::None, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_pdecay"), s(nm + " Pitch Decay"), "P-Decay", 2, 1000, d.pdecay, PUnit::ms, PScale::Log, A, nullptr, 1 });
        v.push_back({ s(sy + "_tone"), s(nm + " Tone"), "Tone", 0, 100, d.tone, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_noise"), s(nm + " Noise"), "Noise", 0, 100, d.noise, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_click"), s(nm + " Click"), "Click", 0, 100, d.click, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_char"), s(nm + " Character"), "Char", 0, 100, d.chr, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_drive"), s(nm + " Drive"), "Drive", 0, 100, d.drive, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_level"), s(nm + " Level"), "Level", -48, 6, d.level, PUnit::dB, PScale::Linear, A, nullptr, 1 });
        v.push_back({ s(sy + "_pan"), s(nm + " Pan"), "Pan", -100, 100, d.pan, PUnit::None, PScale::Linear, A, nullptr, 1 });
    }
    v.push_back({ "kit_drive", "Kit Drive", "Drive", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "velocity", "Velocity Sensitivity", "Velocity", 0, 100, 60, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "humanize", "Humanize", "Humanize", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "follow", "Edit the Pad Played", "Follow", 0, 1, 1, PUnit::None, PScale::Linear, kPBoolean | kPNoUndo, nullptr, 1 });   // appended
    return v;
}

// Factory kits. Models: 0 kick 1 snare 2 clap 3 hat 4 tom 5 metal 6 FM 7 noise.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    if (!v.empty()) return v;
    auto F = [&](const char* sec, const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), sec }); };
    v.push_back({ "Default", {} });
    F("KITS", "Long boom", { { "kick_decay", 1800 }, { "kick_pitch", 22 }, { "kick_pdecay", 70 }, { "kick_click", 15 }, { "kick_drive", 25 }, { "kick_tune", -3 },
        { "snare_tune", 2 }, { "snare_noise", 70 }, { "snare_decay", 180 }, { "clap_decay", 320 }, { "chat_decay", 45 }, { "ohat_decay", 300 }, { "humanize", 20 } });
    F("KITS", "Tight electro", { { "kick_decay", 260 }, { "kick_pitch", 36 }, { "kick_pdecay", 18 }, { "kick_click", 70 }, { "kick_char", 40 },
        { "snare_model", 7 }, { "snare_pitch", 30 }, { "snare_pdecay", 25 }, { "snare_decay", 150 }, { "snare_char", 60 }, { "snare_tone", 60 },
        { "chat_decay", 35 }, { "chat_char", 80 }, { "ohat_char", 80 }, { "perc_char", 80 }, { "perc_pitch", 36 }, { "perc_pdecay", 15 } });
    F("KITS", "Techno warehouse", { { "kick_decay", 600 }, { "kick_pitch", 26 }, { "kick_drive", 55 }, { "kick_char", 35 }, { "kick_tone", 40 }, { "kick_tune", -2 },
        { "clap_char", 75 }, { "clap_decay", 420 }, { "clap_tone", 40 }, { "chat_tone", 75 }, { "ohat_decay", 600 }, { "ohat_tone", 70 },
        { "perc_model", 5 }, { "perc_decay", 250 }, { "perc_tone", 35 }, { "cym_decay", 2600 }, { "kit_drive", 25 } });
    F("KITS", "House", { { "kick_decay", 380 }, { "kick_pitch", 24 }, { "kick_click", 30 }, { "clap_decay", 300 }, { "clap_char", 55 }, { "snare_level", -8 },
        { "chat_decay", 55 }, { "ohat_decay", 260 }, { "ohat_level", -7 }, { "perc_model", 5 }, { "perc_char", 25 }, { "perc_tune", 7 }, { "perc_decay", 120 }, { "humanize", 15 } });
    F("KITS", "Lo-fi dusty", { { "kick_tone", 25 }, { "kick_decay", 350 }, { "kick_drive", 40 }, { "snare_tone", 35 }, { "snare_decay", 180 }, { "snare_drive", 35 },
        { "chat_tone", 35 }, { "ohat_tone", 35 }, { "clap_tone", 35 }, { "kit_drive", 45 }, { "humanize", 45 }, { "velocity", 80 } });
    F("KITS", "Industrial", { { "kick_drive", 85 }, { "kick_char", 70 }, { "kick_click", 80 }, { "snare_model", 5 }, { "snare_decay", 300 }, { "snare_tone", 30 }, { "snare_drive", 60 },
        { "clap_model", 7 }, { "clap_pitch", 40 }, { "clap_pdecay", 60 }, { "clap_char", 70 }, { "tom_model", 6 }, { "tom_char", 90 }, { "tom_pitch", 30 }, { "kit_drive", 50 } });
    F("KITS", "Minimal clicks", { { "kick_decay", 160 }, { "kick_pitch", 40 }, { "kick_pdecay", 8 }, { "kick_click", 90 }, { "snare_model", 6 }, { "snare_decay", 90 }, { "snare_char", 70 },
        { "clap_decay", 120 }, { "chat_decay", 20 }, { "ohat_decay", 120 }, { "perc_decay", 60 }, { "perc_char", 95 }, { "tom_decay", 120 }, { "tom_tune", 12 }, { "volume", 6 } });
    F("KITS", "Breakbeat", { { "kick_decay", 300 }, { "kick_tone", 60 }, { "kick_noise", 15 }, { "snare_noise", 75 }, { "snare_click", 70 }, { "snare_decay", 260 }, { "snare_tune", 4 },
        { "chat_decay", 70 }, { "ohat_decay", 500 }, { "cym_decay", 2200 }, { "humanize", 35 }, { "velocity", 85 }, { "kit_drive", 20 } });
    F("KITS", "FM percussion", { { "kick_model", 6 }, { "kick_char", 10 }, { "kick_pitch", 30 }, { "snare_model", 6 }, { "snare_char", 70 }, { "snare_noise", 30 },
        { "clap_model", 6 }, { "clap_char", 90 }, { "clap_decay", 120 }, { "tom_model", 6 }, { "tom_char", 30 }, { "perc_char", 60 }, { "cym_model", 6 }, { "cym_char", 85 }, { "cym_decay", 900 } });
    F("KITS", "Big room", { { "kick_decay", 700 }, { "kick_pitch", 34 }, { "kick_drive", 70 }, { "kick_click", 50 }, { "kick_char", 25 }, { "clap_decay", 520 }, { "clap_char", 80 }, { "clap_level", -2 },
        { "snare_noise", 80 }, { "snare_decay", 400 }, { "cym_decay", 3500 }, { "kit_drive", 30 } });
    F("KITS", "Trap", { { "kick_decay", 2500 }, { "kick_pitch", 14 }, { "kick_pdecay", 120 }, { "kick_click", 25 }, { "kick_tune", -5 }, { "kick_drive", 20 },
        { "snare_tune", 5 }, { "snare_decay", 160 }, { "snare_noise", 55 }, { "clap_char", 40 }, { "chat_decay", 30 }, { "chat_tone", 70 }, { "ohat_decay", 250 } });
    F("KITS", "Zaps and blips", { { "kick_model", 7 }, { "kick_pitch", 40 }, { "kick_pdecay", 80 }, { "kick_char", 80 }, { "snare_model", 7 }, { "snare_pitch", 30 }, { "snare_char", 60 },
        { "clap_model", 6 }, { "clap_char", 75 }, { "tom_model", 7 }, { "tom_pitch", 44 }, { "tom_char", 85 }, { "perc_pitch", 48 }, { "perc_pdecay", 6 }, { "cym_model", 7 }, { "cym_tone", 80 }, { "volume", 8 } });
    return v;
}

}} // namespace pump::kit
