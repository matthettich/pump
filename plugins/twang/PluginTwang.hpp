// pump TWANG: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "TwangParams.hpp"
#include "../coil/CoilEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::twang;

class PluginTwang : public PumpPlugin {
public:
    PluginTwang() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)std::max<uint32_t>(getBufferSize(), 4096));
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
    }

    pump::coil::Engine engine;
    pump::MeterValue outPeak[2];
    pump::synth::NoteQueue keys;   // the window's keyboard (MIDI tuning)
    std::atomic<uint32_t> kickRequest { 0 };
    std::atomic<float> kickStrength { 1.0f };

protected:
    const char* getLabel() const override { return "pump_twang"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)std::max<uint32_t>(getBufferSize(), 4096));
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        bypass.setLatency(0);
    }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        pump::coil::Settings s;
        s.resonator = true;
        s.model = (int)pv(kModel); s.tension = pv(kTension); s.drip = pv(kDrip); s.decay = pv(kDecay); s.release = pv(kRelease);
        s.harmonic = pv(kHarmonic) * 0.01; s.spread = pv(kSpread) * 0.01; s.wobble = pv(kWobble) * 0.01; s.tone = pv(kTone) * 0.01; s.grit = pv(kGrit);
        s.tuning = (int)pv(kTuning); s.root = (int)pv(kRoot); s.chord = (int)pv(kChord); s.octave = (int)pv(kOctave);
        s.dwell = pv(kDrive); s.tank = pv(kTank) * 0.01; s.width = pv(kWidth) * 0.01; s.mix = pv(kMix) * 0.01; s.volumeDb = pv(kVolume);
        engine.setSettings(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        const uint32_t req = kickRequest.load(std::memory_order_acquire);
        if (req != kickSeen) { kickSeen = req; if (pv(kBypass) < 0.5f) engine.kick(kickStrength.load(std::memory_order_relaxed)); }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        int nk = 0;
        keys.drain([&](uint8_t note, uint8_t vel) { if (nk < 64) uiKeys[nk++] = { 0u, (uint8_t)(vel ? 0x90 : 0x80), note, vel }; });
        uint32_t done = 0, ev = 0;
        while (done < frames)
        {
            const uint32_t n = std::min<uint32_t>(frames - done, 256u);
            int nm = 0;
            if (done == 0) for (int k = 0; k < nk; ++k) midi[nm++] = uiKeys[k];
            while (ev < midiEventCount && midiEvents[ev].frame < done + n)
            {
                const MidiEvent& m = midiEvents[ev++];
                if (m.size < 1 || m.size > 3 || nm >= 512) continue;
                midi[nm++] = { m.frame >= done ? m.frame - done : 0u, m.data[0], m.size > 1 ? m.data[1] : (uint8_t)0, m.size > 2 ? m.data[2] : (uint8_t)0 };
            }
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, (int)n);
            pump::sanitizeBlock(l, (int)n);
            pump::sanitizeBlock(r, (int)n);
            engine.process(l, r, l, r, (int)n, midi, nm);
            bypass.apply(l, r, (int)n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (uint32_t i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += n;
        }
    }

private:
    pump::BypassMixer bypass;
    pump::synth::Midi midi[512 + 64] = {}, uiKeys[64] = {};
    uint32_t kickSeen = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginTwang)
};

END_NAMESPACE_DISTRHO
