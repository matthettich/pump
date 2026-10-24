// pump COMP: presets for sources and stems (vocals, drums, bass, guitars, keys/synths, buses).
// Shown under SOURCES in the preset menu. Included inside the plugin's namespace by CompParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
//
// Thresholds assume a source peaking around -6 dBFS; for quieter or hotter tracks move THRESH by the same amount.
// styles: 0 Clean 1 Glue 2 Opto 3 Punch 4 Vari-Mu 5 Pump; channel_mode: 0 St 1 M/S 2 Mid 3 Side; oversampling 0/1/2
#pragma once

inline void addSourcePresets(std::vector<Preset>& v)
{
    auto S = [&](const char* name, std::vector<PresetValue> values) { v.push_back({ name, values, "SOURCES" }); };

    // --- vocals
    // First in a two-stage vocal chain: a fast, 1176-ish catcher that only touches the spikes (2-5 dB).
    S("Vocals: fast peak catcher", { { "style", 0 }, { "threshold", -16 }, { "ratio", 5 }, { "knee", 6 }, { "attack", 0.8f },
        { "release", 50 }, { "range", 8 }, { "sc_hpf", 100 }, { "out_gain", 1 } });
    // Second stage: slow optical leveling that rides the phrases (3-6 dB), the HPF keeps low notes from pulling it.
    S("Vocals: opto smooth rider", { { "style", 2 }, { "threshold", -28 }, { "ratio", 3 }, { "knee", 12 }, { "attack", 15 },
        { "release", 450 }, { "auto_release", 1 }, { "sc_hpf", 120 }, { "out_gain", 4.5f } });
    // Up-front rock / rap vocal: fast attack, high ratio, all the way in your face.
    S("Vocals: aggressive upfront", { { "style", 0 }, { "threshold", -26 }, { "ratio", 8 }, { "knee", 3 }, { "attack", 0.3f },
        { "release", 60 }, { "auto_release", 1 }, { "sc_hpf", 150 }, { "oversampling", 1 }, { "out_gain", 8 } });
    S("Backing vocals: squash & sit", { { "style", 0 }, { "threshold", -26 }, { "ratio", 6 }, { "knee", 6 }, { "attack", 3 },
        { "release", 90 }, { "auto_release", 1 }, { "sc_hpf", 150 }, { "out_gain", 6.5f } });

    // --- drums
    // SSL-style bus glue: 4:1, 30 ms attack lets the transients through, auto release, the HPF stops the kick pumping it.
    S("Drums: bus glue", { { "style", 1 }, { "threshold", -22 }, { "ratio", 4 }, { "knee", 6 }, { "attack", 30 },
        { "release", 100 }, { "auto_release", 1 }, { "range", 6 }, { "sc_hpf", 90 }, { "out_gain", 1.5f } });
    // New York crush for a parallel blend: flattened hard (the HPF crushes the snare and room, not the kick), mixed in.
    S("Drums: parallel crush", { { "style", 1 }, { "threshold", -30 }, { "ratio", 20 }, { "knee", 0 }, { "attack", 0.1f },
        { "release", 50 }, { "mix", 25 }, { "sc_hpf", 100 }, { "oversampling", 1 }, { "auto_makeup", 1 } });
    // Feedback punch: the 15 ms attack keeps the click, the reduction shortens the tail.
    S("Kick: punch & short tail", { { "style", 3 }, { "threshold", -16 }, { "ratio", 4 }, { "knee", 3 }, { "attack", 15 },
        { "release", 60 }, { "out_gain", 2 } });
    // Fast attack and a longer release: the crack is shaved, the body and ring come up (complements Snare snap).
    S("Snare: fatten body", { { "style", 0 }, { "threshold", -16 }, { "ratio", 4 }, { "knee", 6 }, { "attack", 0.5f },
        { "release", 140 }, { "out_gain", 5 } });

    // --- bass
    S("Bass: even out (opto)", { { "style", 2 }, { "threshold", -14 }, { "ratio", 4 }, { "knee", 9 }, { "attack", 15 },
        { "release", 220 }, { "out_gain", 3.5f } });
    // Route the kick to the sidechain: the bass dips 3-6 dB under each kick and is back for the next note.
    S("Bass: ducked by kick (ext SC)", { { "style", 0 }, { "sc_source", 1 }, { "threshold", -32 }, { "ratio", 6 }, { "knee", 6 },
        { "attack", 1 }, { "release", 90 }, { "range", 5 }, { "sc_lpf", 1000 } });

    // --- guitars, keys
    S("Guitars: tighten distorted", { { "style", 0 }, { "threshold", -14 }, { "ratio", 3 }, { "knee", 9 }, { "attack", 8 },
        { "release", 80 }, { "range", 6 }, { "sc_hpf", 150 }, { "out_gain", 3.5f } });
    S("Guitars: acoustic strum even", { { "style", 2 }, { "threshold", -18 }, { "ratio", 3 }, { "knee", 12 }, { "attack", 20 },
        { "release", 200 }, { "sc_hpf", 100 }, { "out_gain", 4 } });
    // Piano / keys: slow enough to keep the hammer, a soft 2.5:1 that evens the left hand against the right.
    S("Keys: gentle even", { { "style", 0 }, { "threshold", -18 }, { "ratio", 2.5f }, { "knee", 12 }, { "attack", 20 },
        { "release", 160 }, { "auto_release", 1 }, { "sc_hpf", 80 }, { "out_gain", 3.5f } });
    // Sustained synths and pads: slow, round vari-mu that holds them steady under the vocal.
    S("Synths: vari-mu hold", { { "style", 4 }, { "threshold", -18 }, { "ratio", 3 }, { "knee", 12 }, { "attack", 40 },
        { "release", 500 }, { "oversampling", 1 }, { "out_gain", 4 } });

    // --- buses
    S("Bus: instrument bus glue", { { "style", 1 }, { "threshold", -20 }, { "ratio", 4 }, { "knee", 6 }, { "attack", 30 },
        { "release", 200 }, { "auto_release", 1 }, { "range", 4 }, { "sc_hpf", 100 }, { "out_gain", 2 } });
}
