// pump STRIP: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "StripParams.hpp"
#include "StripEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::strip;

class PluginStrip : public PumpPlugin {
public:
    PluginStrip() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }

    // feeds for the UI: the output (mono sum, for the analyzer), input and output peaks, gain reduction (as positive
    // dB, held as maxima), the compressor's detector level
    pump::ScopeRing outRing { 1 << 15 };
    pump::MeterValue inPeak[2], outPeak[2], compGr, gateGr, compLevel;
    Engine engine;
    int latencyNow() const { return engine.latency(); }

protected:
    const char* getLabel() const override { return "pump_strip"; }
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
        s.model = (int)pv(kModel);
        s.input = pv(kInput);
        s.color = pv(kColor);
        s.phase = pv(kPhase) >= 0.5f;
        s.hpf = pv(kHpf);
        s.lpf = pv(kLpf);
        s.eqOn = pv(kEqOn) >= 0.5f;
        s.g[0] = pv(kLfGain); s.f[0] = pv(kLfFreq);
        s.g[1] = pv(kLmfGain); s.f[1] = pv(kLmfFreq); s.q[1] = pv(kLmfQ);
        s.g[2] = pv(kHmfGain); s.f[2] = pv(kHmfFreq); s.q[2] = pv(kHmfQ);
        s.g[3] = pv(kHfGain); s.f[3] = pv(kHfFreq);
        s.lfBell = pv(kLfBell) >= 0.5f;
        s.hfBell = pv(kHfBell) >= 0.5f;
        s.gateOn = pv(kGateOn) >= 0.5f;
        s.gateThresh = pv(kGateThresh);
        s.gateRange = pv(kGateRange);
        s.gateRelease = pv(kGateRelease);
        s.gateMode = (int)pv(kGateMode);
        s.compOn = pv(kCompOn) >= 0.5f;
        s.thresh = pv(kCompThresh);
        s.ratio = pv(kCompRatio);
        s.attack = pv(kCompAttack);
        s.release = pv(kCompRelease);
        s.autoRel = pv(kCompAuto) >= 0.5f;
        s.makeup = pv(kCompMakeup);
        s.mix = pv(kCompMix);
        s.scHpf = pv(kScHpf);
        s.order = (int)pv(kOrder);
        s.outDb = pv(kOutGain);
        s.autoGain = pv(kAutoGain) >= 0.5f;
        s.oversampling = (int)pv(kOversampling);
        engine.setSettings(s);
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
            float il = 0.0f, ir = 0.0f;
            for (int i = 0; i < n; ++i) { il = std::max(il, std::fabs(l[i])); ir = std::max(ir, std::fabs(r[i])); }
            inPeak[0].setMax(il); inPeak[1].setMax(ir);
            engine.process(l, r, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { outRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            compGr.setMax(-engine.compGr.load(std::memory_order_relaxed));
            gateGr.setMax(-engine.gateGr.load(std::memory_order_relaxed));
            // the detector level, offset so silence (-120) still reads as a positive max-hold value
            compLevel.setMax(engine.compLevel.load(std::memory_order_relaxed) + 200.0f);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    int lastLatency = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginStrip)
};

END_NAMESPACE_DISTRHO
