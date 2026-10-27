#include "PluginVector.hpp"

START_NAMESPACE_DISTRHO

Plugin* createPlugin() { return new PluginVector(); }

END_NAMESPACE_DISTRHO
