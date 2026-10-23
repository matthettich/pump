// pump HEAT: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "HeatParams.hpp"
#include "HeatEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::heat;

class PluginHeat : public PumpPlugin {
public:
    PluginHeat() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        syncAll();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
    }

    // feeds for the UI
    pump::ScopeRing preRing { 1 << 15 }, postRing { 1 << 15 };
    pump::MeterValue outPeak[2];
    Engine engine;
    int latencyNow() const { return engine.latency(); }

protected:
    const char* getLabel() const override { return "pump_heat"; }
    const char* getDescription() const override { return "Multiband saturation and exciter: warm, tape, tube, triode, retro and clip curves, oversampled, with auto gain."; }

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
        s.mode = (int)pv((uint32_t)bandParam(b, kMode));
        s.drive = pv((uint32_t)bandParam(b, kDrive));
        s.mix = pv((uint32_t)bandParam(b, kBandMix));
        s.level = pv((uint32_t)bandParam(b, kLevel));
        s.solo = pv((uint32_t)bandParam(b, kSolo)) >= 0.5f;
        s.mute = pv((uint32_t)bandParam(b, kMute)) >= 0.5f;
        s.bypass = pv((uint32_t)bandParam(b, kBandBypass)) >= 0.5f;
        engine.setBand(b, s);
    }
    void syncGlobals()
    {
        engine.setOutput(pv(kOutGain));
        engine.setMix(pv(kMix));
        engine.setOversampling((int)pv(kOversampling));
        engine.setAutoGain(pv(kAutoGain) >= 0.5f);
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
            for (int i = 0; i < n; ++i) preRing.push(0.5f * (l[i] + r[i]));
            engine.process(l, r, n);
            // latency follows the engine (it changes only after an oversampling or crossover-type fade)
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    int lastLatency = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHeat)
};

END_NAMESPACE_DISTRHO
