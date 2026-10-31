// pump CRUSH in pump.wasm: plugins/crush/PluginCrush.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_crush
#include "PluginCrush.hpp"
#include "Runner.hpp"
PUMP_RUNNER(crush, PluginCrush)
