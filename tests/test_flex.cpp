// pump FLEX: the engine against the original. tools/flex-ref.mjs renders reference output from the Thunder Plus page's own
// code (a fixed 120 BPM drum-like input, four bars, every pattern: FLX 01-FF and FLY 01-FF, plus two cases: the pattern
// changing from block to block through a clock wrap, and the transport stopping and restarting); this test renders the
// same input through FlexEngine, block by block with the host's position at the start of each block, and compares:
// every one of the 510 patterns (and the names, which come from the same table) within 1e-4. The two sides run the same
// maths in double precision on a float32 buffer; what remains is the last bits of sin / cos / pow / log (V8's and the C
// library's differ by an ulp) reaching the output through the read position (about 1e-12 in time, so about 1e-9 in level
// on this input) plus float32 rounding of the output, which both sides do. The tolerance is far above that and far below
// anything audible: a wrong pattern, hash, or branch is off by 0.01 to 1.
// The references are made with `node tools/flex-ref.mjs <thunder-plus>/index.html` (about 1.5 GB in a temp folder, never
// committed) in $FLEX_REF_DIR (default $TMPDIR/flex-ref, or /tmp/flex-ref); without them the parity part is skipped and
// says so. The rest runs always: nothing playing = the input exactly; a stopped transport passes through; every pattern
// is finite and bounded at any block size and sample rate; the slot mapping and the names; CPU.
#include "../plugins/flex/FlexEngine.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace pump;
using namespace pump::flex;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static const double SR = 48000.0;
static const int BLOCK = 512, NS = 384000;

static std::vector<float> readF32(const std::string& path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path.c_str(), "rb"))
    {
        std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        v.resize((size_t)n / 4);
        if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear();
        std::fclose(f);
    }
    return v;
}
static std::string refDir()
{
    if (const char* e = std::getenv("FLEX_REF_DIR")) if (*e) return e;
    if (const char* t = std::getenv("TMPDIR")) if (*t) return std::string(t) + "/flex-ref";
    return "/tmp/flex-ref";
}
static Transport tp(bool playing, double bpm, double ppq) { Transport t; t.playing = playing; t.valid = true; t.bpm = bpm; t.ppq = ppq; return t; }
static double ppqAt(double start, double bpm, int s) { return start + s * bpm / 60 / SR; }   // the same expression as flex-ref.mjs

// render the input in blocks: flxAt(block), transportAt(block, first sample)
template <class FF, class TF>
static void render(std::vector<float>& L, std::vector<float>& R, double sr, int block, FF flxAt, TF trAt)
{
    Engine e; Settings s; e.setSettings(s); e.prepare(sr, block);
    const int n = (int)L.size();
    for (int b = 0, o = 0; o < n; ++b, o += block)
    {
        const int m = std::min(block, n - o);
        s.flx = flxAt(b); e.setSettings(s);
        e.setTransport(trAt(b, o));
        e.process(L.data() + o, R.data() + o, m);
    }
}
static std::string family(int flx)
{
    static const char* const x[] = { "", "ROLL", "STUTTER", "BUILD", "REVERSE", "DOWN", "UP", "TAPE", "SCRATCH", "GATE", "FILTER", "CRUSH", "FADE REPEAT", "JUGGLE", "GROOVE", "CHAOS" };
    static const char* const y[] = { "", "PITCH ROLL", "EUCLID", "PROB", "SLICE PITCH", "FREEZE/SMEAR", "CD SKIP", "BOUNCE", "MUTATE", "STUTTER EDIT", "POLYMETER", "WARP", "REVERSE ROLL", "GRAINS", "TEXTURE", "CHAOS 2" };
    char b[64];
    if (flx <= 16) return "FLX 01-10 hand-written";
    if (flx <= 255) { std::snprintf(b, sizeof b, "FLX %X_ %s", flx >> 4, x[flx >> 4]); return b; }
    const int v = flx - 256;
    if (v <= 15) return "FLY 01-0F (one of each)";
    std::snprintf(b, sizeof b, "FLY %X_ %s", v >> 4, y[v >> 4]); return b;
}

static void parity()
{
    const std::string dir = refDir();
    const std::vector<float> in = readF32(dir + "/input.f32");
    if ((int)in.size() != NS * 2)
    {
        printf("  SKIP: no references in %s (make them: node tools/flex-ref.mjs <thunder-plus>/index.html %s)\n", dir.c_str(), dir.c_str());
        return;
    }
    std::vector<float> L0((size_t)NS), R0((size_t)NS);
    for (int i = 0; i < NS; ++i) { L0[(size_t)i] = in[(size_t)(2 * i)]; R0[(size_t)i] = in[(size_t)(2 * i + 1)]; }
    // Samples where the REFERENCE is not finite are counted, not compared: the original's read can land exactly on the end of
    // its history buffer (x = w - D + N rounds up to N when w - D is a hair below 0) and read past it: one NaN sample. The port
    // reads the start of the buffer there instead. A non-finite sample from the port is always a failure.
    int refNaN = 0, refNaNPatterns = 0;
    auto compare = [&](const std::vector<float>& L, const std::vector<float>& R, const std::string& file, double& err) {
        const std::vector<float> ref = readF32(dir + "/" + file);
        if ((int)ref.size() != NS * 2) return false;
        err = 0; int skipped = 0;
        for (int i = 0; i < NS; ++i)
        {
            const float a = L[(size_t)i], b = R[(size_t)i], c = ref[(size_t)(2 * i)], d = ref[(size_t)(2 * i + 1)];
            if (!std::isfinite(a) || !std::isfinite(b)) { err = INFINITY; return true; }   // never let a NaN pass as a small error
            if (!std::isfinite(c) || !std::isfinite(d)) { ++skipped; continue; }
            err = std::max(err, std::max(std::fabs((double)a - c), std::fabs((double)b - d)));
        }
        if (skipped) { refNaN += skipped; ++refNaNPatterns; }
        return true;
    };
    // names
    {
        std::map<int, std::string> ref;
        if (FILE* f = std::fopen((dir + "/names.txt").c_str(), "rb"))
        {
            char line[512];
            while (std::fgets(line, sizeof line, f))
            {
                std::string s(line); while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
                const size_t t = s.find('\t'); if (t != std::string::npos) ref[std::atoi(s.substr(0, t).c_str())] = s.substr(t + 1);
            }
            std::fclose(f);
        }
        int bad = 0;
        for (int k = 1; k <= 511; ++k)
        {
            if (k == 256) continue;
            if (ref[k] != patternName(k)) { if (++bad <= 5) printf("    name %d: \"%s\" vs the page's \"%s\"\n", k, patternName(k).c_str(), ref[k].c_str()); }
        }
        CHECK(ref.size() == 510 && bad == 0, "all 510 names match the page's (%d differ, %d listed)", bad, (int)ref.size());
    }
    // every pattern
    std::map<std::string, double> famMax; std::map<std::string, int> famN;
    double worst = 0; int worstK = 0, missing = 0, over = 0;
    const double tol = 1e-4;
    for (int k = 1; k <= 511; ++k)
    {
        if (k == 256) continue;
        std::vector<float> L = L0, R = R0;
        render(L, R, SR, BLOCK, [k](int) { return k; }, [](int, int o) { return tp(true, 120, ppqAt(0, 120, o)); });
        double err = 0;
        char f[32]; std::snprintf(f, sizeof f, "ref_%03d.f32", k);
        if (!compare(L, R, f, err)) { ++missing; continue; }
        const std::string fam = family(k);
        famMax[fam] = std::max(famMax[fam], err); ++famN[fam];
        if (err > worst) { worst = err; worstK = k; }
        if (err > tol) { ++over; if (over <= 8) printf("    pattern %d (%s %s): max error %.3g\n", k, k <= 255 ? "FLX" : "FLY", patternName(k).c_str(), err); }
    }
    printf("  max abs error per family (float32 output; tolerance %.0e):\n", tol);
    for (auto& kv : famMax) printf("    %-28s %3d patterns  %.3g\n", kv.first.c_str(), famN[kv.first], kv.second);
    if (refNaN) printf("  note: the original read past its buffer end on %d samples in %d patterns (NaN in the reference; the port is finite there)\n", refNaN, refNaNPatterns);
    CHECK(missing == 0, "all 510 reference files present (%d missing)", missing);
    CHECK(over == 0, "all patterns within %.0e: worst error %.3g (flx %d)", tol, worst, worstK);
    // the pattern changing while it plays
    {
        static const int kChange[][2] = { { 0, 0 }, { 60, 0x1A }, { 140, 0x7A }, { 220, 0xB1 }, { 300, 256 + 0x23 }, { 380, 256 + 0x5C }, { 460, 5 }, { 540, 0xF3 }, { 620, 256 + 0xE2 }, { 700, 0 } };   // (as flex-ref.mjs)
        std::vector<float> L = L0, R = R0; double err = 0;
        render(L, R, SR, BLOCK, [](int b) { int f = 0; for (auto& c : kChange) if (b >= c[0]) f = c[1]; return f; }, [](int, int o) { return tp(true, 128, ppqAt(13.3, 128, o)); });
        const bool ok = compare(L, R, "case_change.f32", err);
        CHECK(ok && err < tol, "pattern changes mid-bar at 128 BPM through the 16-beat wrap: max error %.3g", err);
    }
    // the transport stopping and restarting elsewhere
    {
        std::vector<float> L = L0, R = R0; double err = 0;
        render(L, R, SR, BLOCK, [](int) { return 0x2B; }, [](int b, int o) { return b < 100 ? tp(true, 120, ppqAt(0, 120, o)) : b < 180 ? tp(false, 120, 0) : tp(true, 120, ppqAt(9.1, 120, o - 180 * BLOCK)); });
        const bool ok = compare(L, R, "case_stop.f32", err);
        CHECK(ok && err < tol, "transport stops (blocks 100-179: straight through) and restarts at beat 9.1: max error %.3g", err);
    }
}

int main()
{
    printf("== parity with the Thunder Plus effect\n");
    parity();

    printf("== clock edge cases\n");
    {
        // a phase correction across the 16-beat wrap must not take the clock negative (that would mean "no clock")
        Engine e; Settings s; s.flx = 0x2B; e.setSettings(s); e.prepare(SR, 512);
        std::vector<float> L(64, 0.1f), R(64, 0.1f);
        e.setTransport(tp(true, 20, 0.0)); e.process(L.data(), R.data(), 1);
        e.setTransport(tp(true, 20, 15.9995)); e.process(L.data(), R.data(), 64);
        const float ph = e.phaseNow();
        CHECK(ph >= 0.0f && ph < 16.0f, "a correction across the 16-beat wrap keeps the clock in [0, 16) (phase %.5f)", ph);
        // 5-tuplet feel one hair before a beat: the slice index must not go below 0
        Engine e2; s.flx = 0xE8; e2.setSettings(s); e2.prepare(SR, 512);
        std::vector<float> L2(512, 0.3f), R2(512, 0.3f);
        e2.setTransport(tp(true, 120, 0.9999999995)); e2.process(L2.data(), R2.data(), 512);
        bool fin = true; for (int i = 0; i < 512; ++i) fin = fin && std::isfinite(L2[(size_t)i]) && std::isfinite(R2[(size_t)i]);
        CHECK(fin && patternName(0xE8) == "5-TUPLET FEEL", "5-TUPLET FEEL at p = 0.9999999995 is finite");
    }

    printf("== names and slots\n");
    CHECK(patternName(1) == "REPEAT 1/4" && patternName(16) == "RANDOM SLICES", "the hand-named first sixteen");
    CHECK(patternName(0x1A) == "ROLL 1/32 FROM BEAT 3", "FLX 1A: \"%s\"", patternName(0x1A).c_str());
    CHECK(slotLabel(0x1A) == "FLX 1A ROLL 1/32 FROM BEAT 3", "slot label \"%s\"", slotLabel(0x1A).c_str());
    CHECK(slotToFlx(255) == 255 && slotToFlx(256) == 257 && slotToFlx(510) == 511 && slotToFlx(0) == 0, "slot 256-510 is FLY 01-FF (flx 257-511)");
    bool inv = true; for (int s = 0; s <= 510; ++s) inv = inv && flxToSlot(slotToFlx(s)) == s;
    CHECK(inv, "slot <-> flx round trip");
    int none = 0; for (int k = 17; k <= 511; ++k) if (k != 256 && table()[(size_t)k].f == Fam::None) ++none;
    CHECK(none == 0, "every pattern 17-255 and 257-511 is defined (%d empty)", none);
    CHECK(slotLabel(256).rfind("FLY 01 ", 0) == 0 && slotLabel(510).rfind("FLY FF ", 0) == 0, "FLY labels: \"%s\", \"%s\"", slotLabel(256).c_str(), slotLabel(510).c_str());

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)(sr * 8.0);
        std::vector<float> L0((size_t)n), R0((size_t)n);
        unsigned seed = 1;
        for (int i = 0; i < n; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float nz = (float)((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f;
            L0[(size_t)i] = 0.4f * std::sin(0.03f * (float)(i % 4000)) + 0.2f * nz; R0[(size_t)i] = 0.3f * std::sin(0.05f * (float)(i % 3000)) - 0.2f * nz;
        }
        // nothing playing (flx 0) and a stopped transport: the input exactly
        {
            std::vector<float> L = L0, R = R0;
            render(L, R, sr, 333, [](int) { return 0; }, [&](int, int o) { return tp(true, 120, o * 2.0 / sr); });
            double err = 0; for (int i = 0; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)i]) + std::fabs(R[(size_t)i] - R0[(size_t)i]));
            CHECK(err == 0.0, "off: output = input exactly (max error %.2g)", err);
            L = L0; R = R0;
            render(L, R, sr, 333, [](int) { return 0x2B; }, [&](int, int) { return tp(false, 120, 0); });
            err = 0; for (int i = 0; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)i]) + std::fabs(R[(size_t)i] - R0[(size_t)i]));
            CHECK(err == 0.0, "a stopped host: straight through, any pattern (max error %.2g)", err);
        }
        // every pattern: finite and bounded at odd block sizes, a tempo that isn't 120, and a bar length that isn't 4/4's
        {
            int bad = 0; double peak = 0;
            for (int k = 1; k <= 511; k += (sr == 48000.0 ? 1 : 7))   // (every pattern at 48 kHz, a spread of them at the other rates)
            {
                if (k == 256) continue;
                std::vector<float> L = L0, R = R0;
                const int blk = 64 + (k * 37) % 900;
                render(L, R, sr, blk, [k](int) { return k; }, [&](int, int o) { Transport t = tp(true, 97.5, 3.0 + o * 97.5 / 60.0 / sr); t.barQuarters = 3.0; return t; });
                for (int i = 0; i < n; ++i) { const float a = std::fabs(L[(size_t)i]), b = std::fabs(R[(size_t)i]); if (!(a < 8.0f) || !(b < 8.0f)) { ++bad; break; } peak = std::max(peak, (double)std::max(a, b)); }
            }
            CHECK(bad == 0, "patterns finite and bounded at odd block sizes, 97.5 BPM (peak %.2f)", peak);
        }
        // mix 0 = the input, out gain
        {
            std::vector<float> L = L0, R = R0;
            Engine e; Settings s; s.flx = 0x2B; s.mix = 0; e.setSettings(s); e.prepare(sr, 512);
            double err = 0;
            for (int o = 0; o + 512 <= n; o += 512) { e.setTransport(tp(true, 120, o * 2.0 / sr)); e.process(L.data() + o, R.data() + o, 512); }
            for (int i = 0; i + 512 <= n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)i]));
            CHECK(err < 1e-6, "mix 0 = the input (max error %.2g)", err);
            L = L0; R = R0;
            Engine e2; Settings s2; s2.outDb = -6.0206; e2.setSettings(s2); e2.prepare(sr, 512);
            for (int o = 0; o + 512 <= n; o += 512) { e2.setTransport(tp(true, 120, o * 2.0 / sr)); e2.process(L.data() + o, R.data() + o, 512); }
            err = 0; for (int i = 0; i + 512 <= n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - 0.5f * L0[(size_t)i]));
            CHECK(err < 1e-5, "out -6 dB halves the level (max error %.2g)", err);
        }
        // CPU: every pattern, per second of audio
        {
            const auto t0 = std::chrono::steady_clock::now();
            double secs = 0;
            for (int k = 1; k <= 511; k += 5)
            {
                std::vector<float> L(L0.begin(), L0.begin() + (long)(sr * 2)), R(R0.begin(), R0.begin() + (long)(sr * 2));
                render(L, R, sr, 512, [k](int) { return k; }, [&](int, int o) { return tp(true, 120, o * 2.0 / sr); });
                secs += 2.0;
            }
            const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: %.2f%% of one core over a sample of patterns (render + engine setup)\n", 100.0 * dt / secs);
        }
    }
    printf(fails ? "FAILED (%d)\n" : "all ok\n", fails);
    return fails ? 1 : 0;
}
