// pump METER: plugin (DSP side). Audio passes through bit for bit; the engine only reads it. Declared in a
// header so the UI can read the readouts, history and spectrum feed directly.
#pragma once
#include "PumpPlugin.hpp"
#include "MeterDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::meter;

class PluginMeter : public pump::meter::MeterDsp<PumpPlugin> {
public:
    PluginMeter() : MeterDsp(params().data(), kNumParams, &presets()) {}

protected:
    const char* getLabel() const override { return "pump_meter"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginMeter)
};

END_NAMESPACE_DISTRHO
