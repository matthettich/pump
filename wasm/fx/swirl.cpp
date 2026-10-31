// pump SWIRL in pump.wasm: plugins/swirl/PluginSwirl.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_swirl
#include "PluginSwirl.hpp"
#include "Runner.hpp"
PUMP_RUNNER(swirl, PluginSwirl)
