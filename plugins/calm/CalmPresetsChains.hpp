// pump CALM: the matching mastering chains: the same names in every pump plugin, tuned to be used together.
// Shown under CHAINS in the preset menu. Included inside the plugin's namespace by CalmParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
//
// Signal order: EQ -> CALM -> COMP -> BAND -> HEAT -> TONE -> WIDE -> LIMIT -> METER.
// Load the same-named preset in every plugin of that chain for a finished master. Gain staging assumes a mix
// peaking around -6 dBFS: EQ, CALM, BAND, HEAT, TONE and WIDE stay close to unity, COMP makes up its own gain
// reduction, LIMIT's gain sets the final loudness (its target is display only) with a -1 dBTP ceiling, and
// METER shows the chain's loudness / true-peak target. Targets (integrated, true peak):
//   Pop -9 LUFS, EDM -7.5, Hip-hop -8, Rock -9, Acoustic & jazz -14, Lo-fi -12, Streaming -14; all -1 dBTP.
// LIMIT's gain is set so a dynamic mix (about -22 LUFS integrated at -6 dBFS peak) lands on the target; a denser
// mix (about -18 LUFS at the same peak) gets more COMP / BAND reduction and lands 1-3 LU louder: lower LIMIT's
// gain by that much (the -14 chains by about 3 dB).
// This plugin's role in each chain:
//   Pop master:             moderate de-harsh above 500 Hz, extra sensitivity at 3.2 kHz
//   EDM master:             moderate resonance control for bright synths, extra sensitivity at 3 kHz
//   Hip-hop master:         light: low-mid boom (300 Hz) and harsh peaks, slow release
//   Rock master:            de-harsh for guitars, 600 Hz-12 kHz, a little extra sensitivity at 3 kHz
//   Acoustic & jazz master: very light, sharp and selective (8192 FFT): only the clear resonances
//   Lo-fi master:           light softening of the top end (1.5-16 kHz, extra at 8 kHz)
//   Streaming master -14:   light all-round resonance control 200 Hz-16 kHz
#pragma once

inline void addChainPresets(std::vector<Preset>& v)
{
    v.push_back({ "Pop master", { { "depth", 30 }, { "sharpness", 55 }, { "selectivity", 55 }, { "attack", 10 }, { "release", 150 }, { "focus_low", 500 }, { "focus_high", 14000 }, { "s4_freq", 3200 }, { "s4_gain", 2 } }, "CHAINS" });
    v.push_back({ "EDM master", { { "depth", 30 }, { "sharpness", 55 }, { "selectivity", 55 }, { "attack", 8 }, { "release", 120 }, { "focus_low", 400 }, { "focus_high", 14000 }, { "s4_freq", 3000 }, { "s4_gain", 2 } }, "CHAINS" });
    v.push_back({ "Hip-hop master", { { "depth", 25 }, { "sharpness", 55 }, { "selectivity", 55 }, { "attack", 12 }, { "release", 160 }, { "focus_low", 180 }, { "focus_high", 12000 }, { "s2_freq", 300 }, { "s2_gain", 1.5f } }, "CHAINS" });
    v.push_back({ "Rock master", { { "depth", 26 }, { "sharpness", 50 }, { "selectivity", 52 }, { "attack", 10 }, { "release", 140 }, { "focus_low", 600 }, { "focus_high", 12000 }, { "s4_freq", 3000 }, { "s4_gain", 1.5f }, { "s4_q", 0.8f } }, "CHAINS" });
    v.push_back({ "Acoustic & jazz master", { { "depth", 16 }, { "sharpness", 70 }, { "selectivity", 65 }, { "attack", 25 }, { "release", 250 }, { "focus_low", 120 }, { "quality", 3 } }, "CHAINS" });
    v.push_back({ "Lo-fi master", { { "depth", 22 }, { "sharpness", 40 }, { "selectivity", 50 }, { "attack", 15 }, { "release", 200 }, { "focus_low", 1500 }, { "focus_high", 16000 }, { "s6_freq", 8000 }, { "s6_gain", 2 } }, "CHAINS" });
    v.push_back({ "Streaming master -14", { { "depth", 24 }, { "sharpness", 60 }, { "selectivity", 58 }, { "attack", 12 }, { "release", 180 }, { "focus_low", 200 }, { "focus_high", 16000 } }, "CHAINS" });
}
