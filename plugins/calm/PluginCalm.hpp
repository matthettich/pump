// pump CALM: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "CalmDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::calm;

class PluginCalm : public pump::calm::CalmDsp<PumpPlugin> {
public:
    PluginCalm() : CalmDsp(params().data(), kNumParams, &presets()) {}

protected:
    const char* getLabel() const override { return "pump_calm"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCalm)
};

END_NAMESPACE_DISTRHO
