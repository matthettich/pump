// pump SNAP in pump.wasm: plugins/snap/PluginSnap.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_snap
#include "PluginSnap.hpp"
#include "Runner.hpp"
PUMP_RUNNER(snap, PluginSnap)
