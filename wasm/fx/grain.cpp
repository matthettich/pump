// pump GRAIN in pump.wasm: plugins/grain/PluginGrain.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_grain
#include "PluginGrain.hpp"
#include "Runner.hpp"
PUMP_RUNNER(grain, PluginGrain)
