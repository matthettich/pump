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
// LIMIT's gain is set so a dynamic mix (about -22 LUFS integrated at -6 dBFS peak) lands on the target; a denser
// mix (about -18 LUFS at the same peak) gets more COMP / BAND reduction and lands 1-3 LU louder: lower LIMIT's
// gain by that much (the -14 chains by about 3 dB).
// This plugin's role in each chain:
//   Pop master:             mono below 100 Hz, highs 108-115% wide
//   EDM master:             mono below 120 Hz (48 dB/oct), mids 105-120%, top 130% with 10% stereoize
//   Hip-hop master:         mono below 100 Hz, highs slightly wider (105-110%)
//   Rock master:            mono below 90 Hz, highs a touch wider (105-108%)
//   Acoustic & jazz master: almost neutral: no mono bass, top band 104%
//   Lo-fi master:           mono below 110 Hz and a slightly narrower image (90-95%)
//   Streaming master -14:   mono below 90 Hz, highs 105-110%
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
