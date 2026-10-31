// pump VOX: plugin (DSP side). Declared in a header so the UI can read the engine's feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "VoxParams.hpp"
#include "VoxEngine.hpp"
#include "VoxFx.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::vox;

class PluginVox : public PumpPlugin {
public:
    PluginVox() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        prepareAll();
    }

    Engine engine;
    VoiceFx fx;                                   // ENGINE Voice FX
    pump::MeterValue fxOutPeak[2];                // its output (the vocoder has its own)
    int latencyNow() const { return lastLatency >= 0 ? lastLatency : engine.latency(); }
    bool voiceFxActive() const { return activeEngine.load() == kEngineVoiceFx; }
    bool sidechainConnected() const { return fNumInputs >= 4; }

protected:
    const char* getLabel() const override { return "pump_vox"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    // Inputs 3-4: the sidechain, its own stereo group flagged as sidechain (a VST3 aux bus, a non-main CLAP port;
    // hosts that leave it unconnected pass silence there).
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        if (input && index >= 2)
        {
            port.hints = kAudioPortIsSidechain;
            port.name = index == 2 ? "Sidechain L" : "Sidechain R";
            port.symbol = index == 2 ? "sidechain_l" : "sidechain_r";
            port.groupId = kGroupSidechain;
            return;
        }
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }
    void initPortGroup(uint32_t groupId, PortGroup& group) override
    {
        if (groupId == kGroupSidechain) { group.name = "Sidechain"; group.symbol = "sidechain"; return; }
        Plugin::initPortGroup(groupId, group);
    }
    // AU only (DISTRHO_PLUGIN_EXTRA_IO): the host picked plain stereo, so there is no sidechain input.
    void ioChanged(uint16_t numInputs, uint16_t) override { fNumInputs = numInputs; }

    void activate() override { prepareAll(); }

    void prepareAll()
    {
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        engine.setSettings(settings());
        engine.prepare(sr, (int)getBufferSize());
        fx.set(fxSettings());
        fx.prepare(sr);
        bypass.prepare(sr, std::max(engine.maxLatencySamples(), fx.latency()) + 8, kBlock);
        activeEngine.store((int)pv(kEngine));
        fade = 1.0f;
        lastLatency = curLatency();
        bypass.setLatency(lastLatency);
        setLatency((uint32_t)lastLatency);
    }

    int curLatency() const { return activeEngine.load() == kEngineVoiceFx ? fx.latency() : engine.latency(); }

    FxSettings fxSettings() const
    {
        FxSettings f;
        f.pitchOn = pv(kFxPitchOn) >= 0.5f; f.pitch = pv(kFxPitch); f.formant = pv(kFxFormant); f.sub = pv(kFxSub);
        f.gritOn = pv(kFxGritOn) >= 0.5f; f.fry = pv(kFxFry); f.rasp = pv(kFxRasp); f.rattle = pv(kFxRattle); f.rattleHz = pv(kFxRattleHz);
        f.drive = pv(kFxDrive); f.tone = pv(kFxTone);
        f.robotOn = pv(kFxRobotOn) >= 0.5f; f.mono = pv(kFxMono); f.note = (int)pv(kFxNote); f.glideMs = pv(kFxGlide);
        f.ring = pv(kFxRing); f.ringHz = pv(kFxRingHz); f.comb = pv(kFxComb); f.crush = pv(kFxCrush);
        f.charOn = pv(kFxCharOn) >= 0.5f; f.filter = (int)pv(kFxFilter); f.stack = (int)pv(kFxStack); f.stackMix = pv(kFxStackMix);
        f.doubler = pv(kFxDouble); f.slap = pv(kFxSlap); f.slapMs = pv(kFxSlapMs);
        f.mix = pv(kFxMix); f.outDb = pv(kOutGain);
        return f;
    }

    Settings settings() const
    {
        Settings s;
        s.mode = (int)pv(kMode); s.route = (int)pv(kRoute); s.bandsIndex = (int)pv(kBands);
        s.lowHz = pv(kLow); s.highHz = pv(kHigh); s.width = pv(kWidth); s.detail = pv(kDetail); s.fftIndex = (int)pv(kFftSize);
        s.tube = pv(kTube); s.driveDb = pv(kDrive); s.smear = pv(kSmear);
        s.formant = pv(kFormant); s.attackMs = pv(kAttack); s.releaseMs = pv(kRelease); s.flatten = pv(kFlatten);
        s.emphasis = pv(kEmphasis); s.sibilance = pv(kSibilance); s.noise = pv(kNoise); s.gateDb = pv(kGate); s.freeze = pv(kFreeze) >= 0.5f;
        s.source = (int)pv(kSource); s.wave = (int)pv(kWave); s.octave = (int)pv(kOctave); s.chord = (int)pv(kChord);
        s.key = (int)pv(kKey); s.scale = (int)pv(kScale); s.glideMs = pv(kGlide); s.detune = pv(kDetune);
        s.spread = pv(kSpread); s.ensemble = pv(kEnsemble); s.voiceDry = pv(kVoiceDry); s.carrierDry = pv(kCarrierDry); s.outDb = pv(kOutGain);
        return s;
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            engine.setSettings(settings());
            fx.set(fxSettings());
        }
        const int wantEngine = (int)pv(kEngine);
        const bool bypassed = pv(kBypass) >= 0.5f;
        const bool hasSc = fNumInputs >= 4 && inputs[2] != nullptr && inputs[3] != nullptr;
        uint32_t ev = 0, done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)kBlock);
            // copy everything first: hosts may hand us aliased in/out (and even sidechain) buffers
            for (int i = 0; i < n; ++i)
            {
                bufL[i] = inputs[0][done + i]; bufR[i] = inputs[1][done + i];
                scL[i] = hasSc ? inputs[2][done + i] : 0.0f; scR[i] = hasSc ? inputs[3][done + i] : 0.0f;
            }
            // this chunk's MIDI (short messages only), frames relative to the chunk
            int nm = 0;
            while (ev < midiEventCount && midiEvents[ev].frame < done + (uint32_t)n)
            {
                const MidiEvent& m = midiEvents[ev++];
                if (m.size < 1 || m.size > 3 || nm >= kMaxMidi) continue;
                midi[nm++] = { m.frame >= done ? m.frame - done : 0u, m.data[0], m.size > 1 ? m.data[1] : (uint8_t)0, m.size > 2 ? m.data[2] : (uint8_t)0 };
            }
            bypass.capture(bufL, bufR, n);
            for (int k = 0; k < nm; ++k) fx.midi(midi[k].status, midi[k].d1, midi[k].d2);
            if (activeEngine.load() == kEngineVoiceFx)
            {
                // the voice, as the routing says: the input, the sidechain, or the right channel (Split L/R)
                const int route = (int)pv(kRoute);
                for (int i = 0; i < n; ++i)
                    voice[i] = route == kRouteSidechainVoice ? 0.5f * (scL[i] + scR[i]) : route == kRouteSplit ? bufR[i] : 0.5f * (bufL[i] + bufR[i]);
                fx.process(voice, bufL, bufR, n);
                float pl = 0.0f, pr = 0.0f;
                for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(bufL[i])); pr = std::max(pr, std::fabs(bufR[i])); }
                fxOutPeak[0].setMax(pl); fxOutPeak[1].setMax(pr);
            }
            else engine.process(bufL, bufR, scL, scR, bufL, bufR, n, midi, nm);
            // ENGINE switches: fade out (10 ms), switch, fade back in
            {
                const float step = (float)n / (0.01f * (float)getSampleRate());
                if (wantEngine != activeEngine.load())
                {
                    for (int i = 0; i < n; ++i) { const float g = std::max(0.0f, fade - step * (float)i / (float)n); bufL[i] *= g; bufR[i] *= g; }
                    fade = std::max(0.0f, fade - step);
                    if (fade <= 0.0f) { activeEngine.store(wantEngine); fx.reset(); engine.reset(); }
                }
                else if (fade < 1.0f)
                {
                    for (int i = 0; i < n; ++i) { const float g = std::min(1.0f, fade + step * (float)i / (float)n); bufL[i] *= g; bufR[i] *= g; }
                    fade = std::min(1.0f, fade + step);
                }
            }
            // latency follows the mode and the engine (it changes once a switch has faded out)
            const int lat = curLatency();
            if (lat != lastLatency) { bypass.setLatency(lat); lastLatency = lat; setLatency((uint32_t)lat); }
            bypass.apply(bufL, bufR, n, bypassed);
            std::memcpy(outputs[0] + done, bufL, sizeof(float) * (size_t)n);
            std::memcpy(outputs[1] + done, bufR, sizeof(float) * (size_t)n);
            done += (uint32_t)n;
        }
    }

private:
    static constexpr uint32_t kGroupSidechain = 0;
    static constexpr int kBlock = 256, kMaxMidi = 512;
    pump::BypassMixer bypass;
    float bufL[kBlock] = {}, bufR[kBlock] = {}, scL[kBlock] = {}, scR[kBlock] = {}, voice[kBlock] = {};
    std::atomic<int> activeEngine { kEngineVocoder };
    float fade = 1.0f;
    MidiMsg midi[kMaxMidi] = {};
    int lastLatency = -1;
    uint16_t fNumInputs = DISTRHO_PLUGIN_NUM_INPUTS;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginVox)
};

END_NAMESPACE_DISTRHO
