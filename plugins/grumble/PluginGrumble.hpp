// pump GRUMBLE: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "GrumbleParams.hpp"
#include "GrumbleEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::grumble;

class PluginGrumble : public PumpPlugin {
public:
    PluginGrumble() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }

    // feeds for the UI: input and output (mono sums; the output lags by the latency) for the scope and the
    // spectrum, and the output peaks
    pump::ScopeRing inRing { pump::kAnalyzerRing }, outRing { pump::kAnalyzerRing };
    pump::MeterValue outPeak[2];
    Engine engine;
    int latencyNow() const { return engine.latency(); }

protected:
    const char* getLabel() const override { return "pump_grumble"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    void reportLatency()
    {
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }

    void sync()
    {
        Settings s;
        for (int k = 0; k < kNumLayers; ++k) { s.on[k] = pv(kLayerOn[k]) >= 0.5f; s.level[k] = pv(kLayerLevel[k]); }
        s.pan[kBeast] = pv(kBeastPan); s.pan[kSplit] = pv(kSplitPan); s.pan[kGrowl] = pv(kGrowlPan); s.pan[kSwarm] = pv(kSwarmWidth);
        s.beastPitch = pv(kBeastPitch); s.beastFormant = pv(kBeastFormant); s.beastFry = pv(kBeastFry);
        s.splitPitch = pv(kSplitPitch); s.splitFormant = pv(kSplitFormant); s.splitWobble = pv(kSplitWobble);
        s.growlPitch = pv(kGrowlPitch); s.growlRough = pv(kGrowlRough); s.growlBreath = pv(kGrowlBreath); s.growlSize = pv(kGrowlSize);
        s.swarmPitch = pv(kSwarmPitch); s.swarmSpread = pv(kSwarmSpread); s.swarmDensity = pv(kSwarmDensity); s.swarmGrainMs = pv(kSwarmGrain);
        s.morphX = pv(kMorphX); s.morphY = pv(kMorphY);
        s.gateDb = pv(kGate); s.drive = pv(kDrive); s.tone = pv(kTone); s.cave = pv(kCave); s.mix = pv(kMix); s.outDb = pv(kOutGain);
        engine.set(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);

        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, 4096);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            for (int i = 0; i < n; ++i) inRing.push(0.5f * (l[i] + r[i]));
            engine.process(l, r, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { outRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    int lastLatency = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginGrumble)
};

END_NAMESPACE_DISTRHO
