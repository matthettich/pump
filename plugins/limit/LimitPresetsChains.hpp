// pump LIMIT: the matching mastering chains: the same names in every pump plugin, tuned to be used together.
// Shown under CHAINS in the preset menu. Included inside the plugin's namespace by LimitParams.hpp, after the
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
//   Pop master:             Aggressive (soft clip + brickwall), 4x, +15.5 dB, -1 dBTP; target display -9
//   EDM master:             Aggressive, 4x, fast (2 ms / 50 ms), +17.4 dB, -1 dBTP; target display -7.5
//   Hip-hop master:         Aggressive, 4x, 40% attack / 60 ms, +16 dB, -1 dBTP; target display -8
//   Rock master:            Aggressive, 4x, 70 ms release, +14.8 dB, -1 dBTP; target display -9
//   Acoustic & jazz master: Dynamic, slow (6 ms / 250 ms), +11.2 dB, -1 dBTP; target Streaming -14
//   Lo-fi master:           Transparent, slow (6 ms / 200 ms), +14.7 dB, -1 dBTP; target display -12
//   Streaming master -14:   Transparent, +10.1 dB, -1 dBTP; target Streaming -14
#pragma once

inline void addChainPresets(std::vector<Preset>& v)
{
    v.push_back({ "Pop master", { { "gain", 15.5f }, { "ceiling", -1 }, { "style", 4 }, { "oversampling", 2 }, { "release", 60 }, { "target", 6 }, { "target_custom", -9 }, { "attack", 40 }, { "lookahead", 3 } }, "CHAINS" });
    v.push_back({ "EDM master", { { "gain", 17.4f }, { "ceiling", -1 }, { "style", 4 }, { "oversampling", 2 }, { "lookahead", 2 }, { "attack", 30 }, { "release", 50 }, { "target", 6 }, { "target_custom", -7.5f } }, "CHAINS" });
    v.push_back({ "Hip-hop master", { { "gain", 16 }, { "ceiling", -1 }, { "style", 4 }, { "oversampling", 2 }, { "lookahead", 3 }, { "attack", 40 }, { "release", 60 }, { "target", 6 }, { "target_custom", -8 } }, "CHAINS" });
    v.push_back({ "Rock master", { { "gain", 14.8f }, { "ceiling", -1 }, { "style", 4 }, { "oversampling", 2 }, { "release", 70 }, { "target", 6 }, { "target_custom", -9 }, { "lookahead", 3 } }, "CHAINS" });
    v.push_back({ "Acoustic & jazz master", { { "gain", 11.2f }, { "ceiling", -1 }, { "style", 2 }, { "lookahead", 6 }, { "attack", 70 }, { "release", 250 }, { "target", 1 } }, "CHAINS" });
    v.push_back({ "Lo-fi master", { { "gain", 14.7f }, { "ceiling", -1 }, { "style", 0 }, { "lookahead", 6 }, { "attack", 60 }, { "release", 200 }, { "target", 6 }, { "target_custom", -12 } }, "CHAINS" });
    v.push_back({ "Streaming master -14", { { "gain", 10.1f }, { "ceiling", -1 }, { "style", 0 }, { "lookahead", 5 }, { "attack", 60 }, { "release", 160 }, { "target", 1 } }, "CHAINS" });
}
