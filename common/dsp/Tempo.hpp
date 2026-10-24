// pump DSP: host tempo and note divisions for the tempo-synced effects (ECHO, GATE, GRAIN, ...). Framework-free:
// PumpPlugin fills a Transport from the host's time position; engines only see this struct.
#pragma once

#include "Util.hpp"

namespace pump {

// The host's transport at the start of a block. `ppq` is the position in quarter notes (valid only when `valid`);
// `barQuarters` is the length of a bar in quarter notes (4 in 4/4, 3 in 6/8).
struct Transport {
    double bpm = 120.0;
    double ppq = 0.0;
    double barQuarters = 4.0;
    bool playing = false;
    bool valid = false;
};

// Note divisions, shortest first (T = triplet, D = dotted). Values in quarter notes.
static const char* const kNoteNames[] = {
    "1/64", "1/32T", "1/32", "1/32D", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
    "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1", "2/1"
};
constexpr int kNumNotes = 18;
enum NoteIndex : int {
    kNote64 = 0, kNote32T, kNote32, kNote32D, kNote16T, kNote16, kNote16D, kNote8T, kNote8, kNote8D,
    kNote4T, kNote4, kNote4D, kNote2T, kNote2, kNote2D, kNote1, kNote2Bars
};

inline double noteQuarters(int i)
{
    static const double q[kNumNotes] = {
        1.0 / 16.0, 1.0 / 12.0, 1.0 / 8.0, 3.0 / 16.0, 1.0 / 6.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 3.0, 1.0 / 2.0, 3.0 / 4.0,
        2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0, 8.0
    };
    return q[clampv(i, 0, kNumNotes - 1)];
}
// A note division at a tempo, in seconds (the tempo is kept to a sane 20..999 BPM).
inline double noteSeconds(int i, double bpm) { return noteQuarters(i) * 60.0 / clampv(bpm, 20.0, 999.0); }

} // namespace pump
