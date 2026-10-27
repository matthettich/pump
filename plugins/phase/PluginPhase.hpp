// pump PHASE: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "PhaseParams.hpp"
#include "PhaseEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::phase;

class PluginPhase : public PumpPlugin {
public:
    PluginPhase() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        if (const char* n = std::getenv("PUMP_TEST_NOTES"))   // headless screenshots: notes to hold, after the preset lands
            testNotes = n;
    }

    Engine engine;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak;

protected:
    const char* getLabel() const override { return "pump_phase"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        s.engine = (int)pv(kEngine); s.algo = (int)pv(kAlgo); s.feedback = (int)pv(kFeedback); s.chip = pv(kChip) >= 0.5f;
        s.tone = pv(kTone) * 0.01; s.lfoRate = pv(kLfoRate); s.lfoAm = pv(kLfoAm) * 0.01; s.lfoPm = pv(kLfoPm) * 0.01;
        s.wheelVib = pv(kWheelVib) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode);
        s.glide = pv(kGlide); s.bend = pv(kBend); s.transpose = (int)pv(kTranspose); s.volumeDb = pv(kVolume);
        s.pitchEnv = pv(kPitchEnv); s.pitchDecay = pv(kPitchDecay) * 0.001;
        for (int k = 0; k < kOps; ++k)
        {
            OpSettings& o = s.op[k];
            o.wave = (int)pv(op(k, kOpWave)); o.ratio = (int)pv(op(k, kOpRatio)); o.fine = pv(op(k, kOpFine));
            o.detune = pv(op(k, kOpDetune)); o.level = pv(op(k, kOpLevel));
            o.a = pv(op(k, kOpA)) * 0.001; o.d = pv(op(k, kOpD)) * 0.001; o.s = pv(op(k, kOpS)) * 0.01; o.r = pv(op(k, kOpR)) * 0.001;
            o.vel = pv(op(k, kOpVel)) * 0.01; o.am = pv(op(k, kOpAm)) >= 0.5f; o.fixed = pv(op(k, kOpFixed)) >= 0.5f;
        }
        s.pdWave1 = (int)pv(kPdWave1); s.pdWave1b = (int)pv(kPdWave1b); s.pdWave2 = (int)pv(kPdWave2); s.pdWave2b = (int)pv(kPdWave2b);
        s.pdDcw1 = pv(kPdDcw1) * 0.01; s.pdDcw2 = pv(kPdDcw2) * 0.01; s.pdKey = pv(kPdKey) * 0.01;
        s.pdA = pv(kPdA) * 0.001; s.pdD = pv(kPdD) * 0.001; s.pdS = pv(kPdS) * 0.01; s.pdR = pv(kPdR) * 0.001;
        s.pdDetune = pv(kPdDetune); s.pdOctave = (int)pv(kPdOctave); s.pdMod = (int)pv(kPdMod);
        engine.setSettings(s);
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        if (!testNotes.empty() && (testClock += frames) > (uint64_t)(0.5 * getSampleRate()))
        {
            for (const char* p = testNotes.c_str(); *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
            testNotes.clear();
        }
        keys.drain([&](uint8_t note, uint8_t vel) { engine.uiNote(note, vel); });
        uint32_t done = 0, ev = 0;
        while (done < frames)
        {
            const uint32_t n = std::min<uint32_t>(frames - done, 256u);
            int nm = 0;
            while (ev < midiEventCount && midiEvents[ev].frame < done + n)
            {
                const MidiEvent& m = midiEvents[ev++];
                if (m.size < 1 || m.size > 3 || nm >= 512) continue;
                midi[nm++] = { m.frame >= done ? m.frame - done : 0u, m.data[0], m.size > 1 ? m.data[1] : (uint8_t)0, m.size > 2 ? m.data[2] : (uint8_t)0 };
            }
            float* l = outputs[0] + done;
            float* r = outputs[1] + done;
            engine.process(l, r, (int)n, midi, nm);
            float pk = 0.0f;
            for (uint32_t i = 0; i < n; ++i) pk = std::max(pk, std::fabs(l[i]));
            outPeak.setMax(pk);
            done += n;
        }
    }

private:
    pump::synth::Midi midi[512] = {};
    std::string testNotes;
    uint64_t testClock = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginPhase)
};

END_NAMESPACE_DISTRHO
