// pump LIMIT: plugin (DSP side). Declared in a header so the UI can read its meters and history directly.
#pragma once
#include "PumpPlugin.hpp"
#include "LimitDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::limit;

class PluginLimit : public pump::limit::LimitDsp<PumpPlugin> {
public:
    PluginLimit() : LimitDsp(params().data(), kNumParams, &presets()) {}

    int latencyNow() const { return engine.latency(); }
    double rate() { return getSampleRate(); }

protected:
    const char* getLabel() const override { return "pump_limit"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginLimit)
};

END_NAMESPACE_DISTRHO
