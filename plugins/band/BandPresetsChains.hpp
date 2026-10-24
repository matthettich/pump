// pump BAND: the matching mastering chains: the same names in every pump plugin, tuned to be used together.
// Shown under CHAINS in the preset menu. Included inside the plugin's namespace by BandParams.hpp, after the
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
//   Pop master:             4 bands RMS, gentle (1.4-2:1); low band holds the kick and bass steady
//   EDM master:             4 bands RMS; firmer low band (2.5:1, 10 ms) for a tight, even sub
//   Hip-hop master:         3 bands RMS; slow low band (45 ms attack) keeps the knock, light top control
//   Rock master:            4 bands RMS, gentle; a little firmer 1.2-6 kHz band for guitar bite
//   Acoustic & jazz master: 3 bands, linear-phase crossovers, 1.2-1.3:1 with 3 dB range: well under 1 dB
//   Lo-fi master:           3 bands; the top band (above 3.5 kHz) compresses most (2.5:1): softer, relaxed highs
//   Streaming master -14:   3 bands RMS, gentle all-round control (about 1 dB)
#pragma once

inline void addChainPresets(std::vector<Preset>& v)
{
    // BP { band, mode, thresh, ratio, attack, release, knee, range, makeup }; modes: 0 compress 1 expand 2 upward
    auto C = [&](const char* name, std::vector<PresetValue> globals, std::vector<BP> bands) {
        Preset p { name, std::move(globals), "CHAINS" };
        for (const BP& b : bands) addBand(p, b);
        v.push_back(p);
    };
    C("Pop master", { { "bands", 4 }, { "xover1", 120 }, { "xover2", 1000 }, { "xover3", 6000 }, { "detection", 1 }, { "out_gain", 1.3f } },
      { { 1, 0, -26, 2, 20, 200, 10, 6, 0 },
        { 2, 0, -33, 1.4f, 25, 180, 10, 5, 0 },
        { 3, 0, -39, 1.5f, 15, 140, 10, 5, 0 },
        { 4, 0, -43, 1.6f, 8, 100, 10, 6, 0 } });
    C("EDM master", { { "bands", 4 }, { "xover1", 120 }, { "xover2", 1000 }, { "xover3", 6000 }, { "detection", 1 }, { "out_gain", 1.6f } },
      { { 1, 0, -26, 2.5f, 10, 100, 8, 8, 0 },
        { 2, 0, -33, 1.6f, 20, 150, 10, 6, 0 },
        { 3, 0, -39, 1.6f, 10, 120, 10, 6, 0 },
        { 4, 0, -43, 1.8f, 5, 80, 10, 6, 0 } });
    C("Hip-hop master", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 4000 }, { "detection", 1 }, { "out_gain", 1.2f } },
      { { 1, 0, -24, 1.8f, 45, 200, 10, 6, 0 },
        { 2, 0, -33, 1.5f, 20, 160, 10, 5, 0 },
        { 3, 0, -41, 1.6f, 8, 100, 10, 6, 0 } });
    C("Rock master", { { "bands", 4 }, { "xover1", 120 }, { "xover2", 1200 }, { "xover3", 6000 }, { "detection", 1 }, { "out_gain", 1.2f } },
      { { 1, 0, -26, 1.8f, 30, 200, 10, 6, 0 },
        { 2, 0, -33, 1.5f, 20, 160, 10, 5, 0 },
        { 3, 0, -39, 1.8f, 8, 100, 10, 6, 0 },
        { 4, 0, -43, 1.6f, 5, 80, 10, 6, 0 } });
    C("Acoustic & jazz master", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 4000 }, { "detection", 1 }, { "xover_mode", 1 }, { "out_gain", 0.2f } },
      { { 1, 0, -22, 1.3f, 50, 300, 12, 3, 0 },
        { 2, 0, -30, 1.2f, 30, 250, 12, 3, 0 },
        { 3, 0, -40, 1.3f, 15, 200, 12, 3, 0 } });
    C("Lo-fi master", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 3500 }, { "detection", 1 }, { "out_gain", 0.8f } },
      { { 1, 0, -26, 1.5f, 40, 300, 12, 5, 0 },
        { 2, 0, -33, 1.4f, 30, 250, 12, 5, 0 },
        { 3, 0, -44, 2.5f, 10, 150, 10, 8, 0 } });
    C("Streaming master -14", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 2500 }, { "detection", 1 }, { "out_gain", 1.2f } },
      { { 1, 0, -26, 1.5f, 30, 200, 10, 6, 0 },
        { 2, 0, -33, 1.4f, 20, 160, 10, 6, 0 },
        { 3, 0, -39, 1.5f, 10, 120, 10, 6, 0 } });
}
