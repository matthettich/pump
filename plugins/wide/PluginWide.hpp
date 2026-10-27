// pump WIDE: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "WideDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::wide;

class PluginWide : public pump::wide::WideDsp<PumpPlugin> {
public:
    PluginWide() : WideDsp(params().data(), kNumParams, &presets()) {}

    int latencyNow() const { return engine.latency(); }

protected:
    const char* getLabel() const override { return "pump_wide"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWide)
};

END_NAMESPACE_DISTRHO
