// pump GLOSS: every machine plays a held note at its pitch (the string within a few cents) and near the same
// loudness; the pitch envelope starts the note PITCH ENV semitones away and falls back; poly plays chords, mono one
// note, legato glides; voices end after their release; the chip's sync changes the timbre and keeps the pitch;
// bubbles chirp upward; random settings and MIDI stay finite. 44.1 and 48 kHz. CPU.
#include "../plugins/gloss/GlossEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::gloss;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static Settings clean(int m)   // no envelopes on the pitch, the filter off, no drive, a held note
{
    Settings s; s.machine = m; s.pitchEnv = 0; s.filterType = kFilterOff; s.drive = 0; s.width = 0; s.sustain = 1.0; s.attack = 0.001; s.lfoDepth = 0;
    if (m == kSuper) s.a = 0.0;
    if (m == kBubble) { s.a = 0.0; s.d = 0.0; s.b = 1.0; }
    if (m == kChip) { s.a = 0.0; s.c = 0.0; s.d = 0.0; }
    if (m == kPluck) { s.a = 0.05; s.d = 0.0; s.c = 0.0; }
    if (m == kDigi) { s.d = 0.0; s.c = 0.0; s.b = 1.0; }
    return s;
}
static void render(Engine& e, std::vector<float>& L, std::vector<float>& R, const std::vector<synth::Midi>& ev)
{
    R.assign(L.size(), 0.0f);
    const int n = (int)L.size();
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
    }
}
// the fundamental by autocorrelation (best lag, refined by a parabola)
static double pitchOf(const std::vector<float>& x, size_t a, size_t b, double sr, double lo = 50.0, double hi = 2000.0)
{
    const int minLag = (int)(sr / hi), maxLag = (int)(sr / lo);
    std::vector<double> ac((size_t)maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag) { double s = 0, e0 = 0, e1 = 0; for (size_t i = a; i + lag < b; ++i) { s += x[i] * x[i + lag]; e0 += x[i] * x[i]; e1 += x[i + lag] * x[i + lag]; } ac[(size_t)lag] = s / std::sqrt(e0 * e1 + 1e-20); }
    // the first lag whose correlation is close to the best (the period, not a multiple of it)
    double best = -1; for (int lag = minLag; lag <= maxLag; ++lag) best = std::max(best, ac[(size_t)lag]);
    int L = minLag;
    for (int lag = minLag + 1; lag < maxLag; ++lag) if (ac[(size_t)lag] > 0.9 * best && ac[(size_t)lag] >= ac[(size_t)lag - 1] && ac[(size_t)lag] >= ac[(size_t)lag + 1]) { L = lag; break; }
    const double y0 = ac[(size_t)L - 1], y1 = ac[(size_t)L], y2 = ac[(size_t)L + 1];
    const double d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2 + 1e-20);
    return sr / (L + d);
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; b = std::min(b, x.size()); for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }
static double db(double g) { return 20.0 * std::log10(g + 1e-12); }
static double cents(double f, double want) { return 1200.0 * std::log2(f / want); }

int main()
{
    for (double sr : { 44100.0, 48000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)sr;
        // ---- pitch, every machine (C4 = 261.63 Hz; A2 = 110 Hz)
        for (int m = 0; m < kNumMachines; ++m)
        {
            for (int note : { 45, 60 })
            {
                Engine e; e.setSettings(clean(m)); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R;
                render(e, L, R, { { 0, 0x90, (uint8_t)note, 100 } });
                const double want = synth::midiToHz(note);
                const double f = pitchOf(L, (size_t)(0.2 * sr), (size_t)(0.5 * sr), sr);
                const double c = cents(f, want);
                CHECK(std::fabs(c) < (m == kPluck ? 6.0 : 2.0), "%s: note %d at %.2f Hz (%+.1f cents)", kMachineNames[m], note, f, c);
            }
        }
        // ---- loudness: a held C4 at each machine's defaults
        {
            double lo = 0, hi = -200;
            for (int m = 0; m < kNumMachines; ++m)
            {
                Engine e; Settings s; s.machine = m; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R;
                render(e, L, R, { { 0, 0x90, 60, 100 } });
                const double r = db(rms(L, (size_t)(0.1 * sr), (size_t)(0.6 * sr)));
                lo = m == 0 ? r : std::min(lo, r); hi = std::max(hi, r);
            }
            CHECK(lo > -24 && hi < -16, "held C4 at the defaults: every machine between %.1f and %.1f dBFS RMS", lo, hi);
        }
        // ---- the pitch envelope
        {
            Engine e; Settings s = clean(kRubber); s.pitchEnv = 12; s.pitchTime = 0.3; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n), R;
            render(e, L, R, { { 0, 0x90, 57, 100 } });
            const double early = pitchOf(L, 0, (size_t)(0.03 * sr), sr, 100, 1000), late = pitchOf(L, (size_t)(0.6 * sr), (size_t)(0.8 * sr), sr, 100, 1000);
            CHECK(early > 380 && early < 445 && std::fabs(cents(late, 220.0)) < 3, "pitch envelope +12: starts near %.0f Hz, settles at %.1f Hz", early, late);
        }
        // ---- poly, mono, legato
        {
            Engine e; Settings s = clean(kSuper); e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L(8192), R;
            render(e, L, R, { { 0, 0x90, 60, 100 }, { 0, 0x90, 64, 100 }, { 0, 0x90, 67, 100 } });
            const int poly = e.voicesNow();
            Engine e2; s.mode = kMono; e2.setSettings(s); e2.prepare(sr, 512);
            render(e2, L, R, { { 0, 0x90, 60, 100 }, { 0, 0x90, 64, 100 }, { 0, 0x90, 67, 100 } });
            const int mono = e2.voicesNow();
            CHECK(poly == 3 && mono == 1, "a chord: %d voices in poly, %d in mono", poly, mono);
            Engine e3; Settings g = clean(kRubber); g.mode = kLegato; g.glide = 150; e3.setSettings(g); e3.prepare(sr, 512);
            std::vector<float> A((size_t)n);
            render(e3, A, R, { { 0, 0x90, 57, 100 }, { (uint32_t)(n / 2), 0x90, 69, 100 } });
            const double mid = pitchOf(A, (size_t)(n / 2 + 0.02 * sr), (size_t)(n / 2 + 0.05 * sr), sr, 100, 1000), end = pitchOf(A, (size_t)(0.9 * n), (size_t)n, sr, 100, 1000);
            CHECK(mid > 230 && mid < 420 && std::fabs(cents(end, 440.0)) < 3, "legato glides: %.0f Hz on the way, %.1f Hz at the end", mid, end);
        }
        // ---- voices end after their release
        {
            int left = 0;
            for (int m = 0; m < kNumMachines; ++m)
            {
                Engine e; Settings s; s.machine = m; s.release = 0.1; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R;
                render(e, L, R, { { 0, 0x90, 60, 100 }, { 100, 0x90, 64, 100 }, { (uint32_t)(n / 4), 0x80, 60, 0 }, { (uint32_t)(n / 4), 0x80, 64, 0 } });
                left += e.voicesNow();
            }
            CHECK(left == 0, "every machine's voices end after the release (%d left)", left);
        }
        // ---- the chip's sync: a new timbre, the same pitch
        {
            auto run = [&](double c) {
                Engine e; Settings s = clean(kChip); s.a = 1.0 / 7.999; s.c = c; e.setSettings(s); e.prepare(sr, 512);
                std::vector<float> L((size_t)n), R;
                render(e, L, R, { { 0, 0x90, 48, 100 } });
                return L;
            };
            const std::vector<float> a = run(0.0), b = run(0.4);
            double diff = 0; for (size_t i = (size_t)(0.2 * sr); i < (size_t)(0.4 * sr); ++i) diff += std::fabs(a[i] - b[i]);
            const double f = pitchOf(b, (size_t)(0.2 * sr), (size_t)(0.5 * sr), sr);
            CHECK(diff > 100.0 && std::fabs(cents(f, synth::midiToHz(48))) < 3, "chip sync: a different wave, still at %.2f Hz", f);
        }
        // ---- bubbles chirp up
        {
            Engine e; Settings s = clean(kBubble); s.a = 0.6; s.b = 0.6; e.setSettings(s); e.prepare(sr, 512);
            std::vector<float> L((size_t)n), R;
            render(e, L, R, { { 0, 0x90, 60, 100 } });
            const double f0 = pitchOf(L, 0, (size_t)(0.01 * sr), sr, 100, 4000), f1 = pitchOf(L, (size_t)(0.06 * sr), (size_t)(0.07 * sr), sr, 100, 4000);
            CHECK(f1 > f0 * 1.3, "a drop rises: %.0f Hz -> %.0f Hz", f0, f1);
        }
        // ---- random settings and MIDI
        {
            std::mt19937 rg(5); std::uniform_real_distribution<double> u(0, 1);
            bool finite = true; double hi = 0;
            for (int trial = 0; trial < 70; ++trial)
            {
                Engine e; Settings s;
                s.machine = trial % kNumMachines; s.a = u(rg); s.b = u(rg); s.c = u(rg); s.d = u(rg); s.tune = -24 + 48 * u(rg); s.pitchEnv = -48 + 96 * u(rg); s.pitchTime = 0.001 + 2 * u(rg);
                s.glide = 1000 * u(rg); s.mode = (int)(u(rg) * kNumModes); s.filterType = (int)(u(rg) * kNumFilters); s.cutoff = 30 * std::pow(600.0, u(rg)); s.res = u(rg);
                s.filterEnv = 2 * u(rg) - 1; s.attack = 0.0005 + u(rg); s.sustain = u(rg); s.release = 0.002 + u(rg); s.drive = u(rg); s.driveType = (int)(u(rg) * kNumDrives);
                s.crushBits = 2 + 14 * u(rg); s.crushRate = 1000 + 47000 * u(rg); s.width = u(rg); s.lfoShape = (int)(u(rg) * kNumLfo); s.lfoDest = (int)(u(rg) * kNumDests);
                s.lfoDepth = u(rg); s.lfoRate = 0.05 + 40 * u(rg);
                e.setSettings(s); e.prepare(sr, 512);
                std::vector<synth::Midi> ev;
                for (int k = 0; k < 24; ++k) ev.push_back({ (uint32_t)(u(rg) * n * 0.4), (uint8_t)(u(rg) < 0.6 ? 0x90 : 0x80), (uint8_t)(12 + u(rg) * 100), (uint8_t)(1 + u(rg) * 126) });
                ev.push_back({ (uint32_t)(n * 0.2), 0xE0, 0, (uint8_t)(u(rg) * 127) });
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                std::vector<float> L((size_t)n / 2), R;
                render(e, L, R, ev);
                for (size_t i = 0; i < L.size(); ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); hi = std::max(hi, (double)std::fabs(L[i])); }
            }
            CHECK(finite && hi < 8.0, "random settings and MIDI on every machine: finite, peak %.2f", hi);
        }
    }
    // ---- CPU: eight voices of each machine
    for (int m = 0; m < kNumMachines; ++m)
    {
        Engine e; Settings s; s.machine = m; s.sustain = 1.0; if (m == kPluck) s.d = 1.0; if (m == kBubble) s.d = 1.0; e.setSettings(s); e.prepare(48000, 512);
        std::vector<float> L(48000 * 4), R(L.size());
        std::vector<synth::Midi> ev; for (int k = 0; k < 8; ++k) ev.push_back({ 0, 0x90, (uint8_t)(48 + 3 * k), 100 });
        const auto a = std::chrono::steady_clock::now();
        for (size_t o = 0; o < L.size(); o += 256) e.process(L.data() + o, R.data() + o, 256, o == 0 ? ev.data() : nullptr, o == 0 ? (int)ev.size() : 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        printf("  CPU %-7s eight voices: %.1f %% of one core\n", kMachineNames[m], secs * 25.0);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
