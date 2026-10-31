// pump TUBE in pump.wasm: plugins/tube/PluginTube.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_tube
#include "PluginTube.hpp"
#include "Runner.hpp"
PUMP_RUNNER(tube, PluginTube)
