// pump LIMIT: presets for sources and stems (vocals, drums, bass, guitars, keys/synths, buses).
// Shown under SOURCES in the preset menu. Included inside the plugin's namespace by LimitParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
//
// GAIN assumes a source peaking around -6 dBFS (it sets how hard the limiter works: about GAIN - 5 dB of reduction on
// the loudest peaks at a -1 dB ceiling). Loudness targets are a mastering display: off for stems.
#pragma once

inline void addSourcePresets(std::vector<Preset>& v)
{
    auto S = [&](const char* name, std::vector<PresetValue> values) { v.push_back({ name, values, "SOURCES" }); };

    // --- vocals
    // After the vocal compressors: catches the odd spike, slow and linked so it never grabs a consonant audibly.
    S("Vocals: transparent peak safety", { { "gain", 6 }, { "ceiling", -1 }, { "style", kTransparent }, { "lookahead", 5 },
        { "attack", 70 }, { "release", 150 }, { "link_transients", 100 }, { "oversampling", 2 }, { "target", 0 } });
    // Rap / pop lead pushed into a denser, steadier level: modern style, fast release.
    S("Vocals: upfront density", { { "gain", 11 }, { "ceiling", -1 }, { "style", kModern }, { "lookahead", 3 },
        { "attack", 40 }, { "release", 60 }, { "link_transients", 100 }, { "oversampling", 2 }, { "target", 0 } });

    // --- drums
    // Shave the loudest hits without blunting them: Dynamic keeps the transient shape, short lookahead, fast release.
    S("Drums: transient-safe shave", { { "gain", 8 }, { "ceiling", -1 }, { "style", kDynamic }, { "lookahead", 1 },
        { "attack", 20 }, { "release", 30 }, { "link_transients", 50 }, { "oversampling", 2 }, { "target", 0 } });
    S("Snare: tame spikes", { { "gain", 8 }, { "ceiling", -1 }, { "style", kPunchy }, { "lookahead", 0.5f },
        { "attack", 10 }, { "release", 20 }, { "oversampling", 2 }, { "target", 0 } });

    // --- bass
    // Long lookahead and release: holds the peaks of low notes without the waveform distortion of a fast limiter.
    S("Bass: clean peak control", { { "gain", 8 }, { "ceiling", -1 }, { "style", kTransparent }, { "lookahead", 10 },
        { "attack", 80 }, { "release", 400 }, { "link_transients", 100 }, { "link_release", 100 }, { "target", 0 } });
    // 808 / sub bass: the Aggressive soft clip adds harmonics that carry on small speakers, and holds a solid level.
    S("Bass: 808 grit & hold", { { "gain", 8 }, { "ceiling", -1 }, { "style", kAggressive }, { "lookahead", 4 },
        { "attack", 40 }, { "release", 120 }, { "link_transients", 100 }, { "oversampling", 2 }, { "target", 0 } });

    // --- guitars, keys, synths
    S("Guitars: even wall", { { "gain", 8 }, { "ceiling", -1 }, { "style", kModern }, { "lookahead", 4 },
        { "attack", 50 }, { "release", 90 }, { "oversampling", 1 }, { "target", 0 } });
    S("Synths: pluck & lead peaks", { { "gain", 8 }, { "ceiling", -1 }, { "style", kPunchy }, { "lookahead", 2 },
        { "attack", 30 }, { "release", 50 }, { "oversampling", 2 }, { "target", 0 } });
    S("Keys: gentle level hold", { { "gain", 7 }, { "ceiling", -1 }, { "style", kTransparent }, { "lookahead", 6 },
        { "attack", 70 }, { "release", 250 }, { "oversampling", 1 }, { "target", 0 } });

    // --- buses
    // Drum + bass submix: punchy, transients mostly unlinked so a snare hit doesn't duck the other side.
    S("Bus: drum & bass submix", { { "gain", 7 }, { "ceiling", -1 }, { "style", kPunchy }, { "lookahead", 2 },
        { "attack", 30 }, { "release", 50 }, { "link_transients", 40 }, { "oversampling", 2 }, { "target", 0 } });
    S("Bus: music bus density", { { "gain", 8 }, { "ceiling", -1 }, { "style", kAllround }, { "lookahead", 4 },
        { "attack", 50 }, { "release", 120 }, { "oversampling", 2 }, { "target", 0 } });
    // Printing / exporting stems: does nothing until a peak would pass -0.3 dBTP, then catches it transparently.
    S("Bus: stem print safety", { { "gain", 0 }, { "ceiling", -0.3f }, { "style", kTransparent }, { "lookahead", 5 },
        { "attack", 60 }, { "release", 200 }, { "true_peak", 1 }, { "oversampling", 2 }, { "target", 0 } });
}
