// pump BOUNCE: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "BounceParams.hpp"
#include "BounceEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::bounce;

class PluginBounce : public PumpPlugin {
public:
    PluginBounce() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 64, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
        testDrops = pump::testEnv("PUMP_TEST_DROPS") != nullptr;
    }

    Engine engine;   // the UI reads its feeds (the ball, the tempo)
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_bounce"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 64, (int)std::max<uint32_t>(getBufferSize(), 8192));
        dropSeen = dropEdges.load(std::memory_order_acquire);
        reportLatency();
    }
    // hosts (CLAP especially) read the latency right after activation, before the first block
    void reportLatency()
    {
        setLatency((uint32_t)engine.latency());
        bypass.setLatency(engine.latency());
    }

    // The DROP button is a momentary parameter: every rising edge counts, so a click that is up again before the next
    // block still fires (the value alone would read 0 by then).
    void setParameterValue(uint32_t index, float value) override
    {
        if (index == (uint32_t)kDrop && value >= 0.5f && pv(kDrop) < 0.5f) dropEdges.fetch_add(1, std::memory_order_release);
        PumpPlugin::setParameterValue(index, value);
    }

    void sync()
    {
        Settings s;
        s.mode = (int)pv(kMode);
        s.timeMs = pv(kTime);
        s.sync = pv(kSync) >= 0.5f;
        s.note = (int)pv(kNote);
        s.gravity = pv(kGravity);
        s.rise = pv(kRise) >= 0.5f;
        s.bounces = (int)pv(kBounces);
        s.dampDb = pv(kDamp);
        s.pitch = pv(kPitch);
        s.tone = pv(kTone);
        s.spread = pv(kSpread);
        s.jitter = pv(kJitter);
        s.sliceMs = pv(kSlice);
        s.fit = pv(kFit) >= 0.5f;
        s.trigger = (int)pv(kTrigger);
        s.thresholdDb = pv(kThreshold);
        s.every = (int)pv(kEvery);
        s.feedback = pv(kFeedback);
        s.mix = pv(kMix);
        s.outDb = pv(kOutGain);
        engine.setSettings(s);
    }

    void midi(const MidiEvent& m)
    {
        if (m.size < 3) return;
        if ((m.data[0] & 0xF0) == 0x90 && m.data[2] > 0) engine.noteOn((float)m.data[2] * (1.0f / 127.0f));
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        engine.setTransport(transport());
        const uint32_t edges = dropEdges.load(std::memory_order_acquire);
        if (edges != dropSeen)
        {
            const uint32_t k = std::min<uint32_t>(edges - dropSeen, 4u);
            dropSeen = edges;
            if (pv(kBypass) < 0.5f) for (uint32_t i = 0; i < k; ++i) engine.fire();
        }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);

        uint32_t done = 0, ev = 0;
        while (done < frames)
        {
            // split at MIDI events (a note-on drops on its sample)
            while (ev < midiEventCount && midiEvents[ev].frame <= done) midi(midiEvents[ev++]);
            uint32_t end = std::min<uint32_t>(frames, done + 4096);
            if (ev < midiEventCount) end = std::min<uint32_t>(end, std::max<uint32_t>(midiEvents[ev].frame, done + 1));
            const int n = (int)(end - done);
            float* l = L + done;
            float* r = R + done;
            if (testDrops)   // headless screenshots: a drop every 2.5 s
            {
                testClock += n;
                if (testClock >= (uint64_t)(2.5 * getSampleRate())) { testClock = 0; engine.fire(); }
            }
            bypass.capture(l, r, n);
            // a NaN / Inf from upstream would otherwise circulate in the line (the bypass path, captured above,
            // stays a plain pass-through)
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done = end;
        }
        while (ev < midiEventCount) midi(midiEvents[ev++]);
    }

private:
    pump::BypassMixer bypass;
    std::atomic<uint32_t> dropEdges { 0 };
    uint32_t dropSeen = 0;
    bool testDrops = false;
    uint64_t testClock = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBounce)
};

END_NAMESPACE_DISTRHO
