// pump CALM: presets for sources and stems (vocals, drums, bass, guitars, keys/synths, buses).
// Shown under SOURCES in the preset menu. Included inside the plugin's namespace by CalmParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
//
// Sensitivity nodes: s1 low shelf, s2..s5 bells, s6 high shelf (freq / gain / q). Focus Low/High limit where it works.
#pragma once

inline void addSourcePresets(std::vector<Preset>& v)
{
    auto S = [&](const char* name, std::vector<PresetValue> values) { v.push_back({ name, values, "SOURCES" }); };

    // --- vocals
    // Harsh 2-5 kHz build-ups on loud notes: moderate, smooth, leaves the sibilance region to a de-esser.
    S("Vocals: de-harsh 2-5k", { { "depth", 45 }, { "sharpness", 60 }, { "selectivity", 50 }, { "attack", 5 }, { "release", 100 },
        { "focus_low", 1500 }, { "focus_high", 8000 }, { "s4_freq", 3500 }, { "s4_gain", 3 }, { "s4_q", 0.8f } });
    // Esses: broad and fast (sibilance is noise, not a narrow peak), hard mode pulls the burst down onto the line.
    S("Vocals: sibilance 5-10k (fast)", { { "depth", 75 }, { "sharpness", 35 }, { "selectivity", 35 }, { "attack", 1 },
        { "release", 45 }, { "mode", 1 }, { "focus_low", 4500 }, { "focus_high", 12000 }, { "s5_freq", 7000 }, { "s5_gain", 4 },
        { "s5_q", 0.7f } });
    S("Backing vocals: soften", { { "depth", 40 }, { "sharpness", 40 }, { "selectivity", 40 }, { "attack", 6 }, { "release", 150 },
        { "focus_low", 1000 }, { "focus_high", 12000 }, { "s4_freq", 3000 }, { "s4_gain", 2 }, { "s6_freq", 8000 }, { "s6_gain", 2 } });

    // --- drums
    S("Drums: cymbal & hat tame", { { "depth", 60 }, { "sharpness", 45 }, { "selectivity", 40 }, { "attack", 2 }, { "release", 60 },
        { "focus_low", 5000 }, { "focus_high", 18000 }, { "s6_freq", 10000 }, { "s6_gain", 3 } });
    // Snare and tom ring: narrow, hard, a long release so the ringing tail stays down.
    S("Snare: ring control", { { "depth", 50 }, { "sharpness", 80 }, { "selectivity", 45 }, { "attack", 3 }, { "release", 150 },
        { "mode", 1 }, { "focus_low", 150 }, { "focus_high", 1500 }, { "quality", 2 } });
    S("Kick: boxy ring", { { "depth", 50 }, { "sharpness", 70 }, { "selectivity", 45 }, { "attack", 4 }, { "release", 120 },
        { "focus_low", 150 }, { "focus_high", 800 }, { "s2_freq", 350 }, { "s2_gain", 2 }, { "quality", 2 } });

    // --- bass
    // Booming notes and room modes: 8192 resolution for the low end, slow enough to leave the attack of each note.
    S("Bass: boom & note jumps", { { "depth", 45 }, { "sharpness", 60 }, { "selectivity", 45 }, { "attack", 20 }, { "release", 200 },
        { "focus_low", 30 }, { "focus_high", 300 }, { "quality", 3 } });

    // --- guitars
    S("Guitars: fizz & harsh", { { "depth", 45 }, { "sharpness", 55 }, { "selectivity", 45 }, { "attack", 4 }, { "release", 120 },
        { "focus_low", 2000 }, { "focus_high", 10000 }, { "s4_freq", 3000 }, { "s4_gain", 2 }, { "s5_freq", 6000 }, { "s5_gain", 3 } });

    // --- keys / synths / pads
    // Resonant synth filters and ringing leads: sharp and hard, high resolution, follows the peak as it moves. Focus starts
    // at 1 kHz: below that, the fundamentals of a mono lead would read as "resonances" too.
    S("Synths: resonant peaks", { { "depth", 60 }, { "sharpness", 70 }, { "selectivity", 60 }, { "attack", 4 }, { "release", 150 },
        { "mode", 1 }, { "focus_low", 1000 }, { "focus_high", 12000 }, { "quality", 2 } });
    S("Keys: tame ringing", { { "depth", 55 }, { "sharpness", 75 }, { "selectivity", 60 }, { "attack", 8 }, { "release", 200 },
        { "focus_low", 1200 }, { "focus_high", 8000 }, { "quality", 2 } });
    // Pads under a vocal: soft and slow, more sensitive where the vocal lives so the pad gives way there.
    S("Pads: soften upper mids", { { "depth", 40 }, { "sharpness", 40 }, { "selectivity", 45 }, { "attack", 20 }, { "release", 250 },
        { "focus_low", 1500 }, { "focus_high", 12000 }, { "s4_freq", 3000 }, { "s4_gain", 3 }, { "s4_q", 0.7f } });

    // --- buses
    // Mix or music bus: gentle, mid only, in the vocal range (the sides keep their sparkle).
    S("Bus: gentle mid smoothing", { { "depth", 30 }, { "sharpness", 55 }, { "selectivity", 55 }, { "attack", 12 }, { "release", 180 },
        { "stereo", 2 }, { "balance", -100 }, { "focus_low", 800 }, { "focus_high", 9000 }, { "s4_freq", 3000 }, { "s4_gain", 2 } });
}
