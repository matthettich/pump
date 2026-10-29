// pump BREAK: plugin (DSP side). Declared in a header so the UI can read its feeds and the break playing, play slices
// and hold effects directly. A loaded break is a state holding its file path ("file"), read in setState (not the audio
// thread) and handed over through pump::Retained.
#pragma once
#include "dsp/Retained.hpp"
#include "dsp/Wav.hpp"
#include "PumpPlugin.hpp"
#include "BreakParams.hpp"
#include "BreakEngine.hpp"
#include <mutex>

START_NAMESPACE_DISTRHO

using namespace pump::brk;

class PluginBreak : public PumpPlugin {
public:
    PluginBreak() : PumpPlugin(params().data(), kNumParams, &presets(), 1)
    {
        Breaks::get().retain();
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (pump::testEnv("PUMP_TEST_NOTES")) preview.store(true);   // headless screenshots: the pattern runs from the start
    }
    ~PluginBreak() override { Breaks::get().release(); }

    // gain staging: the built-in breaks sit near -14 dBFS RMS; VOLUME 0 dB puts them near -18
    static constexpr float kTrimDb = -4.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::MeterValue outPeak[2];
    std::atomic<bool> preview { false };          // the window's PLAY (never saved)
    std::atomic<int> padSlice { -1 };              // the window's slice to play now (0-based)
    std::atomic<unsigned> uiHeld { 0u };           // effects held in the window or by the computer keys (bits)

    // what plays now (the window draws it): nullptr while a built-in is still being made, or with no file
    const Source* sourceNow() const { return nowSrc.load(std::memory_order_acquire); }
    std::string fileInfo() const { std::lock_guard<std::mutex> g(lock); return info; }

protected:
    const char* getLabel() const override { return "pump_break"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        state.key = "file"; state.label = "Break File"; state.defaultValue = ""; state.hints = kStateIsFilenamePath;
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "file") != 0) { PumpPlugin::setState(key, value); return; }
        path = value ? value : "";
        std::shared_ptr<Source> s;
        std::string inf;
        try   // setState() must never throw into the host
        {
            if (!path.empty())
            {
                const pump::WavData w = pump::readWav(path, (size_t)(192000.0 * 40.0));
                if (!w.ok()) inf = "couldn't load: " + w.error;
                else
                {
                    // stereo (a mono file on both sides), at most 30 seconds
                    s = std::make_shared<Source>();
                    const size_t n = std::min(w.channels[0].size(), (size_t)(w.sampleRate * 30.0));
                    s->L.assign(w.channels[0].begin(), w.channels[0].begin() + (long)n);
                    const auto& rc = w.channels.size() > 1 ? w.channels[1] : w.channels[0];
                    s->R.assign(rc.begin(), rc.begin() + (long)n);
                    s->sr = w.sampleRate; s->bars = 0;   // its tempo comes from FILE LENGTH
                    std::string nm = path;
                    const size_t sl = nm.find_last_of("/\\"); if (sl != std::string::npos) nm = nm.substr(sl + 1);
                    s->name = nm; inf = nm;
                    if (n < 64) { s.reset(); inf = "the file is too short"; }
                }
            }
        }
        catch (...) { s.reset(); inf = "couldn't load the file"; }
        file.publish(std::move(s));
        std::lock_guard<std::mutex> g(lock);
        info = inf;
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "file") == 0) return String(path.c_str());
        return PumpPlugin::getState(key);
    }

    void activate() override
    {
        Breaks::get().start((int)pv(kSource));
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
    }

    void sync()
    {
        Settings s;
        s.fit = pv(kFit) >= 0.5f; s.fileBars = (int)pv(kFileBars); s.transpose = pv(kTranspose); s.tone = pv(kTone) * 0.01; s.drive = pv(kDrive) * 0.01;
        s.volumeDb = pv(kVolume) + kTrimDb; s.seqMode = (int)pv(kSeqMode); s.length = (int)pv(kSeqLength); s.rate = (int)pv(kSeqRate); s.swing = pv(kSwing) * 0.01;
        for (int k = 0; k < kFx; ++k) { s.fxAmount[k] = pv((uint32_t)(kFxAmount1 + k)) * 0.01; s.fxHold[k] = pv((uint32_t)(kFxHold1 + k)) >= 0.5f; }
        s.pattern = patternFrom([this](int i) { return pv((uint32_t)i); });
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
        // the break: a built-in (once it is made) or the file
        const Source* f = file.acquire();   // every block: acknowledges what is in use
        const int sel = (int)pv(kSource);
        const Source* src = sel == kSourceFile ? f : Breaks::get().ready(sel);
        nowSrc.store(src, std::memory_order_release);
        engine.setSource(src);
        engine.setPreview(preview.load(std::memory_order_relaxed));
        const unsigned uh = uiHeld.load(std::memory_order_relaxed);
        for (int k = 0; k < kFx; ++k) engine.setHeld(k, (uh >> k) & 1u);
        const int ps = padSlice.exchange(-1, std::memory_order_relaxed);
        pump::Transport tr = transport();
        engine.setTransport(tr);
        if (ps >= 0) engine.playSlice(ps, 1.0);
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
    std::string path, info;
    pump::Retained<Source> file;
    std::atomic<const Source*> nowSrc { nullptr };
    mutable std::mutex lock;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBreak)
};

END_NAMESPACE_DISTRHO
