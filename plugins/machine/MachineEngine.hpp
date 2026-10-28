// pump MACHINE engine: the drum machine firmware's six voices, running as they do on the machine. GPL-3.0-only (see
// fw/firmware/NOTICE.md).
//
// The firmware (fw/, compiled unchanged, through fw/machine_fw.c) renders 64-frame blocks of the whole machine at its
// own 44.14 kHz: the three drum voices, the snare, the cymbal and the hi-hat with their panning and the FX bus, the two
// stereo pairs summed. Blocks are made on demand and resampled (windowed sinc) to the host's rate; a hit lands on the
// next block, as on the machine.
//
// The firmware keeps its state in global variables: it was written for one machine. Each Engine owns a copy of that
// state; whichever instance runs puts its copy in place (under a process-wide lock) and takes it back out when another
// instance needs the firmware. The audio thread only ever try-locks: when another instance holds the firmware (it is
// powering on), this block is silent rather than the audio thread waiting. With one instance nothing is ever copied. Every instance powers on from the same
// start-up memory, so the same settings and hits always sound the same.
//
// MIDI: on channel 1 and 9-16 the seven sounds play at their own pitch, in order on the white keys up from C4 (C4 drum 1,
// D4 drum 2, E4 drum 3, F4 snare, G4 cymbal, A4 closed hat, B4 open hat), and so do the General MIDI drum notes (the
// sounds' notes before 2026-10-27, so older patterns still play: 36 35 drum 1; 41 43 45 47 drum 2; 37 39 48 50 drum 3;
// 38 40 snare; 42 44 closed hat; 46 open hat; 49 51 52 55 57 59 cymbal). Channels 2-8 play sounds 1-7 chromatically
// (60 = the sound's own pitch), the way the machine's per-voice MIDI channels do.
#pragma once

#include "dsp/Resampler.hpp"
#include "dsp/Synth.hpp"
#include "dsp/Util.hpp"
#include "MachineParams.hpp"
#include "fw/machine_fw.h"
#include <array>
#include <atomic>
#include <mutex>

namespace pump { namespace machine {

// which sound a note plays (-1 none): its own note (C4 up the white keys, 60-71), or a General MIDI drum note
inline int soundForNote(int note)
{
    if (const int w = whiteKeyIndex(note); w >= 0 && w < kSounds) return w;
    switch (note)
    {
    case 35: case 36: return 0;
    case 41: case 43: case 45: case 47: return 1;
    case 37: case 39: case 48: case 50: return 2;
    case 38: case 40: return 3;
    case 49: case 51: case 52: case 55: case 57: case 59: return 4;
    case 42: case 44: return 5;
    case 46: return 6;
    default: return -1;
    }
}
// the firmware renders OUTPUT_DMA_SIZE frames per block (64 in this build; machine_fw.c checks it at compile time)
constexpr int kMaxFwBlock = 64;

inline int noteForSound(int s) { return whiteKeyNote(clampv(s, 0, kSounds - 1)); }   // C4 D4 E4 F4 G4 A4 B4

class Engine {
public:
    uint32_t lastHit() const { return lastHitFeed.load(std::memory_order_relaxed); }   // pad | count << 8
    Engine() { for (auto& v : values) v.store(-1); }
    ~Engine()
    {
        std::lock_guard<std::mutex> g(lock());
        if (owner() == this) owner() = nullptr;
    }

    // power on with these parameter values (fw parameter number -> value), then settle the knob smoothing
    void prepare(double hostRate, int maxBlock, const int* fwValues)
    {
        sr = hostRate;
        const double fwRate = mfw_rate();
        for (int c = 0; c < 2; ++c) rs[c].prepare(fwRate, sr, mfw_block() + 8, 24);
        state.resize(mfw_state_size());   // sized here, so handing the firmware over never allocates on the audio thread
        std::lock_guard<std::mutex> g(lock());
        takeFirmware();
        // power on from the firmware's start-up memory: its init functions don't reset everything (a voice's phase,
        // the FX buffers...), so without this an instance would inherit the last one's leftovers
        static std::vector<uint8_t> pristine;
        if (pristine.empty()) { pristine.resize(mfw_state_size()); mfw_state_save(pristine.data()); }
        mfw_state_load(pristine.data());
        mfw_init(0x2545F491u);
        mfw_init_kit();
        uint8_t* kit = mfw_params();
        for (int p = 0; p < kFwNumParams; ++p) if (fwValues[p] >= 0) kit[p] = (uint8_t)fwValues[p];
        mfw_load();
        float l[kMaxFwBlock], r[kMaxFwBlock];
        for (int b = 0; b < (int)(0.35 * fwRate / mfw_block()); ++b) mfw_render(l, r);   // the knobs' smoothing settles
        for (int p = 0; p < kFwNumParams; ++p) { applied[(size_t)p] = fwValues[p]; values[(size_t)p].store(fwValues[p]); }
        volS.setRamp(std::max(1, (int)(0.02 * sr)));
        volS.reset(dbToGain(volumeDb));
        (void)maxBlock;
    }
    // any thread: a parameter's new value (fw parameter number), applied at the next block
    void setParam(int fw, int value) { if (fw > 0 && fw < kFwNumParams) { values[(size_t)fw].store(value); dirty.store(true); } }
    void setVolume(double db) { volumeDb = db; }
    void setTempo(double bpm) { tempo = bpm; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    float flash(int s) const { return flashFeed[(size_t)clampv(s, 0, kSounds - 1)].load(std::memory_order_relaxed); }
    void clearFlash(int s, float to) { flashFeed[(size_t)clampv(s, 0, kSounds - 1)].store(to, std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev, const std::pair<int, int>* hits = nullptr, int nhits = 0)
    {
        std::unique_lock<std::mutex> g(lock(), std::try_to_lock);
        if (!g.owns_lock())
        {
            // another instance is powering on (a moment, at load): this block is silent and its hits are skipped
            std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f);
            for (int i = 0; i < n; ++i) outRing.push(0.0f);
            return;
        }
        takeFirmware();
        mfw_set_bpm((int)std::lround(clampv(tempo, 20.0, 400.0)));
        if (dirty.exchange(false))
            for (int p = 1; p < kFwNumParams; ++p) { const int v = values[(size_t)p].load(); if (v >= 0 && v != applied[(size_t)p]) { mfw_set(p, v); applied[(size_t)p] = v; } }
        for (int k = 0; k < nhits; ++k) trigger(hits[k].first, hits[k].second, 60);
        volS.setTarget(dbToGain(volumeDb));
        int i = 0, e = 0;
        while (i < n)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            const int until = e < nev ? std::min(n, (int)ev[e].offset) : n;
            for (; i < until; ++i)
            {
                while (!rs[0].canPull()) renderBlock();
                const double vol = volS.next();
                L[i] = (float)(rs[0].pull() * vol); R[i] = (float)(rs[1].pull() * vol);
            }
        }
        while (e < nev) handle(ev[e++]);
        sanitizeBlock(L, n); sanitizeBlock(R, n);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }

private:
    static std::mutex& lock() { static std::mutex m; return m; }
    static Engine*& owner() { static Engine* o = nullptr; return o; }
    // put this instance's firmware state in place (the lock is held)
    void takeFirmware()
    {
        Engine*& o = owner();
        if (o == this) return;
        if (o && o->state.size() >= mfw_state_size()) { mfw_state_save(o->state.data()); o->hasState = true; }
        if (hasState) mfw_state_load(state.data());
        o = this;
    }
    void renderBlock()
    {
        float l[kMaxFwBlock], r[kMaxFwBlock];
        mfw_render(l, r);
        for (int i = 0; i < mfw_block(); ++i) { rs[0].push(l[i]); rs[1].push(r[i]); }
    }
    void trigger(int sound, int vel, int note)
    {
        if (sound < 0 || sound >= kSounds) return;
        noteLastHit(sound);
        mfw_trigger(sound, clampv(vel, 1, 127), clampv(note, 0, 127));
        flashFeed[(size_t)sound].store(vel / 127.0f, std::memory_order_relaxed);
    }
    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0, ch = m.status & 0x0F;
        if (st != 0x90 || m.d2 == 0) return;
        if (ch >= 1 && ch <= 7) trigger(ch - 1, m.d2, m.d1);   // channels 2-8: sounds 1-7, chromatic
        else trigger(soundForNote(m.d1), m.d2, 60);
    }

    double sr = 48000.0, volumeDb = 0.0, tempo = 120.0;
    StreamResampler rs[2];
    LinearSmoother volS;
    std::vector<uint8_t> state;   // this instance's firmware memory while another instance runs
    bool hasState = false;
    std::array<std::atomic<int>, kFwNumParams> values {};
    std::array<int, kFwNumParams> applied {};
    std::atomic<bool> dirty { false };
    std::array<std::atomic<float>, kSounds> flashFeed {};
    // the last pad hit (by MIDI or the window): pad | count << 8, for the window to follow
    std::atomic<uint32_t> lastHitFeed { 0u };
    void noteLastHit(int p) { lastHitFeed.store((uint32_t)p | (((lastHitFeed.load(std::memory_order_relaxed) >> 8) + 1u) << 8), std::memory_order_relaxed); }
};

}} // namespace pump::machine
