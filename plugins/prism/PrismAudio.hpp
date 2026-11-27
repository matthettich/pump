// pump PRISM: the built-in sources' audio (renderWith, builtinAudio): a few seconds of one of pump's own instruments
// (or a simple generated sound). No threads here, so a build without them (Thunder Plus's wasm: pump FREEZE) can make them;
// PrismSources.hpp makes and analyses them on a background thread for the plugins.
#pragma once

#include "PrismParams.hpp"
#include "PrismAnalysis.hpp"
#include "../sing/SingEngine.hpp"
#include "../coil/CoilEngine.hpp"
#include "../phase/PhaseEngine.hpp"
#include "../wob/WobEngine.hpp"
#include <algorithm>
#include <memory>
#include <vector>

namespace pump { namespace prism {

constexpr int kBuiltins = kNumSources - 1;   // sources 1..7

namespace detail {
// A built-in source's audio, made a step at a time (BuiltinJob::step) or all at once (builtinAudio, which is the same
// steps run to the end, so both give the same samples). Thunder Plus's wasm has no threads: it makes FREEZE's sources a
// few blocks per audio callback instead of in one long call.
class BuiltinJob {
public:
    virtual ~BuiltinJob() = default;
    // up to `units` steps (a 256-sample engine block, or a 4096-sample stretch of a generated sound); true when done
    virtual bool step(int units) = 0;
    virtual int unitsLeft() const = 0;
    std::vector<float> take() { return std::move(out); }
protected:
    std::vector<float> out;
};

template <class E, class F>
class RenderJob final : public BuiltinJob {
public:
    RenderJob(std::unique_ptr<E> eng, double sr, double secs, std::vector<synth::Midi> events, F p)
        : ep(std::move(eng)), proc(std::move(p)), ev(std::move(events)), n((int)(sr * secs)), L((size_t)n + 256), R((size_t)n + 256) {}
    bool step(int units) override
    {
        for (; units > 0 && off < n; --units, off += 256)
        {
            std::vector<synth::Midi> here;
            while (q < ev.size() && (int)ev[q].offset < off + 256) { auto x = ev[q++]; x.offset -= (uint32_t)off; here.push_back(x); }
            proc(*ep, L.data() + off, R.data() + off, 256, here);
        }
        if (off < n) return false;
        if (ep) finish();
        return true;
    }
    int unitsLeft() const override { return off < n ? (n - off + 255) / 256 + 1 : 0; }
private:
    void finish()
    {
        ep.reset();
        L.resize((size_t)n);
        for (int i = 0; i < n; ++i) L[(size_t)i] = 0.5f * (L[(size_t)i] + R[(size_t)i]);
        float pk = 1e-6f; for (float v : L) pk = std::max(pk, std::fabs(v));
        for (float& v : L) v *= 0.7f / pk;
        out = std::move(L); std::vector<float>().swap(R);
    }
    std::unique_ptr<E> ep;
    F proc;
    std::vector<synth::Midi> ev;
    const int n;
    std::vector<float> L, R;
    size_t q = 0;
    int off = 0;
};
template <class E, class F>
inline std::unique_ptr<BuiltinJob> renderJob(std::unique_ptr<E> e, double sr, double secs, std::vector<synth::Midi> ev, F proc)
{
    return std::make_unique<RenderJob<E, F>>(std::move(e), sr, secs, std::move(ev), std::move(proc));
}

// the generated sounds (5: the swept saw, 6: the flute), sample by sample; `gen(i, y)` makes sample i
template <class G>
class GenJob final : public BuiltinJob {
public:
    GenJob(int count, G g) : gen(std::move(g)), n(count), y((size_t)count) {}
    bool step(int units) override
    {
        for (; units > 0 && i < n; --units) { const int e = std::min(n, i + 4096); for (; i < e; ++i) y[(size_t)i] = gen(i); }
        if (i < n) return false;
        if (!doneFlag) { float pk = 1e-6f; for (float v : y) pk = std::max(pk, std::fabs(v)); for (float& v : y) v *= 0.7f / pk; out = std::move(y); doneFlag = true; }
        return true;
    }
    int unitsLeft() const override { return i < n ? (n - i + 4095) / 4096 + 1 : 0; }
private:
    G gen;
    const int n;
    std::vector<float> y;
    int i = 0;
    bool doneFlag = false;
};
template <class G>
inline std::unique_ptr<BuiltinJob> genJob(int n, G g) { return std::make_unique<GenJob<G>>(n, std::move(g)); }

inline std::unique_ptr<BuiltinJob> builtinJob(int id, double sr)
{
    using V = std::vector<synth::Midi>;
    switch (id)
    {
    case 1:
    {
        auto ep = std::make_unique<sing::Engine>(); sing::Engine& e = *ep; sing::Settings s; s.vib = 0.35; e.setSettings(s); e.prepare(sr, 256); e.setLyrics("aa ~ ee ~ ii ~ oo ~ uu");
        V ev; for (int k = 0; k < 9; ++k) { ev.push_back({ (uint32_t)(k * 0.5 * sr), 0x90, 57, 100 }); ev.push_back({ (uint32_t)((k * 0.5 + 0.49) * sr), 0x80, 57, 0 }); }
        std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
        return renderJob(std::move(ep), sr, 4.8, ev, [](sing::Engine& x, float* l, float* r, int n, V& h) { x.process(l, r, n, h.data(), (int)h.size()); });
    }
    case 2: case 7:
    {
        auto ep = std::make_unique<coil::Engine>(); coil::Engine& e = *ep; coil::Settings s;
        if (id == 2) { s.model = 1; s.harmonic = 0.7; s.decay = 4.0; s.excite = 0.6; s.spread = 0.05; s.release = 1.5; }
        else { s.model = 3; s.harmonic = 0.2; s.decay = 6.0; s.excite = 0.9; s.spread = 0.45; s.tone = 0.3; s.release = 3.0; }
        e.prepare(sr, 256); e.setSettings(s);
        V ev { { 0, 0x90, (uint8_t)(id == 2 ? 48 : 43), 110 }, { (uint32_t)(3.5 * sr), 0x80, (uint8_t)(id == 2 ? 48 : 43), 0 } };
        return renderJob(std::move(ep), sr, 4.5, ev, [](coil::Engine& x, float* l, float* r, int n, V& h) { x.process(nullptr, nullptr, l, r, n, h.data(), (int)h.size()); });
    }
    case 3:
    {
        auto ep = std::make_unique<phase::Engine>(); phase::Engine& e = *ep; phase::Settings s; s.engine = phase::kSix; s.algo = 14; s.chip = false; s.feedback = 0;
        const double ratio[6] = { 1, 3, 5, 7, 9, 3 }, fine[6] = { 0, 50, 41, 0, 0, 50 }, level[6] = { 90, 82, 76, 70, 64, 80 }, dec[6] = { 4.0, 3.0, 2.0, 1.6, 1.2, 2.5 };
        for (int k = 0; k < 6; ++k) { auto& o = s.op[k]; o.ratio = (int)ratio[k]; o.fine = fine[k]; o.level = level[k]; o.a = 0.001; o.d = dec[k]; o.s = 0.0; o.r = 2.0; o.vel = 0.0; }
        e.setSettings(s); e.prepare(sr, 256);
        V ev { { 0, 0x90, 60, 110 }, { (uint32_t)(3.5 * sr), 0x80, 60, 0 } };
        return renderJob(std::move(ep), sr, 4.5, ev, [](phase::Engine& x, float* l, float* r, int n, V& h) { x.process(l, r, n, h.data(), (int)h.size()); });
    }
    case 4:
    {
        auto ep = std::make_unique<wob::Engine>(); wob::Engine& e = *ep; wob::Settings s; s.partials = 160; s.wobSync = false; s.wobRate = 2.0; s.wobMorph = 1.0; s.shapeB = 1.0; s.tiltB = 0.6;
        s.wobCut = 2.0; s.cutoff = 500.0; s.res = 0.4; s.sub = 0.0; s.octave = 0; s.volumeDb = 0.0;
        e.setSettings(s); e.prepare(sr, 256);
        V ev { { 0, 0x90, 45, 110 }, { (uint32_t)(3.9 * sr), 0x80, 45, 0 } };
        return renderJob(std::move(ep), sr, 4.0, ev, [](wob::Engine& x, float* l, float* r, int n, V& h) { x.process(l, r, n, h.data(), (int)h.size()); });
    }
    case 5:
    {
        // a saw through a resonant low-pass sweeping up and back
        struct St { double ph = 0.0, lp = 0.0, bp = 0.0; };
        auto st = std::make_shared<St>();
        return genJob((int)(sr * 4.0), [st, sr](int i) {
            const double f = 110.0;
            const double t = i / sr, sw = 0.5 - 0.5 * std::cos(kTwoPi * t / 4.0);
            const double fc = 200.0 * std::pow(40.0, sw), g = 2.0 * std::sin(kPi * std::min(fc, sr * 0.2) / sr), q = 0.25;
            St& z = *st;
            z.ph += f / sr; if (z.ph >= 1.0) z.ph -= 1.0;
            const double saw = synth::blepSaw(z.ph, f / sr);
            z.lp += g * z.bp; const double hp = saw - z.lp - q * z.bp; z.bp += g * hp;
            return (float)(z.lp * std::min(1.0, t * 50.0) * std::min(1.0, (4.0 - t) * 20.0));
        });
    }
    default:
    {
        // a breathy flute: a few soft harmonics with vibrato, breath noise
        struct St { fx::Rng rng { 77u }; double ph = 0.0, nz = 0.0; };
        auto st = std::make_shared<St>();
        return genJob((int)(sr * 4.0), [st, sr](int i) {
            St& z = *st;
            const double t = i / sr, f = 440.0 * (1.0 + 0.006 * std::sin(kTwoPi * 5.2 * t) * std::min(1.0, t));
            z.ph += f / sr; if (z.ph >= 1.0) z.ph -= 1.0;
            z.nz += (z.rng.bi() - z.nz) * 0.25;
            const double env = std::min(1.0, t * 6.0) * std::min(1.0, (4.0 - t) * 4.0);
            const double tone = std::sin(kTwoPi * z.ph) + 0.25 * std::sin(2 * kTwoPi * z.ph) + 0.08 * std::sin(3 * kTwoPi * z.ph);
            return (float)((tone * (0.85 + 0.15 * z.nz) + 0.35 * z.nz) * env);
        });
    }
    }
}
inline std::vector<float> builtinAudio(int id, double sr)
{
    auto j = builtinJob(id, sr);
    while (!j->step(1 << 20)) {}
    return j->take();
}
} // namespace detail

}} // namespace pump::prism
