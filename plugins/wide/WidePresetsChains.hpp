// pump WIDE: the matching mastering chains: the same names in every pump plugin, tuned to be used together.
// Shown under CHAINS in the preset menu. Included inside the plugin's namespace by WideParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
//
// Signal order: EQ -> CALM -> COMP -> BAND -> HEAT -> TONE -> WIDE -> LIMIT -> METER.
// Load the same-named preset in every plugin of that chain for a finished master. Gain staging assumes a mix
// peaking around -6 dBFS: EQ, CALM, BAND, HEAT, TONE and WIDE stay close to unity, COMP makes up its own gain
// reduction, LIMIT's gain sets the final loudness (its target is display only) with a -1 dBTP ceiling, and
// METER shows the chain's loudness / true-peak target. Targets (integrated, true peak):
//   Pop -9 LUFS, EDM -7.5, Hip-hop -8, Rock -9, Acoustic & jazz -14, Lo-fi -12, Streaming -14; all -1 dBTP.
// LIMIT's gain is set for a dynamic mix (about -22 LUFS integrated at -6 dBFS peak); with a denser mix that
// already sits nearer -18 LUFS, lower LIMIT's gain by the difference.
// This plugin's role in each chain:
//   Pop master:             -
//   EDM master:             -
//   Hip-hop master:         -
//   Rock master:            -
//   Acoustic & jazz master: -
//   Lo-fi master:           -
//   Streaming master -14:   -
#pragma once

inline void addChainPresets(std::vector<Preset>& v)
{
    v.push_back({ "Pop master", { { "mono_bass", 1 }, { "mono_freq", 100 }, { "b1_width", 100 }, { "b2_width", 100 }, { "b3_width", 108 }, { "b4_width", 115 } }, "CHAINS" });
    v.push_back({ "EDM master", { { "mono_bass", 1 }, { "mono_freq", 120 }, { "mono_slope", 1 }, { "b1_width", 100 }, { "b2_width", 105 }, { "b3_width", 120 }, { "b4_width", 130 }, { "b1_stereoize", 0 }, { "b2_stereoize", 0 }, { "b3_stereoize", 0 }, { "b4_stereoize", 10 } }, "CHAINS" });
    v.push_back({ "Hip-hop master", { { "mono_bass", 1 }, { "mono_freq", 100 }, { "b1_width", 100 }, { "b2_width", 100 }, { "b3_width", 105 }, { "b4_width", 110 } }, "CHAINS" });
    v.push_back({ "Rock master", { { "mono_bass", 1 }, { "mono_freq", 90 }, { "b1_width", 100 }, { "b2_width", 100 }, { "b3_width", 105 }, { "b4_width", 108 } }, "CHAINS" });
    v.push_back({ "Acoustic & jazz master", { { "b1_width", 100 }, { "b2_width", 100 }, { "b3_width", 100 }, { "b4_width", 104 } }, "CHAINS" });
    v.push_back({ "Lo-fi master", { { "mono_bass", 1 }, { "mono_freq", 110 }, { "b1_width", 100 }, { "b2_width", 95 }, { "b3_width", 95 }, { "b4_width", 90 } }, "CHAINS" });
    v.push_back({ "Streaming master -14", { { "mono_bass", 1 }, { "mono_freq", 90 }, { "b1_width", 100 }, { "b2_width", 100 }, { "b3_width", 105 }, { "b4_width", 110 } }, "CHAINS" });
}
