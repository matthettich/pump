// pump TWANG in pump.wasm: plugins/twang/PluginTwang.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_twang
#include "PluginTwang.hpp"
#include "Runner.hpp"
PUMP_RUNNER(twang, PluginTwang)
