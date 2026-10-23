// pump EQ: plugin (DSP side). Declared in a header so the UI can read its analyzer feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "EQParams.hpp"
#include "EQEngine.hpp"
#include "dsp/Bypass.hpp"
#include "dsp/Audition.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::eq;

class PluginEQ : public PumpPlugin {
public:
    PluginEQ() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 17, (int)std::max<uint32_t>(getBufferSize(), 8192));
    }

    // feeds for the UI (analyzers)
    pump::ScopeRing preRing { 1 << 15 }, postRing { 1 << 15 };
    Engine engine;

    double dynOffset(int band) const { return engine.dynOffset(band); }
    int latencyNow() const { return engine.latency(); }

protected:
    const char* getLabel() const override { return "pump_eq"; }
    const char* getDescription() const override { return "24-band mastering EQ: analog-matched curves, dynamic bands, mid/side, linear phase."; }

    void activate() override
    {
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 17, (int)std::max<uint32_t>(getBufferSize(), 8192));
        audition.prepare(getSampleRate());
        syncAll();
        lastLatency = -1;
    }
    void sampleRateChanged(double) override { activate(); }

    void syncAll()
    {
        for (int b = 0; b < kNumBands; ++b) syncBand(b);
        engine.setGainScale(pv(kGainScale));
        engine.setOutput(pv(kOutGain));
        engine.setAutoGain(pv(kAutoGain) >= 0.5f);
        engine.setDynTimes(pv(kDynAttack), pv(kDynRelease));
        engine.setPhaseMode((int)pv(kPhase), (int)pv(kLinQuality));
        engine.setSolo((int)pv(kSolo) - 1);
    }
    void syncBand(int b)
    {
        pump::BandSpec s;
        s.shape = (pump::Shape)(int)pv((uint32_t)bandParam(b, kShape));
        s.freq = pv((uint32_t)bandParam(b, kFreq));
        s.gainDb = pv((uint32_t)bandParam(b, kGain));
        s.q = pv((uint32_t)bandParam(b, kQ));
        s.slope = kSlopeValues[pump::clampv((int)pv((uint32_t)bandParam(b, kSlope)), 0, 8)];
        engine.setBand(b, pv((uint32_t)bandParam(b, kUsed)) >= 0.5f, pv((uint32_t)bandParam(b, kBandBypass)) >= 0.5f, s,
                       (int)pv((uint32_t)bandParam(b, kPlace)), pv((uint32_t)bandParam(b, kDyn)) >= 0.5f,
                       pv((uint32_t)bandParam(b, kDynRange)), pv((uint32_t)bandParam(b, kDynThresh)));
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            bool bandDirty[kNumBands] = {};
            bool global = false;
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i)
            {
                if (!takeChanged(i)) continue;
                const int b = bandOf((int)i);
                if (b >= 0) bandDirty[b] = true; else global = true;
            }
            for (int b = 0; b < kNumBands; ++b) if (bandDirty[b]) syncBand(b);
            if (global)
            {
                engine.setGainScale(pv(kGainScale));
                engine.setOutput(pv(kOutGain));
                engine.setAutoGain(pv(kAutoGain) >= 0.5f);
                engine.setDynTimes(pv(kDynAttack), pv(kDynRelease));
                engine.setPhaseMode((int)pv(kPhase), (int)pv(kLinQuality));
                engine.setSolo((int)pv(kSolo) - 1);
            }
        }
        const int lat = engine.latency();
        if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }

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
            audition.set(pv(kAudition) >= 0.5f, pv(kAuditionFreq), pv(kAuditionQ));
            audition.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            for (int i = 0; i < n; ++i) postRing.push(0.5f * (l[i] + r[i]));
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    pump::Audition audition;
    int lastLatency = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEQ)
};

END_NAMESPACE_DISTRHO
