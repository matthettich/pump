// pump GRAIN: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "GrainParams.hpp"
#include "GrainEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::grain;

class PluginGrain : public PumpPlugin {
public:
    PluginGrain() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 16, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }

    // feeds for the UI: the engine's buffer peaks and grain snapshot, output peaks, and the host tempo in use
    Engine engine;
    pump::MeterValue outPeak[2];
    std::atomic<float> bpmNow { 120.0f };
    std::atomic<bool> playingNow { false };

protected:
    const char* getLabel() const override { return "pump_grain"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 16, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    // hosts (CLAP especially) read the latency right after activation, before the first block; grains only read the
    // past, so it is always 0
    void reportLatency()
    {
        setLatency((uint32_t)engine.latency());
        bypass.setLatency(engine.latency());
    }

    void sync()
    {
        Settings s;
        s.positionMs = pv(kPosition);
        s.sizeMs = pv(kSize);
        s.density = pv(kDensity);
        s.spray = pv(kSpray);
        s.sync = pv(kSync) >= 0.5f;
        s.division = (int)pv(kDivision);
        s.envelope = (int)pv(kEnvelope);
        s.reverse = pv(kReverse);
        s.stereo = pv(kStereo);
        s.pitch = pv(kPitch);
        s.pitchSpread = pv(kPitchSpread);
        s.quantize = (int)pv(kQuantize);
        s.feedback = pv(kFeedback);
        s.filter = (int)pv(kFilter);
        s.cutoff = pv(kCutoff);
        s.resonance = pv(kResonance);
        s.space = pv(kSpace);
        s.motionRate = pv(kMotionRate);
        s.motionDepth = pv(kMotionDepth);
        s.morphX = pv(kMorphX);
        s.morphY = pv(kMorphY);
        s.freeze = pv(kFreeze) >= 0.5f;
        s.mix = pv(kMix);
        s.outDb = pv(kOutGain);
        engine.setSettings(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        const pump::Transport t = transport();
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        bpmNow.store((float)t.bpm, std::memory_order_relaxed);
        playingNow.store(t.playing, std::memory_order_relaxed);
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
            // the transport at this chunk's start (the musical position moves on with the samples already done)
            pump::Transport tc = t;
            if (tc.valid && done > 0) tc.ppq += (double)done / getSampleRate() * tc.bpm / 60.0;
            engine.setTransport(tc);
            bypass.capture(l, r, n);
            // a NaN / Inf from upstream would otherwise stay in the capture buffer and the space for good (the bypass
            // path, captured above, stays a plain pass-through)
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginGrain)
};

END_NAMESPACE_DISTRHO
