// pump DSP: small shared helpers. Header-only, no framework dependencies (also builds for wasm).
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace pump {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double dbToGain(double db) { return std::pow(10.0, db * 0.05); }
inline double gainToDb(double g) { return 20.0 * std::log10(std::max(g, 1e-30)); }
inline float dbToGainF(float db) { return std::exp(db * 0.115129254649702f); }
inline float gainToDbF(float g) { return 8.68588963806504f * std::log(std::max(g, 1e-30f)); }

template <typename T> inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// One-pole coefficient for a time constant in seconds (reaches 1 - 1/e after `seconds`).
inline double tauCoef(double seconds, double sampleRate)
{
    if (seconds <= 0.0) return 0.0;
    return std::exp(-1.0 / (seconds * sampleRate));
}

// Linear-in-time parameter smoother, glides to the target over `rampSamples`.
class LinearSmoother {
public:
    void reset(double v) { cur = target = v; steps = 0; }
    void setRamp(int samples) { ramp = std::max(1, samples); }
    void setTarget(double v)
    {
        if (v == target) return;
        target = v;
        steps = ramp;
        inc = (target - cur) / steps;
    }
    inline double next()
    {
        if (steps > 0) { cur += inc; if (--steps == 0) cur = target; }
        return cur;
    }
    bool isSmoothing() const { return steps > 0; }
    double current() const { return cur; }
    double getTarget() const { return target; }
private:
    double cur = 0.0, target = 0.0, inc = 0.0;
    int steps = 0, ramp = 64;
};

// Aligned float buffer (16/32 byte alignment for SIMD FFT).
class AlignedBuffer {
public:
    AlignedBuffer() = default;
    explicit AlignedBuffer(size_t n) { resize(n); }
    ~AlignedBuffer() { release(); }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    AlignedBuffer(AlignedBuffer&& o) noexcept : p(o.p), n(o.n) { o.p = nullptr; o.n = 0; }
    AlignedBuffer& operator=(AlignedBuffer&& o) noexcept { if (this != &o) { release(); p = o.p; n = o.n; o.p = nullptr; o.n = 0; } return *this; }

    void resize(size_t count)
    {
        if (count == n) { clear(); return; }
        release();
        if (count == 0) return;
        void* raw = nullptr;
       #if defined(_WIN32)
        raw = _aligned_malloc(count * sizeof(float), 64);
       #else
        if (posix_memalign(&raw, 64, count * sizeof(float)) != 0) raw = nullptr;
       #endif
        if (raw == nullptr) throw std::bad_alloc();
        p = static_cast<float*>(raw);
        n = count;
        clear();
    }
    void clear() { if (p) std::memset(p, 0, n * sizeof(float)); }
    float* data() { return p; }
    const float* data() const { return p; }
    size_t size() const { return n; }
    float& operator[](size_t i) { return p[i]; }
    const float& operator[](size_t i) const { return p[i]; }
private:
    void release()
    {
        if (p)
        {
           #if defined(_WIN32)
            _aligned_free(p);
           #else
            std::free(p);
           #endif
        }
        p = nullptr; n = 0;
    }
    float* p = nullptr;
    size_t n = 0;
};

// Simple integer-sample delay line (power of two ring).
class DelayLine {
public:
    void setMaxDelay(int samples)
    {
        int size = 1;
        while (size < samples + 2) size <<= 1;
        buf.assign((size_t)size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    void setDelay(int samples) { d = clampv(samples, 0, mask - 1); }
    int getDelay() const { return d; }
    inline float process(float x)
    {
        buf[(size_t)w] = x;
        const float y = buf[(size_t)((w - d) & mask)];
        w = (w + 1) & mask;
        return y;
    }
private:
    std::vector<float> buf { 0.0f, 0.0f };
    int mask = 1, w = 0, d = 0;
};

// Lock-free single-producer/single-consumer sample ring for UI feeds (analyzers, scopes, history).
// The audio thread writes; the UI reads the most recent `count` samples. Reads are not synchronised
// against concurrent writes beyond the write index (the ring is large enough that tearing is invisible).
class ScopeRing {
public:
    explicit ScopeRing(int sizePow2 = 1 << 15) { buf.assign((size_t)sizePow2, 0.0f); mask = sizePow2 - 1; }
    inline void push(float x)
    {
        const uint32_t i = w.load(std::memory_order_relaxed);
        buf[i & (uint32_t)mask] = x;
        w.store(i + 1, std::memory_order_release);
    }
    // Copies the newest `count` samples into dst (oldest first). Returns the write position read.
    uint32_t readLatest(float* dst, int count) const
    {
        const uint32_t end = w.load(std::memory_order_acquire);
        for (int k = 0; k < count; ++k)
            dst[k] = buf[(end - (uint32_t)count + (uint32_t)k) & (uint32_t)mask];
        return end;
    }
    uint32_t position() const { return w.load(std::memory_order_acquire); }
    float at(uint32_t index) const { return buf[index & (uint32_t)mask]; }
    int capacity() const { return mask + 1; }
private:
    std::vector<float> buf;
    int mask;
    std::atomic<uint32_t> w { 0 };
};

// A float published from the audio thread to the UI (max-hold until the UI reads it).
class MeterValue {
public:
    void setMax(float v)
    {
        float cur = val.load(std::memory_order_relaxed);
        while (v > cur && !val.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    void setMin(float v)
    {
        float cur = val.load(std::memory_order_relaxed);
        while (v < cur && !val.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    void set(float v) { val.store(v, std::memory_order_relaxed); }
    float get() const { return val.load(std::memory_order_relaxed); }
    float exchange(float v) { return val.exchange(v, std::memory_order_relaxed); }
private:
    std::atomic<float> val { 0.0f };
};

// Denormal protection for the audio thread (flush-to-zero / denormals-are-zero).
class DenormalGuard {
public:
    DenormalGuard()
    {
       #if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__)
        old = __builtin_ia32_stmxcsr_compat();
        setcsr(old | 0x8040);
       #elif defined(__aarch64__)
        uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        old = fpcr;
        fpcr |= (1ULL << 24);
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
       #endif
    }
    ~DenormalGuard()
    {
       #if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__)
        setcsr((unsigned)old);
       #elif defined(__aarch64__)
        uint64_t fpcr = old;
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
       #endif
    }
private:
   #if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__)
    static unsigned __builtin_ia32_stmxcsr_compat()
    {
        unsigned v;
        __asm__ __volatile__("stmxcsr %0" : "=m"(v));
        return v;
    }
    static void setcsr(unsigned v) { __asm__ __volatile__("ldmxcsr %0" : : "m"(v)); }
   #endif
    uint64_t old = 0;
};

} // namespace pump
