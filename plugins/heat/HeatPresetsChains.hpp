// pump HEAT: the matching mastering chains: the same names in every pump plugin, tuned to be used together.
// Shown under CHAINS in the preset menu. Included inside the plugin's namespace by HeatParams.hpp, after the
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

// one band's settings in a chain preset (BP is defined in HeatParams.hpp)
inline void addChainBand(Preset& p, const BP& b)
{
    static std::deque<std::string> pool;
    auto sym = [&](const char* f) -> const char* { pool.push_back("b" + std::to_string(b.band) + "_" + f); return pool.back().c_str(); };
    p.values.push_back({ sym("mode"), (float)b.mode });
    p.values.push_back({ sym("drive"), b.drive });
    p.values.push_back({ sym("mix"), b.mix });
    p.values.push_back({ sym("level"), b.level });
}

inline void addChainPresets(std::vector<Preset>& v)
{
    // BP { band, mode, drive, mix, level }; modes: 0 warm 1 tape 2 tube 3 triode 4 retro 5 clip. Auto gain stays on.
    auto C = [&](const char* name, std::vector<PresetValue> globals, std::vector<BP> bands) {
        Preset p { name, std::move(globals), "CHAINS" };
        for (const BP& b : bands) addChainBand(p, b);
        v.push_back(p);
    };
    C("Pop master", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 4000 } },
      { { 1, 1, 6, 60, 0 },
        { 2, 0, 3, 30, 0 },
        { 3, 2, 4, 30, 0 } });
    C("EDM master", { { "bands", 3 }, { "xover1", 120 }, { "xover2", 5000 } },
      { { 1, 5, 8, 100, 0 },
        { 2, 0, 4, 40, 0 },
        { 3, 2, 6, 40, 0 } });
    C("Hip-hop master", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 4000 } },
      { { 1, 5, 7, 100, 0.5f },
        { 2, 2, 5, 40, 0 },
        { 3, 0, 2, 25, 0 } });
    C("Rock master", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 4000 } },
      { { 1, 1, 8, 70, 0 },
        { 2, 3, 6, 50, 0 },
        { 3, 1, 4, 40, 0 } });
    C("Acoustic & jazz master", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 4000 }, { "mix", 50 } },
      { { 1, 1, 2, 25, 0 },
        { 2, 0, 1.5f, 20, 0 },
        { 3, 0, 1, 15, 0 } });
    C("Lo-fi master", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 4000 } },
      { { 1, 1, 10, 80, 0 },
        { 2, 1, 9, 75, 0 },
        { 3, 1, 8, 70, 0 } });
    C("Streaming master -14", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 3000 } },
      { { 1, 1, 3, 40, 0 },
        { 2, 0, 2, 30, 0 },
        { 3, 0, 2, 25, 0 } });
}
