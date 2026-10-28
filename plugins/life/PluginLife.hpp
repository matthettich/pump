// pump LIFE: plugin (DSP side). Declared in a header so the UI can read its feeds and play its pads directly.
#pragma once
#include "PumpPlugin.hpp"
#include "LifeParams.hpp"
#include "LifeEngine.hpp"
#include <cstring>

START_NAMESPACE_DISTRHO

using namespace pump::life;

// the drawn board as text: 16 rows of 4 hex digits (bit x = column x)
inline std::string boardToText(const Board& b)
{
    std::string s; char h[8];
    for (int y = 0; y < kMaxGrid; ++y) { std::snprintf(h, sizeof h, "%04x", (unsigned)b[(size_t)y]); s += h; }
    return s;
}
inline Board boardFromText(const char* s)
{
    Board b {}; b.fill(0);
    if (!s) return b;
    const size_t n = std::strlen(s);
    for (int y = 0; y < kMaxGrid && (size_t)(y * 4 + 4) <= n; ++y)
    {
        char h[5] = { s[y * 4], s[y * 4 + 1], s[y * 4 + 2], s[y * 4 + 3], 0 };
        b[(size_t)y] = (uint16_t)std::strtoul(h, nullptr, 16);
    }
    return b;
}

class PluginLife : public PumpPlugin {
public:
    PluginLife() : PumpPlugin(params().data(), kNumParams, &presets(), 1)
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (const char* n = pump::testEnv("PUMP_TEST_RUN")) testRun = n[0] == '1';   // headless screenshots: run free
    }

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = +2.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_life"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        state.key = "board";
        state.label = "Drawn board";
        state.defaultValue = "";
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "board") == 0) { board = value ? value : ""; engine.setDrawn(boardFromText(board.c_str())); return; }
        PumpPlugin::setState(key, value);
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "board") == 0) return String(board.c_str());
        return PumpPlugin::getState(key);
    }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); engine.setDrawn(boardFromText(board.c_str())); }

    void sync()
    {
        Settings s;
        s.volumeDb = pv(kVolume) + kTrimDb; s.drive = pv(kDrive) * 0.01; s.fmAll = pv(kFmAll) * 0.01; s.decayAll = pv(kDecayAll) * 0.01;
        s.pitchRange = pv(kPitchRange); s.lfoRate = pv(kLfoRate); s.lfoDepth = pv(kLfoDepth) * 0.01; s.width = pv(kWidth) * 0.01;
        s.mode = (int)pv(kMode); s.rate = (int)pv(kRate); s.length = (int)pv(kLength); s.offset = (int)pv(kOffset); s.sens = pv(kSens) * 0.01;
        s.xWrap = (int)pv(kXWrap); s.yWrap = (int)pv(kYWrap); s.colour = (int)pv(kColour); s.cols = (int)pv(kCols); s.rows = (int)pv(kRows);
        s.rule = (int)pv(kRule); s.trigger = (int)pv(kTrigger); s.pattern = (int)pv(kPattern); s.reseed = pv(kReseed) >= 0.5f;
        s.shift = pv(kShift); s.grain = pv(kGrain) * 0.001; s.time = pv(kTime) * 0.001; s.feedback = pv(kFeedback) * 0.01;
        s.filter = pv(kFilter) * 0.01; s.fxDecay = pv(kFxDecay) * 0.001; s.mix = pv(kMix) * 0.01;
        s.fmLoop = pv(kFmLoop) >= 0.5f;
        if (testRun) s.mode = kModeFree;
        for (int t = 0; t < kTones; ++t)
        {
            ToneSettings& o = s.tone[t];
            o.pitch = pv(tone(t, kTPitch)); o.kick = pv(tone(t, kTKick)) * 0.01; o.fm = pv(tone(t, kTFm)) * 0.01; o.ring = pv(tone(t, kTRing)) * 0.01;
            o.decay = pv(tone(t, kTDecay)) * 0.001; o.level = pv(tone(t, kTLevel)) * 0.01; o.mute = pv(tone(t, kTMute)) >= 0.5f;
            o.fxSend = pv(fxSend(t)) * 0.01;
        }
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
        keys.drain([&](uint8_t note, uint8_t vel) { if (vel > 0 && note < kTones) engine.trigger(note, vel / 127.0); });
        pump::Transport tr = transport();
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
            engine.setTransport(tr);
            float* l = outputs[0] + done;
            float* r = outputs[1] + done;
            engine.process(l, r, (int)n, midi, nm);
            outLimit.process(l, r, (int)n);
            float pl = 0.0f, pr = 0.0f;
            for (uint32_t i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            tr.ppq += n * tr.bpm / 60.0 / getSampleRate();
            done += n;
        }
    }

private:
    pump::synth::Midi midi[512] = {};
    std::string board;
    bool testRun = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginLife)
};

END_NAMESPACE_DISTRHO
