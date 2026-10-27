// pump HEAT: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "HeatDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::heat;

class PluginHeat : public pump::heat::HeatDsp<PumpPlugin> {
public:
    PluginHeat() : HeatDsp(params().data(), kNumParams, &presets()) {}

protected:
    const char* getLabel() const override { return "pump_heat"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHeat)
};

END_NAMESPACE_DISTRHO
