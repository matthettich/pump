// pump COMP: plugin (DSP side). Declared in a header so the UI can read the engine's meters and history directly.
#pragma once
#include "PumpPlugin.hpp"
#include "CompParams.hpp"
#include "CompEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::comp;

class PluginComp : public PumpPlugin {
public:
    PluginComp() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        prepareAll();
    }

    pump::comp::Engine engine;
    int latencyNow() const { return engine.latency(); }
    bool sidechainConnected() const { return fNumInputs >= 4; }

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
    void ioChanged(uint16_t numInputs, uint16_t) override { fNumInputs = numInputs; }

    void activate() override
    {
        prepareAll();
        lastLatency = -1;
    }
    void sampleRateChanged(double) override { prepareAll(); }

    void prepareAll()
    {
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        syncValues();
        engine.setSettings(settingsFromParams(values));
        engine.prepare(sr, (int)getBufferSize());
        bypass.prepare(sr, engine.maxLatencySamples() + 8, kBlock);
        bypass.setLatency(engine.latency());
        silence.assign(kBlock, 0.0f);
        setLatency((uint32_t)engine.latency());
    }
    void syncValues() { for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) values[i] = pv(i); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            syncValues();
            engine.setSettings(settingsFromParams(values));
        }
        const bool bypassed = pv(kBypass) >= 0.5f;
        const bool hasSc = fNumInputs >= 4 && inputs[2] != nullptr && inputs[3] != nullptr;

        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)kBlock);
            // copy everything first: hosts may hand us aliased in/out (and even sidechain) buffers
            for (int i = 0; i < n; ++i)
            {
                bufL[i] = inputs[0][done + i]; bufR[i] = inputs[1][done + i];
                scL[i] = hasSc ? inputs[2][done + i] : 0.0f; scR[i] = hasSc ? inputs[3][done + i] : 0.0f;
            }
            bypass.capture(bufL, bufR, n);
            engine.process(bufL, bufR, scL, scR, bufL, bufR, n);
            // the engine applies latency changes between chunks: keep the bypass path matched
            const int lat = engine.latency();
            if (lat != lastLatency) { bypass.setLatency(lat); lastLatency = lat; setLatency((uint32_t)lat); }
            bypass.apply(bufL, bufR, n, bypassed);
            std::memcpy(outputs[0] + done, bufL, sizeof(float) * (size_t)n);
            std::memcpy(outputs[1] + done, bufR, sizeof(float) * (size_t)n);
            done += (uint32_t)n;
        }
    }

private:
    static constexpr uint32_t kGroupSidechain = 0;
    static constexpr int kBlock = 256;
    pump::BypassMixer bypass;
    float values[kNumParams] = {};
    float bufL[kBlock] = {}, bufR[kBlock] = {}, scL[kBlock] = {}, scR[kBlock] = {};
    std::vector<float> silence;
    int lastLatency = -1;
    uint16_t fNumInputs = DISTRHO_PLUGIN_NUM_INPUTS;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginComp)
};

END_NAMESPACE_DISTRHO
