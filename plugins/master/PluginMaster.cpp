#include "PluginMaster.hpp"

START_NAMESPACE_DISTRHO

Plugin* createPlugin() { return new PluginMaster(); }

END_NAMESPACE_DISTRHO
