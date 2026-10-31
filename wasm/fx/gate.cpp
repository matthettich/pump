// pump GATE in pump.wasm: plugins/gate/PluginGate.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_gate
#include "PluginGate.hpp"
#include "Runner.hpp"
PUMP_RUNNER(gate, PluginGate)
