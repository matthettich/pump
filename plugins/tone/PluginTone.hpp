// pump TONE: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "ToneDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::tone;

class PluginTone : public pump::tone::ToneDsp<PumpPlugin> {
public:
    PluginTone() : ToneDsp(params().data(), kNumParams, &presets()) {}

protected:
    const char* getLabel() const override { return "pump_tone"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginTone)
};

END_NAMESPACE_DISTRHO
