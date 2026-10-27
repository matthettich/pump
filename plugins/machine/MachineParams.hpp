// pump MACHINE: parameters (shared by the DSP and the UI). GPL-3.0-only (see fw/firmware/NOTICE.md).
//
// The firmware's own sound parameters come first, one per knob on the machine's voice pages, as the machine's 0-127
// values (or its option lists), generated from the firmware's menu tables into MachineMeta.hpp. Their symbols are the
// firmware's parameter names, so they never change. Then pump's own.
#pragma once

#include "PumpParams.hpp"
#include "MachineMeta.hpp"
#include <deque>
#include <string>
#include <vector>

namespace pump { namespace machine {

enum Extra : int { kVolume = kNumFw, kNumParams };

// the seven sounds (the hat voice plays closed or open)
constexpr int kSounds = 7;
static const char* const kSoundNames[kSounds] = { "DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "C-HAT", "O-HAT" };
inline int voiceOfSound(int s) { return s >= 6 ? 5 : s; }

inline const std::vector<ParamSpec>& params()
{
    static std::vector<ParamSpec> v;
    if (!v.empty()) return v;
    const unsigned I = kPAutomatable | kPInteger, B = kPAutomatable | kPBoolean;
    for (int i = 0; i < kNumFw; ++i)
    {
        const FwParam& f = kFw[i];
        v.push_back({ f.sym, f.name, f.dial, (float)f.min, (float)f.max, (float)f.def, PUnit::None, PScale::Linear, f.kind == kOnOff ? B : I, f.choices, 1 });
    }
    v.push_back({ "volume", "Volume", "Volume", -48, 12, 0, PUnit::dB, PScale::Linear, kPAutomatable, nullptr, 1 });
    return v;
}

// A knob by a short name that means the same knob on every voice (Thunder Plus's preset names), as this voice's
// parameter symbol (nullptr when the voice has no such knob). Voices 0-2 drums, 3 snare, 4 cymbal, 5 hat.
inline std::string knobSymbol(int voice, const std::string& a)
{
    const std::string n = std::to_string(voice + 1);
    const bool cy = voice == 4, hh = voice == 5;
    const std::string mo = cy ? "_cym" : "", mw = cy ? "_cym" : "_hh";
    static const char* const wave[6] = { "osc_wave_drum1", "osc_wave_drum2", "osc_wave_drum3", "osc_wave_snare", "wave1_cym", "wave1_hh" };
    static const char* const dist[6] = { "drive1", "drive2", "drive3", "snare_distortion", "cymbal_distortion", "hat_distortion" };
    std::string s;
    if (a == "coarse") s = "coarse" + n; else if (a == "fine") s = "fine" + n; else if (a == "wave") s = wave[voice]; else if (a == "pw") s = "pwm_" + n;
    else if (a == "atk") s = "veloa" + n; else if (a == "dec") s = hh ? "velod6_closed" : "velod" + n; else if (a == "decO") s = "velod6_open";
    else if (a == "slope") s = "vol_slope" + n; else if (a == "rpt") s = "repeat" + n; else if (a == "pDec") s = "mod_eg" + n;
    else if (a == "pSlope") s = "pitch_slope" + n; else if (a == "pAmt") s = "modamnt" + n; else if (a == "fmAmt") s = "fmamnt" + n;
    else if (a == "fmFreq") s = "fm_freq" + n; else if (a == "fmWave") s = "mod_wave_drum" + n; else if (a == "fmMode") s = "mix_mod_" + n;
    else if (a == "noiseF") s = "noise_freq1"; else if (a == "noiseMix") s = "mix1";
    else if (a == "m1f") s = "mod_osc_f1" + mo; else if (a == "m2f") s = "mod_osc_f2" + mo; else if (a == "m1l") s = "mod_osc_gain1" + mo;
    else if (a == "m2l") s = "mod_osc_gain2" + mo; else if (a == "m1w") s = "wave2" + mw; else if (a == "m2w") s = "wave3" + mw;
    else if (a == "tWave") s = "trans" + n + "_wave"; else if (a == "tLvl") s = "trans" + n + "_vol"; else if (a == "tPitch") s = "trans" + n + "_freq";
    else if (a == "cut") s = "filter_freq_" + n; else if (a == "res") s = "reso_" + n; else if (a == "ftype") s = "filter_type_" + n;
    else if (a == "fdrive") s = "filter_drive_" + n; else if (a == "srate") s = "voice_decimation" + n; else if (a == "drive") s = dist[voice];
    else if (a == "vol") s = "vol" + n;
    for (int i = 0; i < kNumFw; ++i) if (s == kFw[i].sym && kFw[i].voice == voice) return s;
    return "";
}

// Factory kits, each voice a Thunder Plus firmware-voice preset; knobs a preset doesn't name keep the init kit's.
inline const std::vector<Preset>& presets()
{
    static std::vector<Preset> v;
    static std::deque<std::string> text;
    if (!v.empty()) return v;
    using Knobs = std::vector<std::pair<const char*, float>>;
    auto kit = [&](const char* name, const std::vector<Knobs>& voices) {
        Preset p { name, {}, "KITS" };
        for (int vo = 0; vo < (int)voices.size(); ++vo)
            for (const auto& k : voices[(size_t)vo])
            {
                const std::string sym = knobSymbol(vo, k.first);
                if (sym.empty()) continue;
                text.push_back(sym);
                p.values.push_back({ text.back().c_str(), k.second });
            }
        v.push_back(std::move(p));
    };
    v.push_back({ "Default", {} });   // the firmware's init kit
    kit("Deep and tight", { { { "coarse", 30 }, { "dec", 30 }, { "slope", 40 }, { "pAmt", 62 }, { "pDec", 24 }, { "pSlope", 30 }, { "tWave", 5 }, { "tLvl", 45 } },
        { { "coarse", 48 }, { "dec", 30 }, { "pAmt", 32 }, { "pDec", 30 }, { "tWave", 12 }, { "tLvl", 35 } },
        { { "coarse", 72 }, { "dec", 5 }, { "pAmt", 10 }, { "pDec", 6 }, { "tWave", 6 }, { "tLvl", 110 }, { "ftype", 1 }, { "cut", 62 } },
        { { "coarse", 48 }, { "dec", 10 }, { "noiseMix", 70 }, { "pAmt", 62 }, { "pDec", 12 }, { "tWave", 0 }, { "tLvl", 60 } },
        { { "dec", 118 }, { "ftype", 1 }, { "cut", 62 } },
        { { "dec", 3 }, { "ftype", 1 }, { "cut", 112 }, { "decO", 26 } } });
    kit("Punch", { { { "coarse", 36 }, { "dec", 22 }, { "slope", 50 }, { "pAmt", 72 }, { "pDec", 15 }, { "pSlope", 24 }, { "tWave", 2 }, { "tLvl", 80 }, { "drive", 85 } },
        { { "coarse", 42 }, { "dec", 34 }, { "pAmt", 34 }, { "pDec", 32 }, { "tWave", 12 }, { "tLvl", 35 } },
        { { "coarse", 92 }, { "dec", 6 }, { "slope", 70 }, { "pAmt", 0 }, { "tWave", 4 }, { "tLvl", 50 } },
        { { "coarse", 43 }, { "dec", 20 }, { "noiseMix", 82 }, { "pAmt", 70 }, { "pDec", 16 }, { "ftype", 0 }, { "cut", 105 }, { "drive", 85 } },
        { { "wave", 3 }, { "coarse", 100 }, { "m1f", 70 }, { "m1l", 90 }, { "m2f", 104 }, { "m2l", 80 }, { "m1w", 3 }, { "m2w", 3 }, { "dec", 116 }, { "ftype", 2 }, { "cut", 96 }, { "res", 18 } },
        { { "wave", 3 }, { "m1w", 3 }, { "m2w", 3 }, { "m1f", 80 }, { "m2f", 110 }, { "dec", 6 }, { "ftype", 2 }, { "cut", 106 }, { "res", 30 }, { "decO", 30 } } });
    kit("Dirty", { { { "coarse", 34 }, { "dec", 27 }, { "pAmt", 78 }, { "pDec", 18 }, { "drive", 118 }, { "fdrive", 70 }, { "cut", 96 }, { "tWave", 10 }, { "tLvl", 60 } },
        { { "coarse", 58 }, { "dec", 18 }, { "slope", 60 }, { "pAmt", 20 }, { "pDec", 12 }, { "fmAmt", 85 }, { "fmFreq", 72 }, { "fmWave", 0 } },
        { { "coarse", 66 }, { "dec", 20 }, { "pAmt", 118 }, { "pDec", 26 }, { "pSlope", 60 } },
        { { "coarse", 46 }, { "dec", 14 }, { "noiseMix", 75 }, { "pAmt", 60 }, { "pDec", 14 }, { "srate", 48 }, { "drive", 105 } },
        { { "dec", 112 }, { "drive", 96 }, { "srate", 72 }, { "ftype", 1 }, { "cut", 58 }, { "vol", 100 } },
        { { "dec", 6 }, { "srate", 60 }, { "drive", 100 }, { "decO", 30 } } });
    kit("Lo-fi", { { { "coarse", 33 }, { "dec", 26 }, { "pAmt", 66 }, { "pDec", 20 }, { "srate", 38 }, { "drive", 90 } },
        { { "coarse", 54 }, { "dec", 26 }, { "pAmt", 30 }, { "pDec", 28 }, { "tWave", 12 }, { "tLvl", 30 } },
        { { "coarse", 76 }, { "dec", 16 }, { "wave", 3 }, { "pAmt", 0 } },
        { { "coarse", 45 }, { "dec", 16 }, { "noiseMix", 78 }, { "ftype", 0 }, { "cut", 82 }, { "srate", 62 } },
        { { "dec", 116 }, { "ftype", 0 }, { "cut", 90 }, { "res", 15 } },
        { { "wave", 4 }, { "atk", 16 }, { "dec", 10 }, { "ftype", 1 }, { "cut", 100 }, { "decO", 24 } } });
    kit("Long boom", { { { "coarse", 29 }, { "dec", 52 }, { "slope", 30 }, { "pAmt", 38 }, { "pDec", 30 }, { "pSlope", 40 } },
        { { "coarse", 27 }, { "dec", 58 }, { "pAmt", 50 }, { "pDec", 60 }, { "pSlope", 80 } },
        { { "coarse", 62 }, { "dec", 14 }, { "wave", 3 }, { "fmAmt", 60 }, { "fmFreq", 66 }, { "fmWave", 3 }, { "ftype", 2 }, { "cut", 92 }, { "res", 40 } },
        { { "dec", 13 }, { "rpt", 3 }, { "noiseMix", 127 }, { "ftype", 2 }, { "cut", 80 }, { "res", 25 }, { "tWave", 9 }, { "tLvl", 80 } },
        { { "dec", 104 }, { "ftype", 1 }, { "cut", 74 } },
        { { "dec", 4 }, { "ftype", 2 }, { "cut", 100 }, { "vol", 127 }, { "decO", 44 } } });
    kit("Toms and bell", { { { "coarse", 42 }, { "dec", 34 }, { "pAmt", 34 }, { "pDec", 32 }, { "tWave", 12 }, { "tLvl", 35 } },
        { { "coarse", 48 }, { "dec", 30 }, { "pAmt", 32 }, { "pDec", 30 }, { "tWave", 12 }, { "tLvl", 35 } },
        { { "coarse", 54 }, { "dec", 26 }, { "pAmt", 30 }, { "pDec", 28 }, { "tWave", 12 }, { "tLvl", 30 } },
        { { "coarse", 47 }, { "dec", 18 }, { "slope", 127 }, { "noiseMix", 80 }, { "pAmt", 55 }, { "pDec", 14 }, { "drive", 80 } },
        { { "wave", 0 }, { "coarse", 84 }, { "m1w", 0 }, { "m1f", 60 }, { "m1l", 75 }, { "m2w", 0 }, { "m2f", 92 }, { "m2l", 50 }, { "dec", 110 }, { "ftype", 7 }, { "vol", 112 } },
        { { "dec", 3 }, { "ftype", 1 }, { "cut", 112 }, { "decO", 26 } } });
    kit("Snaps and zaps", { { { "coarse", 66 }, { "dec", 20 }, { "pAmt", 118 }, { "pDec", 26 }, { "pSlope", 60 } },
        { { "coarse", 76 }, { "dec", 16 }, { "wave", 3 }, { "pAmt", 0 } },
        { { "coarse", 27 }, { "dec", 58 }, { "pAmt", 50 }, { "pDec", 60 }, { "pSlope", 80 } },
        { { "dec", 6 }, { "noiseMix", 100 }, { "ftype", 1 }, { "cut", 70 }, { "tWave", 13 }, { "tLvl", 90 } },
        { { "atk", 95 }, { "dec", 114 }, { "ftype", 1 }, { "cut", 66 }, { "vol", 112 } },
        { { "wave", 4 }, { "atk", 16 }, { "dec", 10 }, { "ftype", 1 }, { "cut", 100 }, { "decO", 20 } } });
    return v;
}

}} // namespace pump::machine
