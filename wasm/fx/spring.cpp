// pump SPRING in pump.wasm: plugins/spring/PluginSpring.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_spring
#include "PluginSpring.hpp"
#include "Runner.hpp"
PUMP_RUNNER(spring, PluginSpring)
