// pump STRIP in pump.wasm: plugins/strip/PluginStrip.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_strip
#include "PluginStrip.hpp"
#include "Runner.hpp"

namespace pumpshim_strip {
// Feeds after the common five (t[5 + i]): 0 compressor gain reduction, 1 gate gain reduction (positive dB, the most
// since the last read), 2 the compressor's detector level (dB). Ring 0: the output (mono sum, for an analyzer).
template <> struct RunnerExtra<PluginStrip> {
    static constexpr int kTel = 3;
    static void command(PluginStrip&, int, float, float) {}
    static int status(PluginStrip& p, float* t)
    {
        t[0] = p.compGr.exchange(0.0f); t[1] = p.gateGr.exchange(0.0f); t[2] = p.compLevel.get();
        return kTel;
    }
    static int rings(const PluginStrip&) { return 1; }
    static const pump::ScopeRing* ring(const PluginStrip& p, int k) { return k == 0 ? &p.outRing : nullptr; }
};
} // namespace pumpshim_strip

PUMP_RUNNER(strip, PluginStrip)
