// pump VOX in pump.wasm: plugins/vox/PluginVox.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_vox
#include "PluginVox.hpp"
#include "Runner.hpp"
PUMP_RUNNER(vox, PluginVox)
