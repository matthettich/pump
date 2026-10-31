// pump TUNE in pump.wasm: plugins/tune/PluginTune.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_tune
#include "PluginTune.hpp"
#include "Runner.hpp"
PUMP_RUNNER(tune, PluginTune)
