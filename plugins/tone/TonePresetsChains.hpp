// pump TONE: the matching mastering chains: the same names in every pump plugin, tuned to be used together.
// Shown under CHAINS in the preset menu. Included inside the plugin's namespace by ToneParams.hpp, after the
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
//   Pop master:             moderate auto-balance (recover 25, tame 30) with a slight brighten
//   EDM master:             firmer auto-balance (30/35), a touch of body and brightness
//   Hip-hop master:         light balance with +12 body for weight
//   Rock master:            moderate balance (30/28), no tilt
//   Acoustic & jazz master: very light (12/15), slow
//   Lo-fi master:           light balance, darker (brighten -15) and fuller (body +15)
//   Streaming master -14:   light balance (20/22)
#pragma once

inline void addChainPresets(std::vector<Preset>& v)
{
    v.push_back({ "Pop master", { { "recover", 25 }, { "tame", 30 }, { "brighten", 5 }, { "speed", 30 } }, "CHAINS" });
    v.push_back({ "EDM master", { { "recover", 30 }, { "tame", 35 }, { "brighten", 5 }, { "body", 10 }, { "speed", 35 } }, "CHAINS" });
    v.push_back({ "Hip-hop master", { { "recover", 20 }, { "tame", 25 }, { "body", 12 }, { "speed", 25 } }, "CHAINS" });
    v.push_back({ "Rock master", { { "recover", 30 }, { "tame", 28 }, { "speed", 30 } }, "CHAINS" });
    v.push_back({ "Acoustic & jazz master", { { "recover", 12 }, { "tame", 15 }, { "speed", 20 } }, "CHAINS" });
    v.push_back({ "Lo-fi master", { { "recover", 15 }, { "tame", 25 }, { "brighten", -15 }, { "body", 15 }, { "speed", 20 } }, "CHAINS" });
    v.push_back({ "Streaming master -14", { { "recover", 20 }, { "tame", 22 }, { "speed", 25 } }, "CHAINS" });
}
