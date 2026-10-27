// pump in WebAssembly: instance handles for the C interface (pump_wasm.cpp, pump_synth.cpp).
//
// A handle is an opaque int >= 0: slot | generation << kSlotBits. When an instance is freed its slot can be reused,
// but with the next generation, so a handle JavaScript kept after freeing is refused (every call on it does nothing,
// or returns 0 / null) instead of reaching whichever instance took the slot next.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace pumpwasm_handles {

template <typename T>
class HandleTable {
public:
    static constexpr int kSlotBits = 10;
    static constexpr int kMaxSlots = 1 << kSlotBits;            // instances at once
    static constexpr uint32_t kMaxGen = (1u << (31 - kSlotBits)) - 1;   // handles stay positive ints

    // takes ownership; -1 when every slot is in use
    int add(T* p)
    {
        std::unique_ptr<T> own(p);
        size_t k = 0;
        while (k < objs.size() && objs[k]) ++k;
        if (k == objs.size())
        {
            if ((int)k >= kMaxSlots) return -1;
            objs.emplace_back();
            gens.push_back(0u);
        }
        gens[k] = gens[k] >= kMaxGen ? 1u : gens[k] + 1u;
        objs[k] = std::move(own);
        return (int)(k | (size_t)gens[k] << kSlotBits);
    }
    T* at(int h) const
    {
        if (h < 0) return nullptr;
        const size_t slot = (size_t)h & (size_t)(kMaxSlots - 1);
        const uint32_t gen = (uint32_t)h >> kSlotBits;
        return slot < objs.size() && gens[slot] == gen ? objs[slot].get() : nullptr;
    }
    void remove(int h)
    {
        if (at(h)) objs[(size_t)h & (size_t)(kMaxSlots - 1)].reset();
    }

private:
    std::vector<std::unique_ptr<T>> objs;
    std::vector<uint32_t> gens;
};

} // namespace pumpwasm_handles
