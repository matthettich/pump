// pump CHOP: plugin (DSP side). Declared in a header so the UI can read its feeds and hold its pads directly.
#pragma once
#include "PumpPlugin.hpp"
#include "ChopParams.hpp"
#include "ChopEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::chop;

class PluginChop : public PumpPlugin {
public:
    PluginChop() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        bypass.setLatency(0);
        if (std::getenv("PUMP_TEST_AUDIO") && std::getenv("PUMP_TEST_CHOP")) testPads = true;   // screenshots: the pads play by themselves
    }

    Engine engine;
    pump::MeterValue outPeak[2];
    // the pads the window holds (mouse and computer keys); momentary, never parameters
    std::atomic<uint32_t> uiHeld { 0 };
    // the last MIDI note that came in (for LEARN): note | count << 8
    std::atomic<uint32_t> lastNote { 0 };

protected:
    const char* getLabel() const override { return "pump_chop"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        midiHeld = 0;
    }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        for (int p = 0; p < kPads; ++p)
        {
            PadSettings& q = s.pad[(size_t)p];
            q.slice = (int)pv(pp(p, kSlice)); q.pitch = (int)pv(pp(p, kPitch)); q.reverse = pv(pp(p, kReverse)) >= 0.5f; q.fx = (int)pv(pp(p, kFx));
            q.amount = pv(pp(p, kFxAmount)) * 0.01; q.levelDb = pv(pp(p, kLevel)); q.note = (int)pv(pp(p, kNote)); q.key = (int)pv(pp(p, kKey));
            notes[p] = q.note;
        }
        s.slices = pv(kSlices) >= 0.5f ? 16 : 8; s.length = (int)pv(kLengthP); s.quant = (int)pv(kQuantise);
        s.hold = pv(kHold) >= 0.5f; s.loopTwo = pv(kLoopTwo) >= 0.5f; s.bpm = pv(kBpm); s.mix = pv(kMix) * 0.01; s.outDb = pv(kOutGain);
        engine.setSettings(s);
    }

    // a pad's note down and up (any channel); all notes off lets go of everything
    void midi(const MidiEvent& m)
    {
        if (m.size < 3) return;
        const uint8_t st = m.data[0] & 0xF0, note = m.data[1], vel = m.data[2];
        const bool on = st == 0x90 && vel > 0, off = st == 0x80 || (st == 0x90 && vel == 0);
        if (on) lastNote.store((uint32_t)note | ((lastNote.load(std::memory_order_relaxed) >> 8) + 1) << 8, std::memory_order_relaxed);
        for (int p = 0; p < kPads; ++p) if (notes[p] == note) { if (on) midiHeld |= 1u << p; else if (off) midiHeld &= ~(1u << p); }
        if (st == 0xB0 && (note == 123 || note == 120)) midiHeld = 0;
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
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        const bool bypassed = pv(kBypass) >= 0.5f;
        uint32_t done = 0, ev = 0;
        while (done < frames)
        {
            // split at MIDI events (a pad goes down on its sample)
            while (ev < midiEventCount && midiEvents[ev].frame <= done) midi(midiEvents[ev++]);
            uint32_t end = std::min<uint32_t>(frames, done + 4096);
            if (ev < midiEventCount) end = std::min<uint32_t>(end, std::max<uint32_t>(midiEvents[ev].frame, done + 1));
            const int n = (int)(end - done);
            float* l = L + done;
            float* r = R + done;
            uint32_t mask = midiHeld | uiHeld.load(std::memory_order_relaxed);
            if (testPads) { testClock += n; const int k = (int)(testClock / (getSampleRate() * 0.5)) % 12; mask |= k < 8 && k % 2 == 0 ? 1u << ((k * 3) % 8) : 0u; }
            engine.setHeld(mask);
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            bypass.apply(l, r, n, bypassed);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done = end;
        }
        while (ev < midiEventCount) midi(midiEvents[ev++]);
    }

private:
    pump::BypassMixer bypass;
    uint32_t midiHeld = 0;
    int notes[kPads] = {};
    bool testPads = false;
    uint64_t testClock = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChop)
};

END_NAMESPACE_DISTRHO
