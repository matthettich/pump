// pump ECHO: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "EchoParams.hpp"
#include "EchoEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::echo;

class PluginEcho : public PumpPlugin {
public:
    PluginEcho() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 64, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }

    Engine engine;   // the UI reads its feeds (times, tempo, wobble, duck, peaks)

protected:
    const char* getLabel() const override { return "pump_echo"; }
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

    void sync()
    {
        Settings s;
        s.mode = (int)pv(kMode);
        s.timeL = pv(kTimeL);
        s.timeR = pv(kTimeR);
        s.sync = pv(kSync) >= 0.5f;
        s.noteL = (int)pv(kNoteL);
        s.noteR = (int)pv(kNoteR);
        s.link = pv(kLink) >= 0.5f;
        s.feedback = pv(kFeedback);
        s.route = (int)pv(kStereo);
        s.width = pv(kWidth);
        s.modDepth = pv(kModDepth);
        s.modRate = pv(kModRate);
        s.age = pv(kAge);
        s.drive = pv(kDrive);
        s.lowCut = pv(kLowCut);
        s.highCut = pv(kHighCut);
        s.diffuse = pv(kDiffuse);
        s.duck = pv(kDuck);
        s.freeze = pv(kFreeze) >= 0.5f;
        s.pitch = pv(kPitch);
        s.fine = pv(kFine);
        s.pitchMode = (int)pv(kPitchMode);
        s.mix = pv(kMix);
        s.outDb = pv(kOutGain);
        engine.setSettings(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        // the host tempo (120 when there is none); synced times follow it whether or not the transport plays
        engine.setTempo(transport().bpm);
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
            // a NaN / Inf from upstream would otherwise circulate in the delay loop (the bypass path, captured above,
            // stays a plain pass-through)
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEcho)
};

END_NAMESPACE_DISTRHO
