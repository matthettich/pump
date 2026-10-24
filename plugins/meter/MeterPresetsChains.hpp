// pump METER: the matching mastering chains: the same names in every pump plugin, tuned to be used together.
// Shown under CHAINS in the preset menu. Included inside the plugin's namespace by MeterParams.hpp, after the
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
//   Pop master:             custom target -9 LUFS / -1 dBTP
//   EDM master:             custom target -7.5 LUFS / -1 dBTP, -36..0 LUFS scale
//   Hip-hop master:         custom target -8 LUFS / -1 dBTP
//   Rock master:            custom target -9 LUFS / -1 dBTP
//   Acoustic & jazz master: Streaming -14 LUFS / -1 dBTP
//   Lo-fi master:           custom target -12 LUFS / -1 dBTP
//   Streaming master -14:   Streaming -14 LUFS / -1 dBTP
#pragma once

inline void addChainPresets(std::vector<Preset>& v)
{
    v.push_back({ "Pop master", { { "target", 7 }, { "custom_lufs", -9 }, { "custom_tp", -1 } }, "CHAINS" });
    v.push_back({ "EDM master", { { "target", 7 }, { "custom_lufs", -7.5f }, { "custom_tp", -1 }, { "scale", 3 } }, "CHAINS" });
    v.push_back({ "Hip-hop master", { { "target", 7 }, { "custom_lufs", -8 }, { "custom_tp", -1 } }, "CHAINS" });
    v.push_back({ "Rock master", { { "target", 7 }, { "custom_lufs", -9 }, { "custom_tp", -1 } }, "CHAINS" });
    v.push_back({ "Acoustic & jazz master", { { "target", 0 } }, "CHAINS" });
    v.push_back({ "Lo-fi master", { { "target", 7 }, { "custom_lufs", -12 }, { "custom_tp", -1 } }, "CHAINS" });
    v.push_back({ "Streaming master -14", { { "target", 0 } }, "CHAINS" });
}
