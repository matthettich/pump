// pump WIDE: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "WideParams.hpp"
#include "WideEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::wide;

class PluginWide : public PumpPlugin {
public:
    PluginWide() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        syncAll();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
    }

    // feeds for the UI: output L/R (vectorscope) and mid/side (spectra)
    pump::ScopeRing scopeL { 1 << 15 }, scopeR { 1 << 15 }, midRing { 1 << 15 }, sideRing { 1 << 15 };
    pump::MeterValue outPeak[2];
    Engine engine;
    int latencyNow() const { return engine.latency(); }

protected:
    const char* getLabel() const override { return "pump_wide"; }
    const char* getDescription() const override { return "Multiband stereo imager: width and stereoize per band, mono bass, vectorscope and correlation."; }

    void activate() override
    {
        syncAll();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        lastLatency = -1;
    }
    void sampleRateChanged(double) override { activate(); }

    void syncBand(int b)
    {
        BandSettings s;
        s.width = pv((uint32_t)bandParam(b, kWidth));
        s.stereoize = pv((uint32_t)bandParam(b, kStereoize));
        s.solo = pv((uint32_t)bandParam(b, kSolo)) >= 0.5f;
        s.mute = pv((uint32_t)bandParam(b, kMute)) >= 0.5f;
        s.bypass = pv((uint32_t)bandParam(b, kBandBypass)) >= 0.5f;
        engine.setBand(b, s);
    }
    void syncGlobals()
    {
        engine.setOutput(pv(kOutGain));
        engine.setBalance(pv(kBalance));
        engine.setMonoBass(pv(kMonoBass) >= 0.5f, pv(kMonoFreq), (int)pv(kMonoSlope));
        engine.setXoverMode((int)pv(kXoverMode));
        engine.setBands((int)pv(kNumBandsP));
        engine.setCrossovers(pv(kXover1), pv(kXover2), pv(kXover3));
        engine.setListen((int)pv(kListen) - 1);
    }
    void syncAll()
    {
        syncGlobals();
        for (int b = 0; b < kNumBands; ++b) syncBand(b);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            bool dirty[kNumBands] = {};
            bool global = false;
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i)
            {
                if (!takeChanged(i)) continue;
                const int b = bandOf((int)i);
                if (b >= 0) dirty[b] = true; else global = true;
            }
            if (global) syncGlobals();
            for (int b = 0; b < kNumBands; ++b) if (dirty[b]) syncBand(b);
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
            engine.process(l, r, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                scopeL.push(l[i]); scopeR.push(r[i]);
                midRing.push(0.5f * (l[i] + r[i])); sideRing.push(0.5f * (l[i] - r[i]));
                pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i]));
            }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    int lastLatency = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWide)
};

END_NAMESPACE_DISTRHO
