// pump VERB in pump.wasm: plugins/verb/PluginVerb.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
#define PUMP_SHIM_NS pumpshim_verb
#include "PluginVerb.hpp"
#include "Runner.hpp"
PUMP_RUNNER(verb, PluginVerb)
