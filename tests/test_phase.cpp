// pump PHASE: every engine plays its note in tune (a lone carrier, chip on and off); the chip's integer sine is a
// sine (harmonics well down); a modulator adds sidebands at the ratio; feedback brightens; the PC card's waveforms
// have their shapes (half sine: even harmonics; square: odd only); fixed-frequency operators ignore the note; the
// algorithms each build (every operator placed, modulators first); phase distortion: no bend is a cosine, full
// bend a saw (harmonics falling about 1/n), the resonant shapes peak at their bend; release ends a voice; every factory
// voice and random settings stay finite and bounded. CPU.
#include "../plugins/phase/PhaseEngine.hpp"
#include "dsp/FFT.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <random>
#include <string>

using namespace pump;
using namespace pump::phase;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double kSr = 48000.0;
static void render(Engine& e, std::vector<float>& L, std::vector<synth::Midi> ev, int blk = 256)
{
    std::vector<float> R(L.size());
    const int n = (int)L.size();
    for (int off = 0; off < n; off += blk)
    {
        const int m = std::min(blk, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
    }
}
static double freqOf(const std::vector<float>& x, size_t a, size_t b)
{
    double first = -1, last = -1; int c = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = (double)(i - 1) + x[i - 1] / (double)(x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++c; }
    return c > 1 ? (c - 1) * kSr / (last - first) : 0.0;
}
static std::vector<double> harmonics(const std::vector<float>& x, size_t at, double f0, int H)
{
    const int N = 16384;
    RealFFT fft(N); AlignedBuffer in((size_t)N), out((size_t)N);
    for (int i = 0; i < N; ++i) { const double t = kTwoPi * i / N; in[(size_t)i] = (float)(x[at + (size_t)i] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t))); }
    fft.forward(in.data(), out.data());
    auto pw = [&](int k) { return (double)out[(size_t)(2 * k)] * out[(size_t)(2 * k)] + (double)out[(size_t)(2 * k + 1)] * out[(size_t)(2 * k + 1)]; };
    std::vector<double> h;
    for (int j = 1; j <= H; ++j)
    {
        const int c = (int)std::lround(j * f0 * N / kSr);
        double p = 0; for (int k = c - 4; k <= c + 4; ++k) p += pw(k);
        h.push_back(10 * std::log10(p + 1e-30));
    }
    for (int j = H - 1; j >= 0; --j) h[(size_t)j] -= h[0];
    return h;
}
// one carrier alone: a 2-op pair with the modulator silent
static Settings lone(int engine, bool chip)
{
    Settings s; s.engine = engine; s.chip = chip; s.feedback = 0; s.tone = 1.0;
    s.algo = engine == kSix ? 0 : 0;
    for (auto& o : s.op) { o.level = 0; o.a = 0.001; o.d = 5; o.s = 1.0; o.r = 0.05; o.vel = 0; o.wave = 0; o.ratio = 1; }
    const Algo& A = algoFor(engine, s.algo);
    for (int k = 0; k < A.n; ++k) if (A.car >> k & 1) s.op[k].level = 99;
    if (engine == kPd) { s.algo = 0; s.op[0].level = 99; s.pdDcw1 = 0; s.pdWave1 = kPdSaw; }
    return s;
}
static synth::Midi on(int note, uint32_t at = 0, int vel = 100) { return { at, 0x90, (uint8_t)note, (uint8_t)vel }; }
static synth::Midi off(int note, uint32_t at) { return { at, 0x80, (uint8_t)note, 0 }; }

int main()
{
    printf("== tuning, every engine, chip on and off\n");
    for (int eng = 0; eng < kNumEngines; ++eng)
        for (int chip = 0; chip < 2; ++chip)
            for (int note : { 45, 69, 93 })
            {
                Engine e; e.prepare(kSr, 256); e.setSettings(lone(eng, chip)); e.prepare(kSr, 256);
                std::vector<float> L((size_t)kSr);
                render(e, L, { on(note) });
                const double want = synth::midiToHz(note), got = freqOf(L, 9600, L.size());
                const double cents = 1200.0 * std::log2(got / want);
                CHECK(std::fabs(cents) < 3.0, "%-17s chip %s: note %d at %.2f Hz (%+.2f cents)", kEngineNames[eng], chip ? "on " : "off", note, got, cents);
            }

    printf("== the chip's sine is a sine; the console's DAC adds grit, the others less\n");
    for (int eng : { kConsole, kArcade, kPc2, kSix })
    {
        Engine e; e.setSettings(lone(eng, true)); e.prepare(kSr, 256);
        std::vector<float> L((size_t)kSr);
        render(e, L, { on(57) });
        const auto h = harmonics(L, 20000, synth::midiToHz(57), 6);
        double worst = -200; for (int j = 1; j < 6; ++j) worst = std::max(worst, h[(size_t)j]);
        CHECK(worst < (eng == kConsole ? -25.0 : -60.0), "%s: harmonics 2-6 at most %.1f dB", kEngineNames[eng], worst);
    }

    printf("== a modulator at ratio 2 adds sidebands at 1f, 3f, 5f\n");
    {
        Settings s = lone(kSix, false); s.algo = 0; s.op[1].level = 80; s.op[1].ratio = 2;
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        std::vector<float> L((size_t)kSr);
        render(e, L, { on(45) });
        const auto h = harmonics(L, 20000, 110.0, 6);
        CHECK(h[2] > -30 && h[1] < h[2] - 20 && h[3] < h[2] - 20, "2 > 1 at ratio 2: 3f at %.1f dB, 2f %.1f, 4f %.1f (odd only)", h[2], h[1], h[3]);
    }
    printf("== feedback brightens a lone operator\n");
    {
        double hi[2];
        for (int fb : { 0, 6 })
        {
            Settings s = lone(kConsole, false); s.feedback = fb; s.algo = 7; for (int k = 0; k < 4; ++k) s.op[k].level = k == 0 ? 99 : 0;
            Engine e; e.setSettings(s); e.prepare(kSr, 256);
            std::vector<float> L((size_t)kSr);
            render(e, L, { on(45) });
            const auto h = harmonics(L, 20000, 110.0, 6);
            hi[fb ? 1 : 0] = h[1];
        }
        CHECK(hi[1] > hi[0] + 30 && hi[1] > -20, "feedback 6: 2nd harmonic %.1f dB (feedback 0: %.1f)", hi[1], hi[0]);
    }
    printf("== the PC card's waveforms\n");
    for (int chip = 0; chip < 2; ++chip)
    {
        auto run = [&](int wave) {
            Settings s = lone(kPc2, chip); s.algo = 1; s.op[0].level = 0; s.op[1].wave = wave;
            Engine e; e.setSettings(s); e.prepare(kSr, 256);
            std::vector<float> L((size_t)kSr);
            render(e, L, { on(45) });
            return harmonics(L, 20000, 110.0, 6);
        };
        const auto half = run(1), sq = run(6);
        CHECK(half[1] > -25, "chip %s: half sine has a 2nd harmonic (%.1f dB)", chip ? "on" : "off", half[1]);
        CHECK(sq[2] > -14 && sq[2] < -6 && sq[1] < -40, "chip %s: square: 3rd at %.1f dB (1/3 = -9.5), 2nd %.1f", chip ? "on" : "off", sq[2], sq[1]);
    }
    printf("== a fixed-frequency operator ignores the note\n");
    {
        double f[2]; int i = 0;
        for (int note : { 48, 72 })
        {
            Settings s = lone(kSix, false); s.op[0].fixed = true; s.op[0].ratio = 2; s.op[0].fine = 30;   // 100 Hz x 10^0.3 = 199.5
            Engine e; e.setSettings(s); e.prepare(kSr, 256);
            std::vector<float> L((size_t)kSr);
            render(e, L, { on(note) });
            f[i++] = freqOf(L, 9600, L.size());
        }
        CHECK(std::fabs(f[0] - 199.53) < 0.5 && std::fabs(f[1] - 199.53) < 0.5, "fixed 100 Hz x fine 30: %.2f Hz at C3 and %.2f Hz at C5", f[0], f[1]);
    }
    printf("== every algorithm places all its operators, modulators first\n");
    {
        int bad = 0, total = 0;
        for (int eng = 0; eng < kNumEngines - 1; ++eng)
            for (int a = 0; a < algoCount(eng); ++a)
            {
                const Algo& A = algoFor(eng, a); ++total;
                int pos[kOps]; for (int k = 0; k < A.n; ++k) pos[A.order[k]] = k;
                bool ok = A.car != 0;
                for (int i = 0; i < A.n; ++i) for (int j = 0; j < A.n; ++j) if ((A.mod[i] >> j & 1) && pos[j] >= pos[i]) ok = false;
                bool seen[kOps] = {}; for (int k = 0; k < A.n; ++k) seen[A.order[k]] = true;
                for (int k = 0; k < A.n; ++k) if (!seen[k]) ok = false;
                if (!ok) { ++bad; printf("    bad: %s / %s\n", kEngineNames[eng], A.name); }
            }
        CHECK(bad == 0, "%d algorithms, %d bad", total, bad);
    }
    printf("== phase distortion\n");
    {
        auto run = [&](int wave, double dcw) {
            Settings s = lone(kPd, false); s.pdWave1 = wave; s.pdDcw1 = dcw; s.pdA = 0.001; s.pdS = 1.0; s.pdKey = 0;
            Engine e; e.setSettings(s); e.prepare(kSr, 256);
            std::vector<float> L((size_t)kSr);
            render(e, L, { on(45) });
            return harmonics(L, 20000, 110.0, 8);
        };
        const auto plain = run(kPdSaw, 0.0), saw = run(kPdSaw, 1.0), res = run(kPdResSaw, 0.5);
        double worst = -200; for (int j = 1; j < 8; ++j) worst = std::max(worst, plain[(size_t)j]);
        CHECK(worst < -50, "no bend: a cosine (harmonics at most %.1f dB)", worst);
        CHECK(std::fabs(saw[1] - -6.0) < 3.0 && std::fabs(saw[3] - -12.0) < 4.0, "full bend saw: 2nd %.1f dB (-6), 4th %.1f (-12)", saw[1], saw[3]);
        int peak = 1; for (int j = 2; j < 8; ++j) if (res[(size_t)j] > res[(size_t)peak]) peak = j;
        CHECK(peak >= 7, "resonance saw at bend 50 %%: strongest above the fundamental at harmonic %d (8.5 wanted)", peak + 1);
    }
    printf("== release ends the voice\n");
    {
        Settings s = lone(kConsole, true); for (auto& o : s.op) o.r = 0.1;
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        std::vector<float> L((size_t)kSr);
        render(e, L, { on(60), off(60, 12000) });
        float tail = 0; for (size_t i = 36000; i < L.size(); ++i) tail = std::max(tail, std::fabs(L[i]));
        CHECK(tail < 1e-3 && e.voicesNow() == 0, "release 100 ms: silent after 0.5 s (peak %.2g), %d voices left", tail, e.voicesNow());
    }
    printf("== factory voices and random settings\n");
    {
        int bad = 0; float worstPk = 0;
        const auto& ps = presets();
        for (size_t p = 0; p < ps.size(); ++p)
        {
            // apply the preset over the parameter defaults
            std::map<std::string, float> val;
            for (const auto& sp : params()) val[sp.symbol] = sp.def;
            for (const auto& pv : ps[p].values) val[pv.symbol] = pv.value;
            auto g = [&](int i) { return val[params()[(size_t)i].symbol]; };
            Settings s;
            s.engine = (int)g(kEngine); s.algo = (int)g(kAlgo); s.feedback = (int)g(kFeedback); s.chip = g(kChip) >= 0.5f; s.tone = g(kTone) * 0.01;
            s.lfoRate = g(kLfoRate); s.lfoAm = g(kLfoAm) * 0.01; s.lfoPm = g(kLfoPm) * 0.01; s.voices = (int)g(kVoices); s.voiceMode = (int)g(kVoiceMode);
            s.glide = g(kGlide); s.transpose = (int)g(kTranspose); s.volumeDb = g(kVolume); s.pitchEnv = g(kPitchEnv); s.pitchDecay = g(kPitchDecay) * 0.001;
            for (int k = 0; k < kOps; ++k)
            {
                OpSettings& o = s.op[k];
                o.wave = (int)g(op(k, kOpWave)); o.ratio = (int)g(op(k, kOpRatio)); o.fine = g(op(k, kOpFine)); o.detune = g(op(k, kOpDetune)); o.level = g(op(k, kOpLevel));
                o.a = g(op(k, kOpA)) * 0.001; o.d = g(op(k, kOpD)) * 0.001; o.s = g(op(k, kOpS)) * 0.01; o.r = g(op(k, kOpR)) * 0.001; o.vel = g(op(k, kOpVel)) * 0.01;
                o.am = g(op(k, kOpAm)) >= 0.5f; o.fixed = g(op(k, kOpFixed)) >= 0.5f;
            }
            s.pdWave1 = (int)g(kPdWave1); s.pdWave1b = (int)g(kPdWave1b); s.pdWave2 = (int)g(kPdWave2); s.pdWave2b = (int)g(kPdWave2b);
            s.pdDcw1 = g(kPdDcw1) * 0.01; s.pdDcw2 = g(kPdDcw2) * 0.01; s.pdKey = g(kPdKey) * 0.01; s.pdA = g(kPdA) * 0.001; s.pdD = g(kPdD) * 0.001;
            s.pdS = g(kPdS) * 0.01; s.pdR = g(kPdR) * 0.001; s.pdDetune = g(kPdDetune); s.pdOctave = (int)g(kPdOctave); s.pdMod = (int)g(kPdMod);
            Engine e; e.setSettings(s); e.prepare(kSr, 256);
            std::vector<float> L((size_t)(kSr * 1.5));
            render(e, L, { on(60), on(64, 100), on(67, 200), off(60, 48000), off(64, 48000), off(67, 48000) });
            float pk = 0; double en = 0; bool fin = true;
            for (float x : L) { fin = fin && std::isfinite(x); pk = std::max(pk, std::fabs(x)); en += (double)x * x; }
            const double rms = std::sqrt(en / L.size());
            printf("    %-22s peak %.2f  rms %5.1f dB\n", ps[p].name, pk, 20 * std::log10(rms + 1e-12));
            if (!fin || pk > 1.5f || rms < 1e-4) ++bad;
            worstPk = std::max(worstPk, pk);
        }
        CHECK(bad == 0, "%zu factory voices: all finite, audible, peak at most %.2f", ps.size(), worstPk);
        std::mt19937 rng(7); std::uniform_real_distribution<double> u(0, 1);
        bool fin = true; float pk = 0;
        for (int t = 0; t < 40; ++t)
        {
            Settings s; s.engine = (int)(u(rng) * kNumEngines); s.algo = (int)(u(rng) * 18); s.feedback = (int)(u(rng) * 8); s.chip = u(rng) < 0.5;
            s.tone = u(rng); s.lfoAm = u(rng); s.lfoPm = u(rng); s.voiceMode = (int)(u(rng) * 3); s.glide = u(rng) * 300; s.pitchEnv = u(rng) * 96 - 48;
            for (auto& o : s.op) { o.wave = (int)(u(rng) * 8); o.ratio = (int)(u(rng) * 32); o.fine = u(rng) * 99; o.level = u(rng) * 99; o.a = 0.0005 + u(rng) * 0.3; o.d = 0.01 + u(rng); o.s = u(rng); o.r = 0.01 + u(rng); o.fixed = u(rng) < 0.2; o.am = u(rng) < 0.5; }
            s.pdWave1 = (int)(u(rng) * 8); s.pdWave2 = (int)(u(rng) * 8); s.pdWave1b = (int)(u(rng) * 9); s.pdWave2b = (int)(u(rng) * 9);
            s.pdDcw1 = u(rng); s.pdDcw2 = u(rng); s.pdMod = (int)(u(rng) * 3); s.pdOctave = (int)(u(rng) * 5) - 2;
            Engine e; e.setSettings(s); e.prepare(t % 2 ? 44100.0 : 96000.0, 256);
            std::vector<float> L(24000);
            std::vector<synth::Midi> ev;
            for (int k = 0; k < 12; ++k) ev.push_back(on(24 + (int)(u(rng) * 90), (uint32_t)(k * 1500), 1 + (int)(u(rng) * 126)));
            ev.push_back({ 3000, 0xE0, 0, (uint8_t)(u(rng) * 127) });
            for (int k = 0; k < 6; ++k) ev.push_back(off(24 + (int)(u(rng) * 90), (uint32_t)(9000 + k * 1000)));
            std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
            render(e, L, ev, 64 + t * 7);
            for (float x : L) { fin = fin && std::isfinite(x); pk = std::max(pk, std::fabs(x)); }
        }
        CHECK(fin && pk < 4.0f, "random settings, rates and MIDI: finite and bounded (peak %.2f)", pk);
    }
    printf("== cpu\n");
    for (int eng : { kSix, kConsole, kPd })
        for (int chip = 0; chip < 2; ++chip)
        {
            Settings s; s.engine = eng; s.algo = eng == kSix ? 12 : 0; s.chip = chip; s.voices = 16; s.feedback = 5;
            for (auto& o : s.op) { o.level = 80; o.s = 1.0; }
            Engine e; e.setSettings(s); e.prepare(kSr, 256);
            std::vector<synth::Midi> ev; for (int k = 0; k < 16; ++k) ev.push_back(on(40 + k * 3));
            std::vector<float> L((size_t)kSr * 2);
            const auto t0 = std::chrono::steady_clock::now();
            render(e, L, ev);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: %s chip %s, 16 voices: %.1f %% of one core\n", kEngineNames[eng], chip ? "on " : "off", 100.0 * secs / 2.0);
        }
    std::printf(fails ? "test_phase: %d FAILED\n" : "test_phase: all passed\n", fails);
    return fails ? 1 : 0;
}
