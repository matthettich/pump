// pump CHOP in pump.wasm: plugins/chop/PluginChop.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_chop
#include "PluginChop.hpp"
#include "Runner.hpp"

namespace pumpshim_chop {
// The window (pump/ui/chop.js). Pads from the window: pump_hold kind 4 (a momentary parameter) on pad<n>_trigger.
// Feeds after the common five (t[5 + i]), positions in samples RELATIVE to the samples written so far (a float can't
// hold the absolute count for long): 0 region start - written, 1 region length, 2 playhead - written (NaN: none),
// 3 written (mod 2^24, for "has it moved"), 4 the pad playing (-1 none), 5 pads held (bits), 6 pads pending (bits),
// 7 live capture start - written, 8 live length, 9 last MIDI note, 10 its count (mod 2^16: a new note when it changes),
// 11..522 the held region's overview (512 peaks), 523..1034 the live capture's overview (512 peaks).
template <> struct RunnerExtra<PluginChop> {
    static constexpr int kTel = 1035;
    static void command(PluginChop&, int, float, float) {}
    static int status(PluginChop& p, float* t)
    {
        auto& e = p.engine;
        const double w = (double)e.writtenNow(), ph = e.playheadNow();
        t[0] = (float)(e.regionStartNow() - w); t[1] = (float)e.regionLenNow(); t[2] = ph < 0.0 ? NAN : (float)(ph - w);
        t[3] = (float)((long long)w & 0xFFFFFF); t[4] = (float)e.padNow(); t[5] = (float)e.heldNow(); t[6] = (float)e.pendingNow();
        t[7] = (float)(e.liveStartNow() - w); t[8] = (float)e.liveLenNow();
        const uint32_t ln = p.lastNote.load(); t[9] = (float)(ln & 0xFF); t[10] = (float)((ln >> 8) & 0xFFFF);
        e.overview(t + 11, 512); e.liveOverview(t + 523, 512);
        return kTel;
    }
    static int rings(const PluginChop&) { return 0; }
    static const pump::ScopeRing* ring(const PluginChop&, int) { return nullptr; }
};
} // namespace pumpshim_chop

PUMP_RUNNER(chop, PluginChop)
