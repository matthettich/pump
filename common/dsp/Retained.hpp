// pump DSP: objects loaded off the audio thread (a sound, a wavetable, a voicebank) and handed to it, freed again
// once the audio thread has shown it no longer uses them.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace pump {

// The loading thread publish()es each new object; the audio thread calls acquire() once per block and uses only what
// that returns (it may keep using it for the rest of the block). Every acquire() acknowledges the newest object it has
// seen, so anything published before that is out of the audio thread's hands and is freed at the next publish() or
// collect(). publish(), collect() and peek() belong to one thread (the host's main thread, which also runs the UI:
// a pointer from peek() stays valid until that thread publishes again).
template <typename T>
class Retained {
public:
    // loading thread: make p (may be null) the current object; returns it
    const T* publish(std::shared_ptr<T> p)
    {
        const uint64_t g = ++gen;
        const T* raw = p.get();
        held.push_back({ std::move(p), g });
        cur.store(raw, std::memory_order_release);
        pubGen.store(g, std::memory_order_release);
        collect();
        return raw;
    }
    // loading thread: free what the audio thread has let go of
    void collect()
    {
        const uint64_t seen = seenGen.load(std::memory_order_acquire);
        size_t keep = 0;
        for (size_t i = 0; i < held.size(); ++i)
            if (held[i].gen >= seen || i + 1 == held.size()) held[keep++] = std::move(held[i]);
        held.resize(keep);
    }
    // loading thread (or while the audio thread isn't running): the current object
    const T* peek() const { return cur.load(std::memory_order_acquire); }
    // audio thread, once per block
    const T* acquire()
    {
        const uint64_t g = pubGen.load(std::memory_order_acquire);
        const T* p = cur.load(std::memory_order_acquire);   // published no earlier than generation g
        seenGen.store(g, std::memory_order_release);
        return p;
    }
    size_t heldCount() const { return held.size(); }   // for the tests

private:
    struct Entry { std::shared_ptr<T> obj; uint64_t gen; };
    std::vector<Entry> held;
    uint64_t gen = 0;
    std::atomic<const T*> cur { nullptr };
    std::atomic<uint64_t> pubGen { 0 }, seenGen { 0 };
};

} // namespace pump
