// pump PRISM: the built-in sources. Each is a few seconds of one of pump's own instruments (or a simple generated
// sound), rendered once and analysed, on a background thread when the first PRISM opens (about a second's work).
#pragma once

#include "PrismAnalysis.hpp"
#include "../sing/SingEngine.hpp"
#include "../coil/CoilEngine.hpp"
#include "../phase/PhaseEngine.hpp"
#include "../wob/WobEngine.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

namespace pump { namespace prism {

constexpr int kBuiltins = kNumSources - 1;   // sources 1..7

namespace detail {
template <class E, class F>
inline std::vector<float> renderWith(E& e, double sr, double secs, std::vector<synth::Midi> ev, F&& proc)
{
    const int n = (int)(sr * secs);
    std::vector<float> L((size_t)n + 256), R((size_t)n + 256);
    size_t q = 0;
    for (int off = 0; off < n; off += 256)
    {
        std::vector<synth::Midi> here;
        while (q < ev.size() && (int)ev[q].offset < off + 256) { auto x = ev[q++]; x.offset -= (uint32_t)off; here.push_back(x); }
        proc(e, L.data() + off, R.data() + off, 256, here);
    }
    L.resize((size_t)n);
    for (int i = 0; i < n; ++i) L[(size_t)i] = 0.5f * (L[(size_t)i] + R[(size_t)i]);
    float pk = 1e-6f; for (float v : L) pk = std::max(pk, std::fabs(v));
    for (float& v : L) v *= 0.7f / pk;
    return L;
}
inline std::vector<float> builtinAudio(int id, double sr)
{
    using V = std::vector<synth::Midi>;
    switch (id)
    {
    case 1:
    {
        sing::Engine e; sing::Settings s; s.vib = 0.35; e.setSettings(s); e.prepare(sr, 256); e.setLyrics("aa ~ ee ~ ii ~ oo ~ uu");
        V ev; for (int k = 0; k < 9; ++k) { ev.push_back({ (uint32_t)(k * 0.5 * sr), 0x90, 57, 100 }); ev.push_back({ (uint32_t)((k * 0.5 + 0.49) * sr), 0x80, 57, 0 }); }
        std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
        return renderWith(e, sr, 4.8, ev, [](sing::Engine& x, float* l, float* r, int n, V& h) { x.process(l, r, n, h.data(), (int)h.size()); });
    }
    case 2: case 7:
    {
        coil::Engine e; coil::Settings s;
        if (id == 2) { s.model = 1; s.harmonic = 0.7; s.decay = 4.0; s.excite = 0.6; s.spread = 0.05; s.release = 1.5; }
        else { s.model = 3; s.harmonic = 0.2; s.decay = 6.0; s.excite = 0.9; s.spread = 0.45; s.tone = 0.3; s.release = 3.0; }
        e.prepare(sr, 256); e.setSettings(s);
        V ev { { 0, 0x90, (uint8_t)(id == 2 ? 48 : 43), 110 }, { (uint32_t)(3.5 * sr), 0x80, (uint8_t)(id == 2 ? 48 : 43), 0 } };
        return renderWith(e, sr, 4.5, ev, [](coil::Engine& x, float* l, float* r, int n, V& h) { x.process(nullptr, nullptr, l, r, n, h.data(), (int)h.size()); });
    }
    case 3:
    {
        phase::Engine e; phase::Settings s; s.engine = phase::kSix; s.algo = 14; s.chip = false; s.feedback = 0;
        const double ratio[6] = { 1, 3, 5, 7, 9, 3 }, fine[6] = { 0, 50, 41, 0, 0, 50 }, level[6] = { 90, 82, 76, 70, 64, 80 }, dec[6] = { 4.0, 3.0, 2.0, 1.6, 1.2, 2.5 };
        for (int k = 0; k < 6; ++k) { auto& o = s.op[k]; o.ratio = (int)ratio[k]; o.fine = fine[k]; o.level = level[k]; o.a = 0.001; o.d = dec[k]; o.s = 0.0; o.r = 2.0; o.vel = 0.0; }
        e.setSettings(s); e.prepare(sr, 256);
        V ev { { 0, 0x90, 60, 110 }, { (uint32_t)(3.5 * sr), 0x80, 60, 0 } };
        return renderWith(e, sr, 4.5, ev, [](phase::Engine& x, float* l, float* r, int n, V& h) { x.process(l, r, n, h.data(), (int)h.size()); });
    }
    case 4:
    {
        wob::Engine e; wob::Settings s; s.partials = 160; s.wobSync = false; s.wobRate = 2.0; s.wobMorph = 1.0; s.shapeB = 1.0; s.tiltB = 0.6;
        s.wobCut = 2.0; s.cutoff = 500.0; s.res = 0.4; s.sub = 0.0; s.octave = 0; s.volumeDb = 0.0;
        e.setSettings(s); e.prepare(sr, 256);
        V ev { { 0, 0x90, 45, 110 }, { (uint32_t)(3.9 * sr), 0x80, 45, 0 } };
        return renderWith(e, sr, 4.0, ev, [](wob::Engine& x, float* l, float* r, int n, V& h) { x.process(l, r, n, h.data(), (int)h.size()); });
    }
    case 5:
    {
        // a saw through a resonant low-pass sweeping up and back
        const int n = (int)(sr * 4.0);
        std::vector<float> y((size_t)n);
        double ph = 0.0, lp = 0.0, bp = 0.0;
        const double f = 110.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, sw = 0.5 - 0.5 * std::cos(kTwoPi * t / 4.0);
            const double fc = 200.0 * std::pow(40.0, sw), g = 2.0 * std::sin(kPi * std::min(fc, sr * 0.2) / sr), q = 0.25;
            ph += f / sr; if (ph >= 1.0) ph -= 1.0;
            const double saw = synth::blepSaw(ph, f / sr);
            lp += g * bp; const double hp = saw - lp - q * bp; bp += g * hp;
            y[(size_t)i] = (float)(lp * std::min(1.0, t * 50.0) * std::min(1.0, (4.0 - t) * 20.0));
        }
        float pk = 1e-6f; for (float v : y) pk = std::max(pk, std::fabs(v)); for (float& v : y) v *= 0.7f / pk;
        return y;
    }
    default:
    {
        // a breathy flute: a few soft harmonics with vibrato, breath noise
        const int n = (int)(sr * 4.0);
        std::vector<float> y((size_t)n);
        fx::Rng rng(77u);
        double ph = 0.0, nz = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, f = 440.0 * (1.0 + 0.006 * std::sin(kTwoPi * 5.2 * t) * std::min(1.0, t));
            ph += f / sr; if (ph >= 1.0) ph -= 1.0;
            nz += (rng.bi() - nz) * 0.25;
            const double env = std::min(1.0, t * 6.0) * std::min(1.0, (4.0 - t) * 4.0);
            const double tone = std::sin(kTwoPi * ph) + 0.25 * std::sin(2 * kTwoPi * ph) + 0.08 * std::sin(3 * kTwoPi * ph);
            y[(size_t)i] = (float)((tone * (0.85 + 0.15 * nz) + 0.35 * nz) * env);
        }
        float pk = 1e-6f; for (float v : y) pk = std::max(pk, std::fabs(v)); for (float& v : y) v *= 0.7f / pk;
        return y;
    }
    }
}
} // namespace detail

// the built-ins, shared by every PRISM in the process, built once in the background
class Builtins {
public:
    static Builtins& get() { static Builtins b; return b; }
    void start()
    {
        std::call_once(once, [this] { th = std::thread([this] {
            for (int id = 1; id <= kBuiltins && !quit.load(); ++id)
            {
                const double sr = 48000.0;
                auto audio = detail::builtinAudio(id, sr);
                set[(size_t)id] = analyse(audio, sr, kSourceNames[id]);
                ready[(size_t)id].store(true, std::memory_order_release);
            }
        }); });
    }
    const Analysis* source(int id) const
    {
        if (id < 1 || id > kBuiltins || !ready[(size_t)id].load(std::memory_order_acquire)) return nullptr;
        return set[(size_t)id].get();
    }
    ~Builtins() { quit.store(true); if (th.joinable()) th.join(); }

private:
    Builtins() = default;
    std::once_flag once;
    std::thread th;
    std::atomic<bool> quit { false };
    std::array<std::shared_ptr<Analysis>, kNumSources> set {};
    std::array<std::atomic<bool>, kNumSources> ready {};
};

}} // namespace pump::prism
