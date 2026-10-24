#include "PluginGate.hpp"

START_NAMESPACE_DISTRHO

Plugin* createPlugin() { return new PluginGate(); }

END_NAMESPACE_DISTRHO
