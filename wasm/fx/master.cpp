// pump MASTER in pump.wasm: plugins/master/PluginMaster.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_master
#include "PluginMaster.hpp"
#include "Runner.hpp"
PUMP_RUNNER(master, PluginMaster)
