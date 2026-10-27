// pump COMP: plugin (DSP side). Declared in a header so the UI can read the engine's meters and history directly.
#pragma once
#include "PumpPlugin.hpp"
#include "CompDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::comp;

class PluginComp : public pump::comp::CompDsp<PumpPlugin> {
public:
    PluginComp() : CompDsp(params().data(), kNumParams, &presets()) { numInputs = DISTRHO_PLUGIN_NUM_INPUTS; }

    int latencyNow() const { return engine.latency(); }
    bool sidechainConnected() const { return numInputs >= 4; }

protected:
    const char* getLabel() const override { return "pump_comp"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    // Inputs 3-4 are the external sidechain: their own stereo group, flagged as sidechain, which DPF turns into a
    // VST3 aux bus and a non-main CLAP port (hosts that leave it unconnected pass silence there).
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        if (input && index >= 2)
        {
            port.hints = kAudioPortIsSidechain;
            port.name = index == 2 ? "Sidechain L" : "Sidechain R";
            port.symbol = index == 2 ? "sidechain_l" : "sidechain_r";
            port.groupId = kGroupSidechain;
            return;
        }
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }
    void initPortGroup(uint32_t groupId, PortGroup& group) override
    {
        if (groupId == kGroupSidechain) { group.name = "Sidechain"; group.symbol = "sidechain"; return; }
        Plugin::initPortGroup(groupId, group);
    }
    // AU only (DISTRHO_PLUGIN_EXTRA_IO): the host picked plain stereo, so there is no sidechain input.
    void ioChanged(uint16_t inputs, uint16_t) override { numInputs = inputs; }

private:
    static constexpr uint32_t kGroupSidechain = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginComp)
};

END_NAMESPACE_DISTRHO
