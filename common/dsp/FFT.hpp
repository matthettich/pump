// pump DSP: real FFT (pffft), partitioned convolution and an STFT engine.
#pragma once

#include "Util.hpp"
#include "../third_party/pffft/pffft.h"
#include <atomic>
#include <memory>

namespace pump {

// Real FFT of size n (a multiple of 32). Ordered layout: [re0, reN/2, re1, im1, re2, im2, ...].
class RealFFT {
public:
    RealFFT() = default;
    explicit RealFFT(int n) { setup(n); }
    ~RealFFT() { if (s) pffft_destroy_setup(s); }
    RealFFT(const RealFFT&) = delete;
    RealFFT& operator=(const RealFFT&) = delete;

    void setup(int n)
    {
        if (n == size && s) return;
        if (s) pffft_destroy_setup(s);
        s = pffft_new_setup(n, PFFFT_REAL);
        size = n;
        work.resize((size_t)n);
    }
    int getSize() const { return size; }
    // Ordered transforms (for analysis and spectral processing)
    void forward(const float* in, float* out) { pffft_transform_ordered(s, in, out, work.data(), PFFFT_FORWARD); }
    void inverse(const float* in, float* out) { pffft_transform_ordered(s, in, out, work.data(), PFFFT_BACKWARD); }   // unscaled (x n)
    // Unordered transforms (for convolution)
    void forwardU(const float* in, float* out) { pffft_transform(s, in, out, work.data(), PFFFT_FORWARD); }
    void inverseU(const float* in, float* out) { pffft_transform(s, in, out, work.data(), PFFFT_BACKWARD); }
    void convolveAccumulate(const float* a, const float* b, float* ab, float scale) { pffft_zconvolve_accumulate(s, a, b, ab, scale); }
private:
    PFFFT_Setup* s = nullptr;
    int size = 0;
    AlignedBuffer work;
};

// Uniformly partitioned overlap-save convolution of a stereo signal with a 2x2 kernel matrix
// (out_L = K_LL * in_L + K_LR * in_R, out_R = K_RL * in_L + K_RR * in_R). Cross kernels are skipped when
// absent. Kernels can be replaced from another thread: the new set is crossfaded in over one block.
// Latency: one block (B samples), on top of whatever delay the kernels themselves have.
class StereoConvolver {
public:
    struct Kernel {
        int partitions = 0;
        bool cross = false;                 // has K_LR / K_RL
        std::vector<AlignedBuffer> spec[4]; // [LL, LR, RL, RR][partition], unordered spectra of size 2B
    };

    void prepare(int blockSize, int maxTaps)
    {
        B = blockSize;
        N = 2 * B;
        fft.setup(N);
        maxParts = (maxTaps + B - 1) / B;
        for (int ch = 0; ch < 2; ++ch)
        {
            fdl[ch].clear();
            for (int p = 0; p < maxParts; ++p) fdl[ch].emplace_back((size_t)N);
            inBuf[ch].resize((size_t)N);
            outBuf[ch].resize((size_t)B);
            prevOut[ch].resize((size_t)B);
        }
        acc.resize((size_t)N); acc2.resize((size_t)N); tmp.resize((size_t)N); spec.resize((size_t)N);
        fdlPos = 0;
        fill = 0;
        cur.reset();
        pending.reset();
        pendingReady.store(false);
    }

    int blockSize() const { return B; }
    int latency() const { return B; }

    // Build a kernel set from time-domain impulse responses (length <= maxTaps). Call from any thread
    // except the audio thread (allocates). ir[1]/ir[2] may be null (no cross terms).
    std::shared_ptr<Kernel> makeKernel(const float* const ir[4], int taps)
    {
        auto k = std::make_shared<Kernel>();
        k->partitions = std::min((taps + B - 1) / B, maxParts);
        k->cross = ir[1] != nullptr || ir[2] != nullptr;
        RealFFT f(N);
        AlignedBuffer t((size_t)N);
        for (int m = 0; m < 4; ++m)
        {
            if (ir[m] == nullptr) continue;
            for (int p = 0; p < k->partitions; ++p)
            {
                t.clear();
                const int start = p * B, n = std::min(B, taps - start);
                for (int i = 0; i < n; ++i) t[(size_t)i] = ir[m][start + i];
                AlignedBuffer s((size_t)N);
                f.forwardU(t.data(), s.data());
                k->spec[m].push_back(std::move(s));
            }
        }
        return k;
    }

    // Hand a new kernel to the audio thread (from a worker thread).
    void setKernel(std::shared_ptr<Kernel> k)
    {
        std::atomic_store(&incoming, std::move(k));
        pendingReady.store(true, std::memory_order_release);
    }
    // Immediately install (not from the audio thread while it runs).
    void setKernelNow(std::shared_ptr<Kernel> k) { cur = std::move(k); pending.reset(); pendingReady.store(false); }

    bool hasKernel() const { return cur != nullptr || pendingReady.load(); }

    // Process in place. Any block size; internally works in blocks of B.
    void process(float* L, float* R, int n)
    {
        int i = 0;
        while (i < n)
        {
            const int take = std::min(n - i, B - fill);
            for (int k = 0; k < take; ++k)
            {
                inBuf[0][(size_t)(B + fill + k)] = L[i + k];
                inBuf[1][(size_t)(B + fill + k)] = R[i + k];
                L[i + k] = outBuf[0][(size_t)(fill + k)];
                R[i + k] = outBuf[1][(size_t)(fill + k)];
            }
            fill += take;
            i += take;
            if (fill == B)
            {
                runBlock();
                fill = 0;
            }
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& b : fdl[ch]) b.clear();
            inBuf[ch].clear(); outBuf[ch].clear();
        }
        fill = 0;
    }

private:
    void convolveWith(const Kernel& k, int outCh, AlignedBuffer& dst)
    {
        dst.clear();
        const float scale = 1.0f / (float)N;
        const int mSelf = outCh == 0 ? 0 : 3, mCross = outCh == 0 ? 1 : 2;
        const int inSelf = outCh, inCross = 1 - outCh;
        for (int p = 0; p < k.partitions; ++p)
        {
            const int slot = (fdlPos - p + maxParts) % maxParts;
            if (!k.spec[mSelf].empty())
                fft.convolveAccumulate(fdl[inSelf][(size_t)slot].data(), k.spec[mSelf][(size_t)p].data(), dst.data(), scale);
            if (k.cross && !k.spec[mCross].empty())
                fft.convolveAccumulate(fdl[inCross][(size_t)slot].data(), k.spec[mCross][(size_t)p].data(), dst.data(), scale);
        }
    }

    void runBlock()
    {
        // take a pending kernel (crossfade from the current one over this block)
        std::shared_ptr<Kernel> fadeFrom;
        if (pendingReady.exchange(false, std::memory_order_acquire))
        {
            std::shared_ptr<Kernel> k = std::atomic_load(&incoming);
            if (k) { fadeFrom = cur; cur = k; }
        }
        // input spectra into the frequency-domain delay line
        fdlPos = (fdlPos + 1) % maxParts;
        for (int ch = 0; ch < 2; ++ch)
            fft.forwardU(inBuf[ch].data(), fdl[ch][(size_t)fdlPos].data());
        for (int ch = 0; ch < 2; ++ch)
        {
            if (!cur)
            {
                // no kernel yet: pass through (delayed by one block, like the convolution)
                for (int k = 0; k < B; ++k) outBuf[ch][(size_t)k] = inBuf[ch][(size_t)(B + k)];
                continue;
            }
            convolveWith(*cur, ch, acc);
            fft.inverseU(acc.data(), tmp.data());
            if (fadeFrom)
            {
                convolveWith(*fadeFrom, ch, acc2);
                fft.inverseU(acc2.data(), spec.data());
                for (int k = 0; k < B; ++k)
                {
                    const float t = (k + 0.5f) / (float)B;
                    outBuf[ch][(size_t)k] = spec[(size_t)(B + k)] * (1.0f - t) + tmp[(size_t)(B + k)] * t;
                }
            }
            else
            {
                for (int k = 0; k < B; ++k) outBuf[ch][(size_t)k] = tmp[(size_t)(B + k)];
            }
        }
        // slide the input window
        for (int ch = 0; ch < 2; ++ch)
            std::memcpy(inBuf[ch].data(), inBuf[ch].data() + B, sizeof(float) * (size_t)B);
        // the old kernel is released here (shared_ptr): only deallocation, rare and small
    }

    RealFFT fft;
    int B = 512, N = 1024, maxParts = 1, fdlPos = 0, fill = 0;
    std::vector<AlignedBuffer> fdl[2];
    AlignedBuffer inBuf[2], outBuf[2], prevOut[2], acc, acc2, tmp, spec;
    std::shared_ptr<Kernel> cur, pending, incoming;
    std::atomic<bool> pendingReady { false };
};

// Short-time Fourier transform engine: sqrt-Hann analysis and synthesis windows, 75% overlap.
// The callback receives each frame's ordered spectra (one per channel) and may change them in place.
// Latency: exactly N samples.
class STFT {
public:
    using Callback = void (*)(void* user, float* const* spectra, int numChannels, int fftSize);

    void prepare(int fftSize, int channels)
    {
        N = fftSize;
        H = N / 4;
        C = std::min(channels, 2);
        fft.setup(N);
        win.resize((size_t)N);
        for (int i = 0; i < N; ++i)
            win[(size_t)i] = (float)std::sqrt(0.5 - 0.5 * std::cos(kTwoPi * i / N));
        for (int c = 0; c < C; ++c)
        {
            ring[c].resize((size_t)N);
            acc[c].resize((size_t)N);
            outQ[c].resize((size_t)H);
            frame[c].resize((size_t)N);
            specBuf[c].resize((size_t)N);
        }
        reset();
    }
    void reset()
    {
        for (int c = 0; c < C; ++c) { ring[c].clear(); acc[c].clear(); outQ[c].clear(); }
        wpos = 0; cnt = 0;
    }
    int latency() const { return N; }
    int fftSize() const { return N; }
    int hop() const { return H; }

    void process(float* const* io, int n, Callback cb, void* user)
    {
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < C; ++c)
            {
                ring[c][(size_t)wpos] = io[c][i];
                io[c][i] = outQ[c][(size_t)cnt];
            }
            wpos = (wpos + 1) % N;
            if (++cnt == H)
            {
                cnt = 0;
                runFrame(cb, user);
            }
        }
    }

private:
    void runFrame(Callback cb, void* user)
    {
        float* spectra[2] = { specBuf[0].data(), C > 1 ? specBuf[1].data() : nullptr };
        for (int c = 0; c < C; ++c)
        {
            for (int i = 0; i < N; ++i)
                frame[c][(size_t)i] = ring[c][(size_t)((wpos + i) % N)] * win[(size_t)i];
            fft.forward(frame[c].data(), specBuf[c].data());
        }
        cb(user, spectra, C, N);
        // sqrt-Hann squared = Hann, which sums to 2 at 75% overlap; the inverse FFT is unscaled (x N)
        const float scale = 0.5f / (float)N;
        for (int c = 0; c < C; ++c)
        {
            fft.inverse(specBuf[c].data(), frame[c].data());
            for (int i = 0; i < N; ++i)
                acc[c][(size_t)i] += frame[c][(size_t)i] * win[(size_t)i] * scale;
            std::memcpy(outQ[c].data(), acc[c].data(), sizeof(float) * (size_t)H);
            std::memmove(acc[c].data(), acc[c].data() + H, sizeof(float) * (size_t)(N - H));
            std::memset(acc[c].data() + (N - H), 0, sizeof(float) * (size_t)H);
        }
    }

    RealFFT fft;
    int N = 2048, H = 512, C = 2, wpos = 0, cnt = 0;
    AlignedBuffer win, ring[2], acc[2], outQ[2], frame[2], specBuf[2];
};

} // namespace pump
