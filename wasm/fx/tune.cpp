// pump TUNE in pump.wasm: plugins/tune/PluginTune.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
// Its states: "notes" (the graph's notes and their edits: the DSP plays them; pump_state_set / get), "curve" (the
// captured pitch, the window's own: Thunder keeps it in the slot). The timeline is the song's (seconds from its start).
#define PUMP_SHIM_NS pumpshim_tune
#include "PluginTune.hpp"
#include "Runner.hpp"

namespace pumpshim_tune {
// The window (pump/ui/tune.js). Command (pump_hold kind 3) a = 1: capture b >= 0.5 on / off (the graph's CAPTURE).
// Feeds after the common five (t[5 + i]): 0 the voice's pitch now (MIDI, fractional; -1 none), 1 the output's,
// 2 the target, 3 the playhead (s), 4 playing, 5 voiced, 6..9 the harmonies' notes (-1 off), 10..17 the MIDI notes held
// (128 bits as eight 16-bit numbers, low first), 18 n = capture frames below, 19.. n frames of (t s, pitch MIDI,
// level dB), oldest first: what was captured since the last read (at most 300; older ones are dropped).
template <> struct RunnerExtra<PluginTune> {
    static constexpr int kMaxFrames = 300, kTel = 19 + 3 * kMaxFrames;
    static void command(PluginTune& p, int c, float b, float) { if (c == 1) p.engine.setCapture(b >= 0.5f); }
    static int status(PluginTune& p, float* t)
    {
        using namespace pump::tune;
        Engine& e = p.engine;
        t[0] = e.inMidi.load(); t[1] = e.outMidi.load(); t[2] = e.targetMidi.load();
        t[3] = (float)e.playheadNow.load(); t[4] = e.playingNow.load() ? 1.0f : 0.0f; t[5] = e.voicedNow.load() ? 1.0f : 0.0f;
        for (int h = 0; h < 4; ++h) t[6 + h] = h < kVoices ? e.harmNoteNow[h].load() : -1.0f;
        for (int i = 0; i < 4; ++i) { const uint32_t b = e.heldBits[i].load(); t[10 + 2 * i] = (float)(b & 0xFFFF); t[11 + 2 * i] = (float)(b >> 16); }
        const uint32_t w = e.capW.load(std::memory_order_acquire);
        uint32_t r = e.capR.load(std::memory_order_relaxed);
        if (w - r > (uint32_t)kMaxFrames) r = w - (uint32_t)kMaxFrames;   // more than a read holds: the oldest go
        int n = 0;
        for (; r != w; ++r, ++n) { const CaptureFrame& f = e.capRing[r & (Engine::kCapRing - 1)]; t[19 + 3 * n] = (float)f.t; t[20 + 3 * n] = f.midi; t[21 + 3 * n] = f.levelDb; }
        e.capR.store(r, std::memory_order_release);
        t[18] = (float)n;
        return 19 + 3 * n;
    }
    static int rings(const PluginTune&) { return 0; }
    static const pump::ScopeRing* ring(const PluginTune&, int) { return nullptr; }
};
} // namespace pumpshim_tune

PUMP_RUNNER(tune, PluginTune)
