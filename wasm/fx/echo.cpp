// pump ECHO in pump.wasm: plugins/echo/PluginEcho.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_echo
#include "PluginEcho.hpp"
#include "Runner.hpp"
PUMP_RUNNER(echo, PluginEcho)
