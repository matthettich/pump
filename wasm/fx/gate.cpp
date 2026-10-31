// pump GATE in pump.wasm: plugins/gate/PluginGate.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_gate
#include "PluginGate.hpp"
#include "Runner.hpp"

namespace pumpshim_gate {
// The window (pump/ui/gate.js). Feeds after the common five (t[5 + i]): 0, 1 the gate's opening now L, R (0..1, after
// depth), 2 the position (quarter notes, end of the last block), 3 tempo, 4 quarters in a bar, 5 locked to the song,
// 6..261 the level display: kBinsPerStep (8) bins a step for up to 32 steps (linear gain; the window shows dB).
template <> struct RunnerExtra<PluginGate> {
    static constexpr int kTel = 262;
    static void command(PluginGate&, int, float, float) {}
    static int status(PluginGate& p, float* t)
    {
        auto& e = p.engine;
        t[0] = e.openNow(0); t[1] = e.openNow(1); t[2] = (float)e.positionQ(); t[3] = (float)e.tempo(); t[4] = e.barQuarters(); t[5] = e.hostLocked() ? 1.0f : 0.0f;
        for (int b = 0; b < 256; ++b) t[6 + b] = e.bin(b);
        return kTel;
    }
    static int rings(const PluginGate&) { return 0; }
    static const pump::ScopeRing* ring(const PluginGate&, int) { return nullptr; }
};
} // namespace pumpshim_gate

PUMP_RUNNER(gate, PluginGate)
