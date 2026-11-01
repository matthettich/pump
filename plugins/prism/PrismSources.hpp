// pump PRISM: the built-in sources. Each is a few seconds of one of pump's own instruments (or a simple generated
// sound), rendered once and analysed, on a background thread when the first PRISM opens (about a second's work).
#pragma once

#include "PrismAudio.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace pump { namespace prism {

// the built-ins, shared by every PRISM in the process, built once in the background
// The built-in sources are rendered on a worker thread, started when a PRISM is activated (start()). Plugins retain()
// and release() it: when the last one goes the thread is stopped and joined (an unfinished set carries on where it
// left off at the next start), so it is never still running while the process exits and its statics go.
class Builtins {
public:
    static Builtins& get() { static Builtins b; return b; }
    void retain() { std::lock_guard<std::mutex> g(m); ++users; }
    void release() { std::lock_guard<std::mutex> g(m); if (--users <= 0) { users = 0; stopLocked(); } }
    void start()
    {
        std::lock_guard<std::mutex> g(m);
        if (running.load() || ready[(size_t)kBuiltins].load()) return;
        if (th.joinable()) th.join();
        quit.store(false); running.store(true);
        th = std::thread([this] {
            for (int id = 1; id <= kBuiltins && !quit.load(); ++id)
            {
                if (ready[(size_t)id].load(std::memory_order_acquire)) continue;
                const double sr = 48000.0;
                auto audio = detail::builtinAudio(id, sr);
                set[(size_t)id] = analyse(audio, sr, kSourceNames[id]);
                ready[(size_t)id].store(true, std::memory_order_release);
            }
            running.store(false);
        });
    }
    const Analysis* source(int id) const
    {
        if (id < 1 || id > kBuiltins || !ready[(size_t)id].load(std::memory_order_acquire)) return nullptr;
        return set[(size_t)id].get();
    }
    ~Builtins() { std::lock_guard<std::mutex> g(m); stopLocked(); }

private:
    Builtins() = default;
    void stopLocked() { quit.store(true); if (th.joinable()) th.join(); }
    std::mutex m;
    int users = 0;
    std::thread th;
    std::atomic<bool> quit { false }, running { false };
    std::array<std::shared_ptr<Analysis>, kNumSources> set {};
    std::array<std::atomic<bool>, kNumSources> ready {};
};

}} // namespace pump::prism
