// pump VOX: plugin (DSP side). Declared in a header so the UI can read the engine's feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "VoxParams.hpp"
#include "VoxEngine.hpp"
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
    int latencyNow() const { return engine.latency(); }
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
        bypass.prepare(sr, engine.maxLatencySamples() + 8, kBlock);
        lastLatency = engine.latency();
        bypass.setLatency(lastLatency);
        setLatency((uint32_t)lastLatency);
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
        }
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
            engine.process(bufL, bufR, scL, scR, bufL, bufR, n, midi, nm);
            // latency follows the mode (it changes once a switch has faded out)
            const int lat = engine.latency();
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
    float bufL[kBlock] = {}, bufR[kBlock] = {}, scL[kBlock] = {}, scR[kBlock] = {};
    MidiMsg midi[kMaxMidi] = {};
    int lastLatency = -1;
    uint16_t fNumInputs = DISTRHO_PLUGIN_NUM_INPUTS;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginVox)
};

END_NAMESPACE_DISTRHO
