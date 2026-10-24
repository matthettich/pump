// pump EQ: the matching mastering chains: the same names in every pump plugin, tuned to be used together.
// Shown under CHAINS in the preset menu. Included inside the plugin's namespace by EQParams.hpp, after the
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
//   Pop master:             25 Hz cut, light low shelf, -1 dB at 320 Hz, dynamic 3.2 kHz tamer, +1.2 dB air shelf
//   EDM master:             steep 28 Hz cut, +1 dB sub bell at 55 Hz, -1.5 dB mud cut, dynamic 2.8 kHz tamer, +1.2 dB top shelf
//   Hip-hop master:         22 Hz cut that keeps the sub, +1.5 dB low shelf at 75 Hz, -1.2 dB at 260 Hz, a touch of presence and air
//   Rock master:            30 Hz cut, -1 dB box at 450 Hz, +1 dB broad 1.6 kHz bell (midrange energy), small top shelf
//   Acoustic & jazz master: gentle 20 Hz cut, -0.6 dB at 250 Hz, +0.8 dB wide air shelf: almost transparent
//   Lo-fi master:           35 Hz cut, +1 dB warmth at 200 Hz, -0.8 dB at 3 kHz, -1.5 dB high shelf at 9 kHz: slightly darker
//   Streaming master -14:   25 Hz cut, +0.5 dB low shelf, -0.8 dB at 320 Hz, +1.2 dB air shelf (gentle polish)
#pragma once

inline void addChainPresets(std::vector<Preset>& v)
{
    // B { band, shape, freq, gain, q, slope, place, dyn, range, thresh }
    // shapes: 0 bell 1 low shelf 2 high shelf 3 low cut 4 high cut; slope index: 0=6 1=12 3=24 4=30 6=48 dB/oct
    auto C = [&](const char* name, std::vector<B> bands) {
        Preset p { name, {}, "CHAINS" };
        for (const B& b : bands) addBand(p, b);
        v.push_back(p);
    };
    C("Pop master", { { 1, 3, 25, 0, 0.71f, 3, 0, 0, -6, -30 },
        { 2, 1, 95, 0.6f, 0.71f, 1, 0, 0, -6, -30 },
        { 3, 0, 320, -1, 0.9f, 1, 0, 0, -6, -30 },
        { 4, 0, 3200, 0, 1.4f, 1, 0, 1, -1.5f, -34 },
        { 5, 2, 11000, 0.8f, 0.71f, 1, 0, 0, -6, -30 } });
    C("EDM master", { { 1, 3, 28, 0, 0.71f, 6, 0, 0, -6, -30 },
        { 2, 0, 55, 1, 1, 1, 0, 0, -6, -30 },
        { 3, 0, 380, -1.5f, 1, 1, 0, 0, -6, -30 },
        { 4, 0, 2800, 0, 1.4f, 1, 0, 1, -2, -34 },
        { 5, 2, 10000, 0.8f, 0.71f, 1, 0, 0, -6, -30 } });
    C("Hip-hop master", { { 1, 3, 22, 0, 0.71f, 4, 0, 0, -6, -30 },
        { 2, 1, 75, 1.5f, 0.71f, 1, 0, 0, -6, -30 },
        { 3, 0, 260, -1.2f, 1, 1, 0, 0, -6, -30 },
        { 4, 0, 4500, 0.8f, 0.8f, 1, 0, 0, -6, -30 },
        { 5, 2, 12000, 1.2f, 0.71f, 1, 0, 0, -6, -30 } });
    C("Rock master", { { 1, 3, 30, 0, 0.71f, 3, 0, 0, -6, -30 },
        { 2, 0, 110, 0.6f, 0.9f, 1, 0, 0, -6, -30 },
        { 3, 0, 450, -1, 1, 1, 0, 0, -6, -30 },
        { 4, 0, 1600, 1.5f, 0.7f, 1, 0, 0, -6, -30 },
        { 5, 2, 10000, 0.8f, 0.71f, 1, 0, 0, -6, -30 } });
    C("Acoustic & jazz master", { { 1, 3, 20, 0, 0.71f, 1, 0, 0, -6, -30 },
        { 2, 0, 250, -0.6f, 0.8f, 1, 0, 0, -6, -30 },
        { 3, 2, 12000, 0.8f, 0.6f, 1, 0, 0, -6, -30 } });
    C("Lo-fi master", { { 1, 3, 35, 0, 0.71f, 1, 0, 0, -6, -30 },
        { 2, 0, 200, 1, 0.7f, 1, 0, 0, -6, -30 },
        { 3, 0, 3000, -0.8f, 0.8f, 1, 0, 0, -6, -30 },
        { 4, 2, 9000, -1.5f, 0.71f, 1, 0, 0, -6, -30 } });
    C("Streaming master -14", { { 1, 3, 25, 0, 0.71f, 3, 0, 0, -6, -30 },
        { 2, 1, 90, 0.5f, 0.71f, 1, 0, 0, -6, -30 },
        { 3, 0, 320, -0.8f, 0.9f, 1, 0, 0, -6, -30 },
        { 4, 2, 11000, 1.2f, 0.71f, 1, 0, 0, -6, -30 } });
}
