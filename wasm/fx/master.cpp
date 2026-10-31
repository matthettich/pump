// pump MASTER in pump.wasm: plugins/master/PluginMaster.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
// Its one try / catch (reading a saved analysis; parseAnalysis itself never throws) builds without exceptions here: the
// headers it uses are included first, then `try` / `catch` stand for plain blocks while the plugin header is read.
#define PUMP_SHIM_NS pumpshim_master
#include "PumpPlugin.hpp"
#include "MasterParams.hpp"
#include "MasterEngine.hpp"
#include "dsp/Bypass.hpp"
#include <mutex>
#define try if (true)
#define catch(...) if (false)
#include "PluginMaster.hpp"
#undef try
#undef catch
#include "Runner.hpp"

namespace pumpshim_master {
// LEARN from Thunder Plus (pump_hold kind 3): 1 start, 2 finish, 3 cancel. Status: learning, an analysis in use.
template <> struct RunnerExtra<PluginMaster> {
    static void command(PluginMaster& p, int c) { if (c == 1) p.learnStart(); else if (c == 2) p.learnFinish(); else if (c == 3) p.learnCancel(); }
    static int status(PluginMaster& p, float* t)
    {
        pump::master::Analysis a; bool got = false;
        for (int i = 0; i < 8 && !got; ++i) got = p.engine.published.load(a);
        t[0] = p.engine.uiLearning.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        t[1] = got && a.valid ? 1.0f : 0.0f;
        t[2] = got && a.valid ? a.lufs : -200.0f;
        return 3;
    }
};
} // namespace pumpshim_master

PUMP_RUNNER(master, PluginMaster)
