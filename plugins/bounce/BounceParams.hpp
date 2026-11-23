// pump BOUNCE: parameters and factory presets (shared by the DSP and the UI).
#pragma once

#include "PumpParams.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pump { namespace bounce {

// Parameter indices. Symbols (first column of params()) never change once released; append only.
//   kMode      TAPS: a multitap delay on the continuous signal. DROP: a trigger captures a slice and re-fires it as a
//              bouncing sequence.
//   kTime      the first gap, in ms (free running); kSync / kNote: the same as a note division at the host tempo.
//   kGravity   the ratio of each gap to the one before it (0.50 = every gap half the last; 0.98 = a slow settle);
//              kRise turns it round (the gaps grow by 1 / gravity: a ball thrown up).
//   kBounces   how many bounces after the first hit (the ball also settles when the gaps fall under 2 ms).
//   kDamp      dB lost per bounce.
//   kPitch     semitones per bounce, accumulating (DROP only: the slice is played back faster or slower).
//   kTone      tilt per bounce, accumulating (DROP only): negative darkens each hit, positive brightens it.
//   kSpread    how far the bounces swing across the stereo field.
//   kJitter    humanize: each bounce lands a little early or late.
//   kSlice     DROP only: the length of the captured slice in ms. kFit shortens each hit to fit its gap.
//   kTrigger   DROP only: what fires a drop (a transient over kThreshold, every kEvery note division of the host
//              tempo, a MIDI note-on, or the kDrop button).
//   kDrop      the momentary DROP button (a host-automatable parameter; a rising edge fires a drop, it is not part
//              of presets, undo or A/B).
//   kFeedback  TAPS: the last bounce feeds the line again, so the settled ball re-drops. DROP: the whole bounce comes
//              back, softer, after it has settled.
enum Param : int {
    kMode = 0, kTime, kSync, kNote, kGravity, kRise, kBounces, kDamp, kPitch, kTone, kSpread, kJitter,
    kSlice, kFit, kTrigger, kThreshold, kEvery, kDrop, kFeedback, kMix, kOutGain, kBypass,
    kNumParams
};

enum Mode : int { kModeTaps = 0, kModeDrop, kNumModes };
static const char* const kModeNames[] = { "Taps", "Drop" };
enum Trigger : int { kTrigTransient = 0, kTrigSync, kTrigMidi, kTrigButton, kNumTriggers };
static const char* const kTriggerNames[] = { "Transient", "Sync", "MIDI", "Button" };

constexpr float kTimeMinMs = 10.0f, kTimeMaxMs = 4000.0f;
constexpr int kMaxBounces = 64;
constexpr double kMaxSeconds = 8.0;          // the whole bounce stays inside this (a synced 2/1 at 60 BPM is 8 s)
constexpr double kMinGapSeconds = 0.002;     // gaps under this: the ball has settled
constexpr float kGravityMin = 0.50f, kGravityMax = 0.98f;
constexpr float kSliceMinMs = 5.0f, kSliceMaxMs = 500.0f;
constexpr float kPitchRange = 12.0f;
constexpr float kThresholdMin = -60.0f;

inline const std::vector<ParamSpec>& params()
{
    static const std::vector<ParamSpec> v = []() {
    std::vector<ParamSpec> v;
    const unsigned A = kPAutomatable;
    // every control glides or is applied per drop (a drop keeps the settings it started with), so all stay automatable
    v.push_back({ "mode", "Mode", "Mode", 0, kNumModes - 1, kModeTaps, PUnit::None, PScale::Linear, A | kPInteger, kModeNames, 1 });
    v.push_back({ "time", "Time", "Time", kTimeMinMs, kTimeMaxMs, 200, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "sync", "Sync", "Sync", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "note", "Note", "Note", 0, kNumNotes - 1, kNote8, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "gravity", "Gravity", "Gravity", kGravityMin, kGravityMax, 0.80f, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "rise", "Rise", "Rise", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "bounces", "Bounces", "Bounces", 2, kMaxBounces, 16, PUnit::None, PScale::Linear, A | kPInteger, nullptr, 1 });
    v.push_back({ "damp", "Damp", "Damp", 0, 12, 1.5f, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "pitch", "Pitch", "Pitch", -kPitchRange, kPitchRange, 0, PUnit::None, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "tone", "Tone", "Tone", -100, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "spread", "Spread", "Spread", 0, 100, 40, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "jitter", "Jitter", "Jitter", 0, 100, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "slice", "Slice", "Slice", kSliceMinMs, kSliceMaxMs, 200, PUnit::ms, PScale::Log, A, nullptr, 1 });
    v.push_back({ "fit", "Fit", "Fit", 0, 1, 1, PUnit::None, PScale::Linear, A | kPBoolean, nullptr, 1 });
    v.push_back({ "trigger", "Trigger", "Trigger", 0, kNumTriggers - 1, kTrigTransient, PUnit::None, PScale::Linear, A | kPInteger, kTriggerNames, 1 });
    v.push_back({ "threshold", "Threshold", "Thresh", kThresholdMin, 0, -24, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "every", "Every", "Every", 0, kNumNotes - 1, kNote4, PUnit::None, PScale::Linear, A | kPInteger, kNoteNames, 1 });
    v.push_back({ "drop", "Drop", "Drop", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPNoUndo, nullptr, 1 });
    v.push_back({ "feedback", "Feedback", "Feedback", 0, 90, 0, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "mix", "Mix", "Mix", 0, 100, 50, PUnit::Percent, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "out_gain", "Output Gain", "Output", -24, 24, 0, PUnit::dB, PScale::Linear, A, nullptr, 1 });
    v.push_back({ "bypass", "Bypass", "Bypass", 0, 1, 0, PUnit::None, PScale::Linear, A | kPBoolean | kPBypass, nullptr, 1 });
    return v;
    }();
    return v;
}

// Factory presets. Modes: 0 taps 1 drop; trigger: 0 transient 1 sync 2 MIDI 3 button. Notes: 5 1/16, 8 1/8, 9 1/8D,
// 11 1/4, 14 1/2, 16 1/1.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> v = []() {
    std::vector<Preset> v;
    auto F = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p) }); };
    auto C = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "CLASSIC" }); };
    auto G = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "GLITCH" }); };
    auto R = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "RHYTHM" }); };
    auto S = [&](const char* n, std::vector<PresetValue> p) { v.push_back({ n, std::move(p), "SLOW" }); };
    F("Default", {});
    C("Dropped ball", { { "mode", 1 }, { "time", 260 }, { "gravity", 0.74f }, { "bounces", 24 }, { "damp", 0.8f }, { "slice", 90 }, { "spread", 20 }, { "threshold", -26 }, { "mix", 50 } });
    C("Ping-pong ball", { { "time", 240 }, { "gravity", 0.78f }, { "bounces", 18 }, { "damp", 1.5f }, { "spread", 100 }, { "mix", 40 } });
    C("Rubber ball", { { "mode", 1 }, { "time", 340 }, { "gravity", 0.86f }, { "bounces", 32 }, { "damp", 0.6f }, { "slice", 120 }, { "spread", 30 }, { "mix", 45 } });
    C("Steel ball", { { "mode", 1 }, { "time", 180 }, { "gravity", 0.66f }, { "bounces", 20 }, { "damp", 0.5f }, { "tone", 25 }, { "slice", 40 }, { "spread", 50 }, { "mix", 45 } });
    G("Ratchet roll", { { "mode", 1 }, { "time", 180 }, { "gravity", 0.84f }, { "bounces", 48 }, { "damp", 0.4f }, { "slice", 30 }, { "spread", 30 }, { "mix", 55 } });
    G("Climbing bounce", { { "mode", 1 }, { "time", 220 }, { "gravity", 0.80f }, { "bounces", 20 }, { "damp", 1.0f }, { "pitch", 1.5f }, { "slice", 100 }, { "spread", 40 }, { "mix", 50 } });
    G("Falling pitch", { { "mode", 1 }, { "time", 160 }, { "gravity", 0.82f }, { "bounces", 20 }, { "damp", 1.0f }, { "pitch", -2.0f }, { "tone", -30 }, { "slice", 140 }, { "mix", 50 } });
    G("Scatter", { { "mode", 1 }, { "time", 200 }, { "gravity", 0.88f }, { "bounces", 40 }, { "damp", 0.7f }, { "jitter", 60 }, { "spread", 100 }, { "slice", 50 }, { "mix", 50 } });
    G("Machine gun", { { "time", 120 }, { "gravity", 0.92f }, { "bounces", 64 }, { "damp", 0.3f }, { "spread", 60 }, { "mix", 45 } });
    R("Beat drop", { { "mode", 1 }, { "trigger", 1 }, { "every", 16 }, { "sync", 1 }, { "note", 11 }, { "gravity", 0.72f }, { "bounces", 24 }, { "damp", 1.0f }, { "slice", 140 }, { "mix", 50 } });
    R("Eighth ratchet", { { "sync", 1 }, { "note", 8 }, { "gravity", 0.75f }, { "bounces", 12 }, { "damp", 1.5f }, { "spread", 70 }, { "mix", 40 } });
    R("Half-time ball", { { "mode", 1 }, { "trigger", 1 }, { "every", 16 }, { "sync", 1 }, { "note", 14 }, { "gravity", 0.70f }, { "bounces", 16 }, { "damp", 1.2f }, { "slice", 200 }, { "pitch", -1.0f }, { "mix", 50 } });
    R("Played ball", { { "mode", 1 }, { "trigger", 2 }, { "sync", 1 }, { "note", 9 }, { "gravity", 0.76f }, { "bounces", 20 }, { "damp", 1.0f }, { "slice", 110 }, { "spread", 50 }, { "mix", 55 } });
    S("Rise", { { "mode", 1 }, { "time", 40 }, { "rise", 1 }, { "gravity", 0.80f }, { "bounces", 14 }, { "damp", 0.5f }, { "slice", 60 }, { "spread", 40 }, { "mix", 50 } });
    S("Thrown up", { { "mode", 1 }, { "time", 20 }, { "rise", 1 }, { "gravity", 0.70f }, { "bounces", 12 }, { "damp", 0.8f }, { "pitch", 1.0f }, { "slice", 40 }, { "mix", 50 } });
    S("Slow fall", { { "time", 800 }, { "gravity", 0.85f }, { "bounces", 12 }, { "damp", 1.0f }, { "spread", 60 }, { "mix", 35 } });
    S("Settling echo", { { "mode", 1 }, { "time", 300 }, { "gravity", 0.78f }, { "bounces", 16 }, { "damp", 1.5f }, { "feedback", 45 }, { "slice", 120 }, { "mix", 45 } });
    return v;
    }();
    return v;
}

}} // namespace pump::bounce
