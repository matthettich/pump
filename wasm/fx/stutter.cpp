// pump STUTTER in pump.wasm: plugins/stutter/PluginStutter.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_stutter
#include "PluginStutter.hpp"
#include "Runner.hpp"
PUMP_RUNNER(stutter, PluginStutter)
