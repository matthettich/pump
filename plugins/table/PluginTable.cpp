#include "PluginTable.hpp"

START_NAMESPACE_DISTRHO

Plugin* createPlugin() { return new PluginTable(); }

END_NAMESPACE_DISTRHO
