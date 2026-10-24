// pump VERB: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "VerbParams.hpp"
#include "VerbEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::verb;

class PluginVerb : public PumpPlugin {
public:
    PluginVerb() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 64, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }

    // feeds for the UI: the input's peak and the wet signal's RMS every 256 samples (the envelope screen's live
    // trace), output peaks
    static constexpr int kLevelHop = 256;
    pump::ScopeRing inLevel { 1 << 10 }, wetLevel { 1 << 10 };
    pump::MeterValue outPeak[2];
    std::atomic<float> bpmNow { 120.0f };
    Engine engine;

protected:
    const char* getLabel() const override { return "pump_verb"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 64, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    // hosts (CLAP especially) read the latency right after activation, before the first block
    void reportLatency()
    {
        setLatency((uint32_t)engine.latency());
        bypass.setLatency(engine.latency());
    }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        s.mode = (int)pv(kMode);
        s.predelayMs = pv(kPredelay);
        s.sync = pv(kSync) >= 0.5f;
        s.note = (int)pv(kNote);
        s.size = pv(kSize);
        s.decay = pv(kDecay);
        s.diffusion = pv(kDiffusion);
        s.balance = pv(kBalance);
        s.bassMult = pv(kBassMult);
        s.crossover = pv(kCrossover);
        s.highDamp = pv(kHighDamp);
        s.dampFreq = pv(kDampFreq);
        s.lowCut = pv(kLowCut);
        s.highCut = pv(kHighCut);
        s.modDepth = pv(kModDepth);
        s.modRate = pv(kModRate);
        s.width = pv(kWidth);
        s.age = pv(kAge);
        s.envTimeMs = pv(kEnvTime);
        s.pitch = (int)pv(kPitch);
        s.shimmer = pv(kShimmer);
        s.duck = pv(kDuck);
        s.freeze = pv(kFreeze) >= 0.5f;
        s.mix = pv(kMix);
        s.outDb = pv(kOutGain);
        engine.setSettings(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        // the host tempo (120 BPM when it sends none; synced pre-delay follows it whether or not it plays)
        const pump::Transport tr = transport();
        engine.setTempo(tr.bpm);
        bpmNow.store((float)tr.bpm, std::memory_order_relaxed);
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
            // hops of kLevelHop samples, aligned to a running counter, so the level feeds tick at a steady rate
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)(kLevelHop - hopPos));
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            // a NaN / Inf from upstream would otherwise ring in the tank for good (the bypass path, captured above,
            // stays a plain pass-through)
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            for (int i = 0; i < n; ++i) inPk = std::max(inPk, std::max(std::fabs(l[i]), std::fabs(r[i])));
            engine.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            hopPos += n;
            if (hopPos >= kLevelHop)
            {
                inLevel.push(inPk);
                wetLevel.push(engine.takeWetRms());
                inPk = 0.0f;
                hopPos = 0;
            }
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    float inPk = 0.0f;
    int hopPos = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginVerb)
};

END_NAMESPACE_DISTRHO
