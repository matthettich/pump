// pump EQ: presets for sources and stems (vocals, drums, bass, guitars, keys/synths, buses).
// Shown under SOURCES in the preset menu. Included inside the plugin's namespace by EQParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
//
// Dynamic-band thresholds assume a source peaking around -6 dBFS (a healthy tracking level); for quieter or hotter
// tracks move THRESH by the same amount. Band fields: { band, shape, freq, gain, q, slope, place, dyn, range, thresh }
// shapes: 0 bell 1 low shelf 2 high shelf 3 low cut 4 high cut 5 notch 6 band pass 7 tilt; slope index 0=6 1=12
// 2=18 3=24 4=30 5=36 6=48 dB/oct.
#pragma once

inline void addSourcePresets(std::vector<Preset>& v)
{
    auto S = [&](const char* name, std::vector<B> bands, std::vector<PresetValue> extra = {}) {
        Preset p { name, {}, "SOURCES" };
        for (const B& b : bands) addBand(p, b);
        for (const auto& e : extra) p.values.push_back(e);
        v.push_back(p);
    };

    // --- vocals
    // Air without brighter esses: a broad 12 kHz shelf, and a dynamic 7 kHz band that only dips while an "s" is there.
    S("Vocals: air without the esses", {
        { 1, 3, 80, 0, 0.71f, 2, 0, 0, -6, -30 },
        { 2, 2, 12000, 3.0f, 0.71f, 1, 0, 0, -6, -30 },
        { 3, 0, 7000, 0, 1.6f, 1, 0, 1, -6, -26 } },
        { { "dyn_attack", 1 }, { "dyn_release", 60 } });
    // Chesty / boxy / nasal only when it builds up: dynamic cuts at 250 and 500 Hz, a fixed dip at the 1 kHz honk.
    S("Vocals: tame boxy & nasal", {
        { 1, 3, 90, 0, 0.71f, 2, 0, 0, -6, -30 },
        { 2, 0, 260, 0, 1.4f, 1, 0, 1, -3.5f, -26 },
        { 3, 0, 500, -1.5f, 1.8f, 1, 0, 1, -2.5f, -24 },
        { 4, 0, 1050, -1.5f, 2.5f, 1, 0, 0, -6, -30 },
        { 5, 0, 3800, 1.5f, 1.0f, 1, 0, 0, -6, -30 } });
    // Close-mic proximity and plosives: a steep 70 Hz cut and a dynamic low shelf that holds back the boom.
    S("Vocals: proximity & plosive control", {
        { 1, 3, 70, 0, 0.71f, 3, 0, 0, -6, -30 },
        { 2, 1, 220, 0, 0.71f, 1, 0, 1, -5, -28 } },
        { { "dyn_attack", 2 }, { "dyn_release", 90 } });
    // Backing vocals behind the lead: thin the bottom, step out of the lead's presence, a touch of air.
    S("Backing vocals: tuck behind the lead", {
        { 1, 3, 180, 0, 0.71f, 3, 0, 0, -6, -30 },
        { 2, 0, 350, -2.0f, 1.0f, 1, 0, 0, -6, -30 },
        { 3, 0, 3000, -2.5f, 0.8f, 1, 0, 0, -6, -30 },
        { 4, 2, 11000, 1.5f, 0.71f, 1, 0, 0, -6, -30 } });

    // --- drums
    S("Kick: sub weight, tight top", {
        { 1, 3, 30, 0, 0.71f, 6, 0, 0, -6, -30 },
        { 2, 0, 52, 3.5f, 1.1f, 1, 0, 0, -6, -30 },
        { 3, 0, 250, -3.0f, 1.0f, 1, 0, 0, -6, -30 },
        { 4, 4, 9000, 0, 0.71f, 1, 0, 0, -6, -30 } });
    S("Kick: beater click", {
        { 1, 3, 35, 0, 0.71f, 3, 0, 0, -6, -30 },
        { 2, 0, 420, -3.5f, 1.6f, 1, 0, 0, -6, -30 },
        { 3, 0, 2800, 4.0f, 1.6f, 1, 0, 0, -6, -30 },
        { 4, 2, 8000, 1.5f, 0.71f, 1, 0, 0, -6, -30 } });
    S("Snare: crack & body", {
        { 1, 3, 90, 0, 0.71f, 2, 0, 0, -6, -30 },
        { 2, 0, 200, 2.0f, 1.4f, 1, 0, 0, -6, -30 },
        { 3, 0, 550, -2.5f, 1.4f, 1, 0, 0, -6, -30 },
        { 4, 0, 5000, 3.0f, 0.9f, 1, 0, 0, -6, -30 } });
    // Drum bus: low cut, a dynamic 300 Hz cut that clears the build-up when everything hits, cymbals ducked when loud.
    S("Drums: bus clarity, tame cymbals", {
        { 1, 3, 28, 0, 0.71f, 3, 0, 0, -6, -30 },
        { 2, 0, 320, 0, 1.2f, 1, 0, 1, -3, -32 },
        { 3, 2, 8000, 0, 0.71f, 1, 0, 1, -4, -34 } },
        { { "dyn_attack", 3 }, { "dyn_release", 120 } });

    // --- bass
    S("Bass: tighten & growl", {
        { 1, 3, 25, 0, 0.71f, 1, 0, 0, -6, -30 },
        { 2, 0, 85, 0, 1.2f, 1, 0, 1, -3, -22 },
        { 3, 0, 260, -2.0f, 1.2f, 1, 0, 0, -6, -30 },
        { 4, 0, 900, 2.5f, 1.2f, 1, 0, 0, -6, -30 },
        { 5, 4, 6000, 0, 0.71f, 1, 0, 0, -6, -30 } },
        { { "dyn_attack", 15 }, { "dyn_release", 180 } });
    // Carve the kick's fundamental out of the bass and put the weight just above it.
    S("Bass: room for the kick", {
        { 1, 0, 55, -3.5f, 2.0f, 1, 0, 0, -6, -30 },
        { 2, 0, 110, 2.0f, 1.2f, 1, 0, 0, -6, -30 } });

    // --- guitars
    // Distorted guitars: weight at 140 Hz, a dynamic cut at the 2.5 kHz bite (only when it gets spiky), fizz rolled off.
    S("Guitars: thicken, de-fizz", {
        { 1, 3, 55, 0, 0.71f, 1, 0, 0, -6, -30 },
        { 2, 0, 140, 1.5f, 1.0f, 1, 0, 0, -6, -30 },
        { 3, 0, 2500, 0, 1.4f, 1, 0, 1, -3, -26 },
        { 4, 4, 8500, 0, 0.71f, 1, 0, 0, -6, -30 },
        { 5, 0, 5500, -2.0f, 1.5f, 1, 0, 0, -6, -30 } });
    // Wide double-tracked guitars around a centre vocal: dip the vocal's range in the mid only, sides stay full.
    S("Guitars: carve centre for vocal", {
        { 1, 3, 60, 0, 0.71f, 1, 0, 0, -6, -30 },
        { 2, 0, 2200, -3.0f, 0.9f, 1, kMid, 0, -6, -30 },
        { 3, 2, 3000, 1.0f, 0.71f, 1, kSide, 0, -6, -30 } });

    // --- keys / synths / pads
    S("Keys: clarity", {
        { 1, 3, 80, 0, 0.71f, 1, 0, 0, -6, -30 },
        { 2, 0, 320, -2.0f, 1.0f, 1, 0, 0, -6, -30 },
        { 3, 0, 1800, 1.0f, 0.9f, 1, 0, 0, -6, -30 },
        { 4, 2, 9000, 1.5f, 0.71f, 1, 0, 0, -6, -30 } });
    S("Synths: lead cut-through", {
        { 1, 3, 120, 0, 0.71f, 1, 0, 0, -6, -30 },
        { 2, 0, 400, -1.5f, 1.2f, 1, 0, 0, -6, -30 },
        { 3, 0, 2200, 2.0f, 1.0f, 1, 0, 0, -6, -30 },
        { 4, 2, 7000, 1.0f, 0.71f, 1, 0, 0, -6, -30 } });
    // Pads under a vocal: thin, a wide dip at the vocal's presence in the mid, top rolled off, sides left alone.
    S("Pads: tuck under a vocal", {
        { 1, 3, 200, 0, 0.71f, 2, 0, 0, -6, -30 },
        { 2, 0, 2800, -3.5f, 0.7f, 1, kMid, 0, -6, -30 },
        { 3, 4, 9000, 0, 0.71f, 1, 0, 0, -6, -30 },
        { 4, 1, 400, -1.5f, 0.71f, 1, 0, 0, -6, -30 } });

    // --- buses
    // Instrument / music bus that leaves room for the vocal: a gentle dynamic dip at 2-4 kHz when it piles up there.
    S("Bus: music bus, vocal pocket", {
        { 1, 3, 25, 0, 0.71f, 3, 0, 0, -6, -30 },
        { 2, 0, 3000, -1.0f, 0.8f, 1, kMid, 1, -2, -26 },
        { 3, 3, 100, 0, 0.71f, 1, kSide, 0, -6, -30 } });
}
