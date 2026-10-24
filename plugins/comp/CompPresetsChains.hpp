// pump COMP: the matching mastering chains: the same names in every pump plugin, tuned to be used together.
// Shown under CHAINS in the preset menu. Included inside the plugin's namespace by CompParams.hpp, after the
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
    v.push_back({ "Pop master", { { "style", 1 }, { "threshold", -22 }, { "ratio", 2 }, { "knee", 12 }, { "attack", 20 }, { "release", 200 }, { "auto_release", 1 }, { "range", 8 }, { "sc_hpf", 60 }, { "out_gain", 1.6f } }, "CHAINS" });
    v.push_back({ "EDM master", { { "style", 1 }, { "threshold", -23 }, { "ratio", 3 }, { "knee", 10 }, { "attack", 10 }, { "release", 150 }, { "auto_release", 1 }, { "range", 8 }, { "sc_hpf", 90 }, { "out_gain", 2.8f } }, "CHAINS" });
    v.push_back({ "Hip-hop master", { { "style", 3 }, { "threshold", -22 }, { "ratio", 2.5f }, { "knee", 8 }, { "attack", 25 }, { "release", 120 }, { "auto_release", 1 }, { "range", 8 }, { "sc_hpf", 80 }, { "out_gain", 1.6f } }, "CHAINS" });
    v.push_back({ "Rock master", { { "style", 1 }, { "threshold", -21 }, { "ratio", 3 }, { "knee", 8 }, { "attack", 10 }, { "release", 200 }, { "auto_release", 1 }, { "range", 8 }, { "sc_hpf", 60 }, { "out_gain", 2.8f } }, "CHAINS" });
    v.push_back({ "Acoustic & jazz master", { { "style", 2 }, { "threshold", -20 }, { "ratio", 1.5f }, { "knee", 18 }, { "attack", 20 }, { "release", 400 }, { "range", 4 }, { "out_gain", 0.8f } }, "CHAINS" });
    v.push_back({ "Lo-fi master", { { "style", 4 }, { "threshold", -18 }, { "ratio", 2 }, { "knee", 18 }, { "attack", 25 }, { "release", 500 }, { "range", 8 }, { "oversampling", 1 }, { "out_gain", 2.4f } }, "CHAINS" });
    v.push_back({ "Streaming master -14", { { "style", 1 }, { "threshold", -20 }, { "ratio", 1.8f }, { "knee", 12 }, { "attack", 30 }, { "release", 300 }, { "auto_release", 1 }, { "range", 6 }, { "sc_hpf", 50 }, { "out_gain", 1.1f } }, "CHAINS" });
}
