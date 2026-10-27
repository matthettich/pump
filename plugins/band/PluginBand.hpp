// pump BAND: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "BandDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::band;

class PluginBand : public pump::band::BandDsp<PumpPlugin> {
public:
    PluginBand() : BandDsp(params().data(), kNumParams, &presets()) {}

protected:
    const char* getLabel() const override { return "pump_band"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBand)
};

END_NAMESPACE_DISTRHO
