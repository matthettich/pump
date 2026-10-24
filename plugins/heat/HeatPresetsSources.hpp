// pump HEAT: presets for sources and stems (vocals, drums, bass, guitars, keys/synths, buses).
// Shown under SOURCES in the preset menu. Included inside the plugin's namespace by HeatParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
#pragma once

// BP fields: band, mode (0 warm 1 tape 2 tube 3 triode 4 retro 5 clip), drive dB, band mix %, level dB.
// Every band the preset uses is listed (a band left out would keep the default 6 dB of Warm drive).
inline void addHeatSource(std::vector<Preset>& v, const char* name, std::vector<PresetValue> globals, std::vector<BP> bands)
{
    static const char* const f[4][4] = {
        { "b1_mode", "b1_drive", "b1_mix", "b1_level" }, { "b2_mode", "b2_drive", "b2_mix", "b2_level" },
        { "b3_mode", "b3_drive", "b3_mix", "b3_level" }, { "b4_mode", "b4_drive", "b4_mix", "b4_level" } };
    Preset p { name, std::move(globals), "SOURCES" };
    for (const BP& b : bands)
    {
        const int k = b.band - 1;
        p.values.push_back({ f[k][0], (float)b.mode });
        p.values.push_back({ f[k][1], b.drive });
        p.values.push_back({ f[k][2], b.mix });
        p.values.push_back({ f[k][3], b.level });
    }
    v.push_back(p);
}

inline void addSourcePresets(std::vector<Preset>& v)
{
    auto P = [&](const char* n, std::vector<PresetValue> g, std::vector<BP> b) { addHeatSource(v, n, std::move(g), std::move(b)); };
    P("Vocals: tape warmth", { { "bands", 3 }, { "xover1", 200 }, { "xover2", 3000 } },
      { { 1, 1, 5, 50, 0 }, { 2, 1, 8, 55, 0 }, { 3, 1, 3, 30, 0 } });
    // presence without extra sibilance: the 2-5 kHz band only, the "s" range above is left clean
    P("Vocals: presence exciter", { { "bands", 4 }, { "xover1", 250 }, { "xover2", 2000 }, { "xover3", 5000 } },
      { { 1, 0, 0, 0, 0 }, { 2, 0, 0, 0, 0 }, { 3, 3, 16, 45, 0.5f }, { 4, 0, 0, 0, 0 } });
    P("Backing vocals: soft & thick", { { "bands", 3 }, { "xover1", 250 }, { "xover2", 4000 } },
      { { 1, 0, 6, 40, 0 }, { 2, 0, 9, 50, 0 }, { 3, 0, 0, 0, -1.5f } });
    P("Drums: bus punch & clip", { { "bands", 3 }, { "xover1", 120 }, { "xover2", 5000 }, { "oversampling", 3 } },
      { { 1, 5, 12, 100, 0 }, { 2, 1, 8, 60, 0 }, { 3, 1, 4, 40, 0 } });
    // the 2nd/3rd harmonics of the sub make the kick read on small speakers; the click gets a little clip
    P("Kick: small-speaker weight", { { "bands", 3 }, { "xover1", 90 }, { "xover2", 2500 } },
      { { 1, 3, 12, 60, 0 }, { 2, 0, 4, 30, 0 }, { 3, 5, 6, 50, 0 } });
    P("Snare: crack", { { "bands", 3 }, { "xover1", 250 }, { "xover2", 2500 }, { "oversampling", 3 } },
      { { 1, 2, 6, 40, 0 }, { 2, 5, 8, 70, 0 }, { 3, 2, 10, 50, 0.5f } });
    P("Bass: growl, clean sub", { { "bands", 3 }, { "xover1", 120 }, { "xover2", 1500 } },
      { { 1, 0, 0, 0, 0 }, { 2, 2, 14, 75, 0 }, { 3, 5, 10, 40, 0 } });
    P("Guitars: tape thickness", { { "bands", 3 }, { "xover1", 150 }, { "xover2", 4000 } },
      { { 1, 1, 6, 60, 0 }, { 2, 1, 8, 70, 0 }, { 3, 1, 3, 30, 0 } });
    P("Keys: tape warmth", { { "bands", 2 }, { "xover1", 3000 } },
      { { 1, 1, 8, 60, 0 }, { 2, 1, 4, 40, -0.5f } });
    P("Synths: retro edge", { { "bands", 3 }, { "xover1", 200 }, { "xover2", 3000 } },
      { { 1, 0, 0, 0, 0 }, { 2, 4, 10, 50, 0 }, { 3, 2, 6, 30, 0 } });
    P("Bus: tube glue", { { "bands", 3 }, { "xover1", 120 }, { "xover2", 4000 } },
      { { 1, 2, 4, 30, 0 }, { 2, 2, 5, 30, 0 }, { 3, 2, 3, 20, 0 } });
}
