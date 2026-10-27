// pump EQ: plugin (DSP side). Declared in a header so the UI can read its analyzer feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "EQDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::eq;

class PluginEQ : public pump::eq::EQDsp<PumpPlugin> {
public:
    PluginEQ() : EQDsp(params().data(), kNumParams, &presets()) {}

    double dynOffset(int band) const { return engine.dynOffset(band); }
    int latencyNow() const { return engine.latencyForUI(); }

protected:
    const char* getLabel() const override { return "pump_eq"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEQ)
};

END_NAMESPACE_DISTRHO
