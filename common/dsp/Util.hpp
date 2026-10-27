// pump DSP: small shared helpers. Header-only, no framework dependencies (also builds for wasm).
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

// PUMP_SYNC_KERNELS (default 0): a build for a host with no threads, such as WebAssembly in Thunder Plus's audio
// worklet. The kernels that the plugins design on a worker thread (EQ linear phase, the linear-phase crossovers of
// BAND / WIDE / HEAT) are then designed on the calling thread, at the point where the worker would have been asked,
// and nothing uses std::thread, mutexes or atomic shared_ptr. The plugins never set it, so their behaviour is
// unchanged. Without exceptions (-fno-exceptions), a failed allocation aborts instead of throwing.
#ifndef PUMP_SYNC_KERNELS
 #define PUMP_SYNC_KERNELS 0
#endif

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__)
 #define PUMP_X86 1
 #include <xmmintrin.h>
#else
 #define PUMP_X86 0
#endif

namespace pump {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double dbToGain(double db) { return std::pow(10.0, db * 0.05); }
inline double gainToDb(double g) { return 20.0 * std::log10(std::max(g, 1e-30)); }
inline float dbToGainF(float db) { return std::exp(db * 0.115129254649702f); }
inline float gainToDbF(float g) { return 8.68588963806504f * std::log(std::max(g, 1e-30f)); }

// FFT size of the UI spectrum analyzer (pump::SpectrumView): 8192 at 44.1/48 kHz, doubled per octave of sample rate
// up to 32768, so bins stay ~5.4-5.9 Hz wide and a given mix reads at the same level at every rate.
inline int analyzerSize(double sampleRate)
{
    int n = 8192;
    while (n < 32768 && sampleRate / (n / 8192) > 66000.0) n *= 2;
    return n;
}
// Size of a ScopeRing an analyzer reads: twice its largest FFT, so a window read behind the newest sample
// (ScopeRing::readSmooth) is never partly overwritten by newer audio.
constexpr int kAnalyzerRing = 1 << 16;

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
       #if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        if (raw == nullptr) throw std::bad_alloc();
       #else
        if (raw == nullptr) std::abort();
       #endif
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
    void clear(float v = 0.0f) { std::fill(buf.begin(), buf.end(), v); }
    void setDelay(int samples) { d = clampv(samples, 0, mask - 1); }
    int getDelay() const { return d; }
    inline float process(float x)
    {
        buf[(size_t)w] = x;
        const float y = buf[(size_t)((w - d) & mask)];
        w = (w + 1) & mask;
        return y;
    }
    // After process(): the sample written `delay` samples ago (read(getDelay()) is what process() returned).
    inline float read(int delay) const { return buf[(size_t)((w - 1 - delay) & mask)]; }
private:
    std::vector<float> buf { 0.0f, 0.0f };
    int mask = 1, w = 0, d = 0;
};

// Replaces non-finite samples (a NaN/Inf from an upstream device) and absurd values (beyond +120 dBFS) with 0, in
// place. Call on an engine's input: one bad sample would otherwise stay in recursive filters and envelopes for good.
inline void sanitizeBlock(float* x, int n)
{
    for (int i = 0; i < n; ++i)
        if (!(std::fabs(x[i]) < 1e6f)) x[i] = 0.0f;
}

// Lock-free single-producer/single-consumer sample ring for UI feeds (analyzers, scopes, history).
// The audio thread writes; the UI reads the most recent `count` samples. The samples are relaxed atomics (free on
// x86 and ARM), so a read racing a write is well-defined; the ring is large enough that such a read is never visible.
class ScopeRing {
public:
    explicit ScopeRing(int sizePow2 = 1 << 15) : buf(new std::atomic<float>[(size_t)sizePow2]), mask(sizePow2 - 1)
    {
        for (int i = 0; i < sizePow2; ++i) buf[(size_t)i].store(0.0f, std::memory_order_relaxed);
    }
    inline void push(float x)
    {
        const uint32_t i = w.load(std::memory_order_relaxed);
        buf[i & (uint32_t)mask].store(x, std::memory_order_relaxed);
        w.store(i + 1, std::memory_order_release);
    }
    // Copies the newest `count` samples into dst (oldest first). Returns the write position read.
    uint32_t readLatest(float* dst, int count) const
    {
        const uint32_t end = w.load(std::memory_order_acquire);
        for (int k = 0; k < count; ++k)
            dst[k] = buf[(end - (uint32_t)count + (uint32_t)k) & (uint32_t)mask].load(std::memory_order_relaxed);
        return end;
    }
    uint32_t position() const { return w.load(std::memory_order_acquire); }

    // UI thread only. Hosts deliver audio in blocks (Renoise often 1024-2048 samples), so the newest sample jumps
    // forward once per block and a display that always shows the newest data moves in steps. smoothPosition() is a
    // read end that advances steadily at the measured rate, about one block behind the newest sample (it snaps back
    // after a pause or a jump); readSmooth() copies the `count` samples ending there.
    uint32_t smoothPosition() const
    {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const uint32_t pos = position();
        if (sT < 0.0) { sT = sLastWT = now; sW = 0.0; sLastW = pos; sPos = 0.0; return pos; }
        const double dt = clampv(now - sT, 0.0, 0.1);
        sT = now;
        if (pos != sLastW)
        {
            const double adv = (double)(uint32_t)(pos - sLastW), el = now - sLastWT;
            sW += adv;
            if (el > 1e-4 && el < 0.5) { const double r = adv / el; sRate = sRate <= 0.0 ? r : sRate + (r - sRate) * 0.05; }
            sBlock = std::max(adv, sBlock * 0.995);
            sLastW = pos; sLastWT = now;
        }
        sPos += sRate * dt;
        const double target = sW - 1.25 * sBlock, behind = sW - sPos;
        if (behind < 0.0 || behind > 4.0 * sBlock + 0.05 * sRate || sRate <= 0.0) sPos = target;
        else sPos += (target - sPos) * 0.03;
        return pos - (uint32_t)std::max(0.0, std::min(sW - sPos, (double)(mask / 2)));
    }
    void readSmooth(float* dst, int count) const
    {
        uint32_t end = smoothPosition();
        // the window must lie wholly in what the ring still holds: never further back than capacity - count
        const uint32_t newest = position(), room = (uint32_t)std::max(0, capacity() - count);
        if ((uint32_t)(newest - end) > room) end = newest - room;
        for (int k = 0; k < count; ++k) dst[k] = buf[(end - (uint32_t)count + (uint32_t)k) & (uint32_t)mask].load(std::memory_order_relaxed);
    }
    float at(uint32_t index) const { return buf[index & (uint32_t)mask].load(std::memory_order_relaxed); }
    int capacity() const { return mask + 1; }
private:
    std::unique_ptr<std::atomic<float>[]> buf;
    int mask;
    std::atomic<uint32_t> w { 0 };
    // smoothPosition()'s state (UI thread)
    mutable double sT = -1.0, sLastWT = 0.0, sW = 0.0, sPos = 0.0, sRate = 0.0, sBlock = 0.0;
    mutable uint32_t sLastW = 0;
};

// A float published from the audio thread to the UI (max-hold until the UI reads it).
// An instrument's last stage: inaudible until a peak would pass -1 dBFS, then it pulls the level down at once (no
// overshoot) and lets it back up over ~60 ms. Both channels move together. For transients (a spring's kick, a big
// chord) only; the gain staging keeps ordinary playing well under it.
class SafetyLimiter {
public:
    void prepare(double sr) { rel = 1.0 - std::exp(-1.0 / (0.060 * sr)); g = 1.0; }
    void process(float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const double a = std::max(std::fabs((double)L[i]), R ? std::fabs((double)R[i]) : 0.0);
            const double want = a > kThr ? kThr / a : 1.0;
            g = want < g ? want : g + (want - g) * rel;
            L[i] = (float)(L[i] * g); if (R) R[i] = (float)(R[i] * g);
        }
    }
    double reduction() const { return g; }
private:
    static constexpr double kThr = 0.891;   // -1 dBFS
    double rel = 0.0003, g = 1.0;
};

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
       #if PUMP_X86
        old = _mm_getcsr();
        _mm_setcsr(old | 0x8040u);
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
       #if PUMP_X86
        _mm_setcsr((unsigned)old);
       #elif defined(__aarch64__)
        const uint64_t fpcr = old;
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
       #endif
    }
    DenormalGuard(const DenormalGuard&) = delete;
    DenormalGuard& operator=(const DenormalGuard&) = delete;
private:
   #if PUMP_X86
    unsigned old = 0;
   #else
    uint64_t old = 0;
   #endif
};

// Single-producer single-consumer queue of pointers, fixed capacity, lock-free and allocation-free. The audio
// thread uses it to hand objects it has finished with back to a non-real-time thread, which frees them.
template <typename T, int Cap>
class SpscPtrQueue {
    static_assert((Cap & (Cap - 1)) == 0, "capacity must be a power of two");
public:
    bool push(T* p)   // producer
    {
        const unsigned w = wpos.load(std::memory_order_relaxed);
        if (w - rpos.load(std::memory_order_acquire) >= (unsigned)Cap) return false;
        slots[w & (Cap - 1)] = p;
        wpos.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T*& p)   // consumer
    {
        const unsigned r = rpos.load(std::memory_order_relaxed);
        if (r == wpos.load(std::memory_order_acquire)) return false;
        p = slots[r & (Cap - 1)];
        rpos.store(r + 1, std::memory_order_release);
        return true;
    }
    bool hasSpace() const   // producer
    {
        return wpos.load(std::memory_order_relaxed) - rpos.load(std::memory_order_acquire) < (unsigned)Cap;
    }
private:
    T* slots[Cap] = {};
    std::atomic<unsigned> wpos { 0 }, rpos { 0 };
};

} // namespace pump
