// pump BAND: presets for sources and stems (vocals, drums, bass, guitars, keys/synths, buses).
// Shown under SOURCES in the preset menu. Included inside the plugin's namespace by BandParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
#pragma once

// Thresholds assume a stem peaking around -6 dBFS; adjust THRESH to taste on hotter or quieter tracks.
// BP fields: band, mode (0 compress 1 expand 2 upward), thresh, ratio, attack, release, knee, range, makeup.
inline void addSourcePreset(std::vector<Preset>& v, const char* name, std::vector<PresetValue> globals, std::vector<BP> bands)
{
    Preset p { name, std::move(globals), "SOURCES" };
    for (const BP& b : bands) addBand(p, b);
    v.push_back(p);
}

inline void addSourcePresets(std::vector<Preset>& v)
{
    auto P = [&](const char* n, std::vector<PresetValue> g, std::vector<BP> b) { addSourcePreset(v, n, std::move(g), std::move(b)); };
    P("Vocals: proximity & plosives", { { "bands", 3 }, { "xover1", 120 }, { "xover2", 400 } },
      { { 1, 0, -42, 4.0f, 1, 60, 6, 12, 0.0f }, { 2, 0, -30, 2.5f, 15, 140, 8, 6, 0.0f } });
    P("Vocals: de-ess 5-10k (keeps air)", { { "bands", 4 }, { "xover1", 120 }, { "xover2", 4800 }, { "xover3", 10000 }, { "lookahead", 2 } },
      { { 3, 0, -36, 8.0f, 0.5f, 50, 3, 10, 0.0f } });
    P("Backing vocals: dense & tucked", { { "bands", 3 }, { "xover1", 250 }, { "xover2", 4000 }, { "detection", 1 } },
      { { 1, 0, -40, 2.5f, 15, 150, 8, 6, 0.0f }, { 2, 0, -34, 2.5f, 10, 120, 8, 6, 1.5f }, { 3, 0, -50, 3.0f, 2, 70, 6, 8, 0.0f } });
    P("Drums: bus control", { { "bands", 3 }, { "xover1", 120 }, { "xover2", 5000 } },
      { { 1, 0, -22, 3.0f, 30, 120, 6, 6, 1.0f }, { 2, 0, -36, 2.5f, 10, 90, 6, 6, 1.0f }, { 3, 0, -36, 3.0f, 2, 60, 6, 6, 0.0f } });
    P("Kick: tighten the tail", { { "bands", 2 }, { "xover1", 150 } },
      { { 1, 1, -18, 2.5f, 0.5f, 40, 6, 12, 0.0f } });
    P("Snare: shorter ring, softer spikes", { { "bands", 3 }, { "xover1", 300 }, { "xover2", 3000 } },
      { { 1, 1, -30, 2.0f, 0.5f, 60, 6, 10, 0.0f }, { 3, 0, -26, 3.0f, 1, 60, 6, 5, 0.0f } });
    P("Bass: even notes, steady growl", { { "bands", 3 }, { "xover1", 100 }, { "xover2", 400 }, { "detection", 1 } },
      { { 1, 0, -14, 3.0f, 30, 150, 6, 6, 1.0f }, { 2, 0, -26, 2.5f, 10, 100, 6, 6, 1.0f } });
    P("Guitars: boom, mud & fizz", { { "bands", 4 }, { "xover1", 150 }, { "xover2", 500 }, { "xover3", 4000 } },
      { { 1, 0, -14, 3.0f, 5, 80, 6, 6, 0.0f }, { 2, 0, -12, 2.5f, 15, 120, 6, 5, 0.0f }, { 4, 0, -44, 3.0f, 2, 60, 6, 6, 0.0f } });
    P("Keys: upward detail lift", { { "bands", 3 }, { "xover1", 250 }, { "xover2", 3000 }, { "detection", 1 } },
      { { 2, 2, -16, 2.0f, 20, 300, 10, 6, 0.0f }, { 3, 2, -26, 2.0f, 15, 250, 10, 8, 0.0f } });
    P("Bus: low-mid build-up", { { "bands", 4 }, { "xover1", 180 }, { "xover2", 600 }, { "xover3", 4000 }, { "detection", 1 } },
      { { 2, 0, -32, 2.5f, 20, 150, 8, 6, 0.5f } });
}
