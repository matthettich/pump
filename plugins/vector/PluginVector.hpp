// pump VECTOR: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "VectorParams.hpp"
#include "VectorEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::vector;

class PluginVector : public PumpPlugin {
public:
    PluginVector() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (const char* n = std::getenv("PUMP_TEST_NOTES")) testNotes = n;
    }

    // gain staging (the volume pass): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = 3.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_vector"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        for (int k = 0; k < kSources; ++k)
        {
            SrcSettings& q = s.src[(size_t)k];
            q.type = (int)pv(ps(k, kType)); q.wave = (int)pv(ps(k, kWaveP)); q.tune = (int)pv(ps(k, kTune)); q.fine = pv(ps(k, kFine)); q.level = pv(ps(k, kLevel)) * 0.01;
        }
        s.x = pv(kX) * 0.01; s.y = pv(kY) * 0.01; s.vecAmt = pv(kVecAmt) * 0.01; s.vecTime = pv(kVecTime) * 0.001; s.vecLoop = (int)pv(kVecLoopP);
        for (int k = 0; k < 4; ++k) { s.px[k] = pv(kP1x + 2 * k) * 0.01; s.py[k] = pv(kP1y + 2 * k) * 0.01; }
        s.seqRate = pv(kSeqRate) * 0.001; s.seqSteps = (int)pv(kSeqSteps); s.seqStride = (int)pv(kSeqStride); s.seqXfade = pv(kSeqXfade) * 0.01;
        s.seqLoop = (int)pv(kSeqLoopP); s.seqTranspose = (int)pv(kSeqTranspose);
        s.scanTension = pv(kScanTension) * 0.01; s.scanDamp = pv(kScanDamp) * 0.01; s.scanCentre = pv(kScanCentre) * 0.01; s.scanHammer = pv(kScanHammer) * 0.01; s.scanSpeed = pv(kScanSpeed) * 0.01;
        s.cutoff = pv(kCutoff); s.reso = pv(kReso) * 0.01; s.envCut = pv(kEnvCut) * 0.01; s.keytrack = pv(kKeytrack) * 0.01;
        s.fA = pv(kFA) * 0.001; s.fD = pv(kFD) * 0.001; s.fS = pv(kFS) * 0.01; s.fR = pv(kFR) * 0.001;
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001;
        s.lfoRate = pv(kLfoRate); s.lfoPitch = pv(kLfoPitch) * 0.01; s.lfoX = pv(kLfoX) * 0.01; s.lfoY = pv(kLfoY) * 0.01; s.lfoCut = pv(kLfoCut) * 0.01;
        s.velocity = pv(kVelocity) * 0.01; s.spread = pv(kSpread) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode);
        s.glide = pv(kGlide); s.octave = (int)pv(kOctave);
        s.volumeDb = pv(kVolume) + kTrimDb;
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
            outLimit.process(l, r, (int)n);
            float pl = 0.0f, pr = 0.0f;
            for (uint32_t i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += n;
        }
    }

private:
    pump::synth::Midi midi[512] = {};
    std::string testNotes;
    uint64_t testClock = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginVector)
};

END_NAMESPACE_DISTRHO
