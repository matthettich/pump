#include "PluginKit.hpp"

START_NAMESPACE_DISTRHO

Plugin* createPlugin() { return new PluginKit(); }

END_NAMESPACE_DISTRHO
