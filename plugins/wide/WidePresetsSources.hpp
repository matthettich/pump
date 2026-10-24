// pump WIDE: presets for sources and stems (vocals, drums, bass, guitars, keys/synths, buses).
// Shown under SOURCES in the preset menu. Included inside the plugin's namespace by WideParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
#pragma once

// widths and stereoize amounts per band (as many values as the preset uses), plus globals
inline void addWideSource(std::vector<Preset>& v, const char* name, std::vector<PresetValue> globals,
                          std::vector<float> w, std::vector<float> st = {})
{
    static const char* const ws[] = { "b1_width", "b2_width", "b3_width", "b4_width" };
    static const char* const ss[] = { "b1_stereoize", "b2_stereoize", "b3_stereoize", "b4_stereoize" };
    Preset p { name, std::move(globals), "SOURCES" };
    for (size_t b = 0; b < w.size() && b < 4; ++b) p.values.push_back({ ws[b], w[b] });
    for (size_t b = 0; b < st.size() && b < 4; ++b) p.values.push_back({ ss[b], st[b] });
    v.push_back(p);
}

inline void addSourcePresets(std::vector<Preset>& v)
{
    auto P = [&](const char* n, std::vector<PresetValue> g, std::vector<float> w, std::vector<float> st = {}) { addWideSource(v, n, std::move(g), std::move(w), std::move(st)); };
    // a lead vocal with stereo doubles / FX printed on it: lows mono, the body narrowed, the top left a little open
    P("Vocals: centred & focused", { { "bands", 3 }, { "xover1", 250 }, { "xover2", 5000 }, { "mono_bass", 1 }, { "mono_freq", 150 } }, { 40, 60, 85 });
    // works on mono stacks too (stereoize), keeps their low end in the middle with the lead
    P("Backing vocals: wide stack", { { "bands", 3 }, { "xover1", 300 }, { "xover2", 3000 }, { "mono_bass", 1 }, { "mono_freq", 180 } }, { 80, 145, 155 }, { 0, 50, 55 });
    P("Drums: wide overheads, mono kick", { { "bands", 4 }, { "xover1", 120 }, { "xover2", 800 }, { "xover3", 5000 }, { "mono_bass", 1 }, { "mono_freq", 110 }, { "mono_slope", 1 } }, { 100, 95, 125, 140 });
    P("Kick & snare: dead centre", { { "bands", 3 }, { "xover1", 200 }, { "xover2", 5000 } }, { 0, 30, 70 });
    P("Bass: mono lows, wide growl", { { "bands", 2 }, { "xover1", 500 }, { "mono_bass", 1 }, { "mono_freq", 140 }, { "mono_slope", 1 } }, { 0, 115 }, { 0, 30 });
    P("Guitars: wide double-track", { { "bands", 3 }, { "xover1", 200 }, { "xover2", 4000 } }, { 75, 135, 120 });
    P("Keys: wide pad", { { "bands", 3 }, { "xover1", 250 }, { "xover2", 2500 }, { "mono_bass", 1 }, { "mono_freq", 150 } }, { 85, 120, 135 }, { 0, 15, 25 });
    // supersaws and chorused synths that are already wide and fall apart in mono
    P("Synths: tame an over-wide synth", { { "bands", 3 }, { "xover1", 300 }, { "xover2", 3000 }, { "mono_bass", 1 }, { "mono_freq", 200 } }, { 30, 75, 95 });
    // leave the middle to the vocal: the instruments' side opens through the vocal's presence range
    P("Bus: make room for the vocal", { { "bands", 4 }, { "xover1", 120 }, { "xover2", 1000 }, { "xover3", 5000 }, { "mono_bass", 1 }, { "mono_freq", 100 } }, { 90, 110, 130, 115 });
}
