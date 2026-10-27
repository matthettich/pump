// pump FREEZE: the built-in sounds (PRISM's seven: rendered by pump's own instruments), cut into spectra once, in the
// background, the first time a FREEZE opens.
#pragma once

#include "FreezeEngine.hpp"
#include "../prism/PrismParams.hpp"
#include "../prism/PrismSources.hpp"
#include <mutex>
#include <thread>
#include <chrono>

namespace pump { namespace freeze {

// Rendered on a worker thread, started when a FREEZE is activated (start()). Plugins retain() and release() it: when
// the last one goes the thread is stopped and joined (an unfinished set carries on where it left off at the next
// start), so it is never still running while the process exits and its statics go.
class Builtins {
public:
    static constexpr int kCount = kNumSources - 1;
    static Builtins& get() { static Builtins b; return b; }
    void retain() { std::lock_guard<std::mutex> g(m); ++users; }
    void release() { std::lock_guard<std::mutex> g(m); if (--users <= 0) { users = 0; stopLocked(); } }
    void start()
    {
        std::lock_guard<std::mutex> g(m);
        if (running.load() || ready[(size_t)kCount].load()) return;
        if (th.joinable()) th.join();
        quit.store(false); running.store(true);
        th = std::thread([this] {
            // the notes they were played at
            static const int roots[kCount + 1] = { 60, 57, 48, 60, 45, 45, 69, 43 };
            for (int id = 1; id <= kCount && !quit.load(); ++id)
            {
                if (ready[(size_t)id].load(std::memory_order_acquire)) continue;
                const double sr = 48000.0;
                const auto audio = prism::detail::builtinAudio(id, sr);
                set[(size_t)id] = makeSpectra(audio, sr, kSourceNames[id], roots[id]);
                ready[(size_t)id].store(true, std::memory_order_release);
            }
            running.store(false);
        });
    }
    const Spectra* source(int id) const
    {
        if (id < 1 || id > kCount || !ready[(size_t)id].load(std::memory_order_acquire)) return nullptr;
        return set[(size_t)id].get();
    }
    // tests and offline renders: wait until they are all made
    void waitReady() const { for (int id = 1; id <= kCount; ++id) while (!ready[(size_t)id].load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    ~Builtins() { std::lock_guard<std::mutex> g(m); stopLocked(); }

private:
    void stopLocked() { quit.store(true); if (th.joinable()) th.join(); }
    std::mutex m;
    int users = 0;
    std::thread th;
    std::atomic<bool> quit { false }, running { false };
    std::array<std::shared_ptr<Spectra>, kCount + 1> set {};
    std::array<std::atomic<bool>, kCount + 1> ready {};
};

}} // namespace pump::freeze
