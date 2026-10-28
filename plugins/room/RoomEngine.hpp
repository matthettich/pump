// pump ROOM: the engine. A stereo FIR per speaker, designed from the profile (common/dsp/RoomCorrect.hpp) at the
// host's rate: the correction scaled by AMOUNT, the preamp that keeps its boosts from clipping, the time alignment;
// minimum phase (latency: one convolution block) or linear phase (plus half the filter, and the profile's phase
// correction when it has one).
//
// Filters are designed on a worker thread of the engine and handed to the convolver, which crossfades them in over
// one block; the audio thread never allocates, locks or designs. Without a profile the signal passes straight through
// (an identity filter, so the latency doesn't change when one is loaded).
#pragma once

#include "dsp/RoomCorrect.hpp"
#include "dsp/FFT.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace pump { namespace roomfx {

struct Settings {
    double amount = 1.0;   // 0..1
    bool linear = false;
    bool align = true;
};

// What one design is made of (copied under the lock, designed outside it).
struct DesignInput {
    room::Profile profile;
    bool hasProfile = false;
    Settings s;
    double fs = 48000.0;
};

// The filter pair for a design, and what the window shows about it.
struct Design {
    std::vector<float> fir[2];
    double preampDb = 0.0;
    double delayMs[2] = { 0.0, 0.0 };
    int latency = 0;   // samples, the convolver's block included
    bool phase = false;   // the profile's phase correction is in these filters
};

inline Design designFilters(const DesignInput& in, int block)
{
    Design d;
    const int taps = room::correctionTaps(in.fs);
    if (!in.hasProfile)
    {
        d.fir[0].assign(1, 1.0f);
        d.fir[1].assign(1, 1.0f);
        d.latency = block;
        return d;
    }
    const room::ProfileChannel* side[2] = { in.profile.side('L'), in.profile.side('R') };
    if (!side[0]) side[0] = side[1];   // one speaker measured: the same correction on both
    if (!side[1]) side[1] = side[0];
    std::vector<double> corr[2];
    double mx = 0.0;
    for (int c = 0; c < 2; ++c)
    {
        corr[c] = side[c]->correction;
        for (double& v : corr[c]) { v *= in.s.amount; mx = std::max(mx, v); }
    }
    d.preampDb = -mx;
    for (int c = 0; c < 2; ++c)
    {
        d.delayMs[c] = in.s.align ? side[c]->delayMs : 0.0;
        // the phase correction exists only in linear phase (it advances the bass: that needs the latency)
        std::vector<double> gd;
        if (in.s.linear && side[c]->hasPhase())
        {
            gd = side[c]->groupDelayMs;
            for (double& v : gd) v *= in.s.amount;
            d.phase = true;
        }
        d.fir[c] = room::designFir(side[c]->f, corr[c], in.fs, taps, in.s.linear, d.delayMs[c] * 0.001 * in.fs, d.preampDb,
                                   gd.empty() ? nullptr : &gd);
    }
    d.latency = block + (in.s.linear ? taps / 2 : 0);
    return d;
}

class Engine {
public:
    static constexpr int kBlock = 256;

    Engine() { worker = std::thread([this] { workerLoop(); }); }
    ~Engine()
    {
        { std::lock_guard<std::mutex> g(lock); quit = true; }
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Not while process() runs (the host's activate): sizes the convolver for this rate and installs a design at once.
    void prepare(double fs)
    {
        std::lock_guard<std::mutex> c(convLock);
        {
            std::lock_guard<std::mutex> g(lock);
            input.fs = fs > 0.0 ? fs : 48000.0;
        }
        const int taps = room::correctionTaps(input.fs);
        conv.prepare(kBlock, taps + taps / 4 + kBlock);
        installLocked(designNow(), true);
    }

    // From any non-audio thread: a new profile (or none).
    void setProfile(const room::Profile* p)
    {
        {
            std::lock_guard<std::mutex> g(lock);
            input.hasProfile = p != nullptr && p->valid();
            input.profile = input.hasProfile ? *p : room::Profile();
            ++generation;
        }
        wake.notify_all();
    }

    // From the audio thread (a changed parameter): no locks or allocation here, the worker picks it up.
    void setSettings(const Settings& s)
    {
        pendAmount.store(s.amount, std::memory_order_relaxed);
        pendLinear.store(s.linear, std::memory_order_relaxed);
        pendAlign.store(s.align, std::memory_order_relaxed);
        settingsSerial.fetch_add(1, std::memory_order_release);
    }

    void setOutputGain(float db) { outTarget = pump::dbToGainF(db); }

    void process(float* L, float* R, int n)
    {
        conv.process(L, R, n);
        for (int i = 0; i < n; ++i)
        {
            outGain += (outTarget - outGain) * 0.002f;
            L[i] *= outGain;
            R[i] *= outGain;
        }
    }

    int latency() const { return latencyNow.load(std::memory_order_relaxed); }

    // what the window shows (non-real-time threads)
    Design info() const { std::lock_guard<std::mutex> g(infoLock); Design d = shown; d.fir[0].clear(); d.fir[1].clear(); return d; }
    unsigned infoSerial() const { return infoCount.load(); }
    bool busy() const { return designing.load(); }

    // for tests: wait until the worker has nothing left to do
    void settle()
    {
        for (int i = 0; i < 2000; ++i)
        {
            if (!designing.load() && seenSettings.load() == settingsSerial.load() && seenGenerationNow() == generationNow()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

private:
    unsigned generationNow() { std::lock_guard<std::mutex> g(lock); return generation; }
    unsigned seenGenerationNow() { std::lock_guard<std::mutex> g(lock); return seenGeneration; }

    Design designNow()
    {
        DesignInput in;
        {
            std::lock_guard<std::mutex> g(lock);
            input.s.amount = pendAmount.load();
            input.s.linear = pendLinear.load();
            input.s.align = pendAlign.load();
            in = input;
            seenGeneration = generation;
        }
        seenSettings.store(settingsSerial.load(std::memory_order_acquire));
        return designFilters(in, kBlock);
    }

    // with convLock held
    void installLocked(Design d, bool now)
    {
        const float* ir[4] = { d.fir[0].data(), nullptr, nullptr, d.fir[1].data() };
        const int taps = (int)std::max(d.fir[0].size(), d.fir[1].size());
        // pad the shorter one (the delays differ) so both have the same length
        for (auto& f : d.fir) f.resize((size_t)taps, 0.0f);
        ir[0] = d.fir[0].data(); ir[3] = d.fir[1].data();
        auto k = conv.makeKernel(ir, taps);
        if (now) conv.setKernelNow(std::move(k)); else conv.setKernel(std::move(k));
        latencyNow.store(d.latency, std::memory_order_relaxed);
        std::lock_guard<std::mutex> g(infoLock);
        shown = std::move(d);
        ++infoCount;
    }

    void workerLoop()
    {
        for (;;)
        {
            {
                std::unique_lock<std::mutex> g(lock);
                wake.wait_for(g, std::chrono::milliseconds(25), [this] { return quit || generation != seenGeneration; });
                if (quit) return;
            }
            const bool settingsMoved = settingsSerial.load(std::memory_order_acquire) != seenSettings.load();
            if (!settingsMoved && generationNow() == seenGenerationNow()) { std::lock_guard<std::mutex> c(convLock); conv.collect(); continue; }
            designing.store(true);
            // a knob being turned: wait until it rests a moment before designing
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            Design d = designNow();
            {
                std::lock_guard<std::mutex> c(convLock);
                installLocked(std::move(d), false);
            }
            designing.store(false);
        }
    }

    StereoConvolver conv;
    std::mutex convLock;   // the worker's setKernel against prepare()
    std::thread worker;
    mutable std::mutex lock;
    std::condition_variable wake;
    bool quit = false;
    DesignInput input;
    unsigned generation = 0, seenGeneration = 0;
    std::atomic<unsigned> settingsSerial { 0 };
    std::atomic<unsigned> seenSettings { 0 };
    std::atomic<double> pendAmount { 1.0 };
    std::atomic<bool> pendLinear { false }, pendAlign { true };
    std::atomic<int> latencyNow { kBlock };
    std::atomic<bool> designing { false };
    float outGain = 1.0f, outTarget = 1.0f;
    mutable std::mutex infoLock;
    Design shown;
    std::atomic<unsigned> infoCount { 0 };
};

}} // namespace pump::roomfx
