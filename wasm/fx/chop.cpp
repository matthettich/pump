// pump CHOP in pump.wasm: plugins/chop/PluginChop.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_chop
#include "PluginChop.hpp"
#include "Runner.hpp"
PUMP_RUNNER(chop, PluginChop)
