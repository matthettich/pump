// pump LIFE: the life rules (a blinker blinks, a glider glides one cell diagonally every four generations, a block
// stays; HighLife's replicator rule; Seeds leaves nothing alive); LOOP repeats the same hits every cycle and follows a
// jump in the song; EVOLVE runs on and reseeds when the board dies; a tone with nothing on it is a sine at its pitch;
// KICK drops the pitch; MIDI 36-43 play the tones; mute silences one; random settings, patterns and transport stay
// finite and bounded. CPU.
#include "../plugins/life/LifeEngine.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::life;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)
static const double kSr = 48000.0;

static int pop(const Board& b) { int n = 0; for (auto r : b) n += __builtin_popcount(r); return n; }
static Board shifted(const Board& b, int dx, int dy, int W, int H)
{
    Board o {}; o.fill(0);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) if ((b[(size_t)y] >> x) & 1) o[(size_t)((y + dy + H) % H)] |= (uint16_t)(1u << ((x + dx + W) % W));
    return o;
}
static double freqOf(const std::vector<float>& x, size_t a, size_t b)
{
    double first = -1, last = -1; int c = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = (double)(i - 1) + x[i - 1] / (double)(x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++c; }
    return c > 1 ? (c - 1) * kSr / (last - first) : 0.0;
}
static Settings quiet()
{
    Settings s; s.mix = 0.0; s.drive = 0.0; s.lfoDepth = 0.0; s.width = 0.0;
    for (auto& t : s.tone) { t.kick = 0; t.fm = 0; t.ring = 0; }
    return s;
}

int main()
{
    printf("== the rules\n");
    {
        const int W = 16, H = 16;
        Board blink = patternBoard(3, W, H);
        const Board b1 = stepBoard(blink, W, H, kLife), b2 = stepBoard(b1, W, H, kLife);
        CHECK(b1 != blink && b2 == blink && pop(b1) == pop(blink), "blinkers: period 2 (%d cells)", pop(blink));
        Board g = patternBoard(1, W, H), g4 = g;
        for (int k = 0; k < 4; ++k) g4 = stepBoard(g4, W, H, kLife);
        CHECK(g4 == shifted(g, 1, 1, W, H), "a glider: one cell down-right every 4 generations");
        Board wrap = g; for (int k = 0; k < 64; ++k) wrap = stepBoard(wrap, W, H, kLife);
        CHECK(wrap == g, "the board wraps: after 64 generations the glider is home");
        Board block {}; block.fill(0); block[5] = block[6] = (uint16_t)(3u << 5);
        CHECK(stepBoard(block, W, H, kLife) == block, "a block is still");
        Board seeds = patternBoard(12, W, H); bool alive2 = false;
        for (int k = 0; k < 6; ++k) { const Board n = stepBoard(seeds, W, H, kSeeds); for (int y = 0; y < H; ++y) if (n[(size_t)y] & seeds[(size_t)y]) alive2 = true; seeds = n; }
        CHECK(!alive2, "Seeds: no cell survives a generation");
        Board pulsar = patternBoard(4, W, H), p3 = pulsar;
        for (int k = 0; k < 3; ++k) p3 = stepBoard(p3, W, H, kLife);
        CHECK(p3 == pulsar && pop(pulsar) == 48, "the pulsar: 48 cells, period 3");
        Board gx = patternBoard(10, W, H), g8 = gx; for (int k = 0; k < 8; ++k) g8 = stepBoard(g8, W, H, kLife);
        Board g1 = stepBoard(gx, W, H, kLife);
        CHECK(g8 == gx && g1 != gx && pop(gx) == 48, "the galaxy: 48 cells, period 8");
    }

    printf("== LOOP repeats with the song\n");
    {
        Settings s = quiet(); s.mode = kModeLoop; s.length = 8; s.pattern = 5; s.sens = 1.0;   // R-pentomino: no two steps alike
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        const int stepN = 6000;   // a 1/16 at 120 BPM
        std::vector<float> L(stepN), R(stepN);
        std::vector<uint32_t> hits;
        Transport tr; tr.playing = true; tr.valid = true; tr.bpm = 120.0;
        for (int st = 0; st < 24; ++st)
        {
            tr.ppq = st * 0.25; e.setTransport(tr);
            e.process(L.data(), R.data(), stepN, nullptr, 0);
            hits.push_back(e.takeHits());
        }
        bool same = true, any = false;
        for (int st = 0; st < 16; ++st) { same = same && hits[(size_t)st] == hits[(size_t)st + 8]; any = any || hits[(size_t)st]; }
        CHECK(same && any, "8-step cycle: steps 0-15 repeat at 8-23 (first hits %02x %02x %02x %02x)", hits[0], hits[1], hits[2], hits[3]);
        // a jump back in the song lands on the same hits
        tr.ppq = 3 * 0.25; e.setTransport(tr);
        e.process(L.data(), R.data(), stepN, nullptr, 0);
        const uint32_t j = e.takeHits();
        CHECK(j == hits[3], "a jump to step 3: the same hits as before (%02x)", j);
        CHECK(e.generationNow() == 3, "and the board is at generation 3 (%d)", e.generationNow());
    }
    printf("== EVOLVE runs on, and reseeds a dead board\n");
    {
        Settings s = quiet(); s.mode = kModeEvolve; s.pattern = 1; s.cols = 5; s.rows = 5; s.reseed = true; s.sens = 1.0;
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        std::vector<float> L(6000), R(6000);
        Transport tr; tr.playing = true; tr.valid = true; tr.bpm = 120.0;
        int maxGen = 0;
        for (int st = 0; st < 40; ++st) { tr.ppq = st * 0.25; e.setTransport(tr); e.process(L.data(), R.data(), 6000, nullptr, 0); maxGen = std::max(maxGen, e.generationNow()); }
        CHECK(maxGen >= 20 && pop(e.boardNow()) > 0, "a glider on a 5 x 5 torus: generation %d reached, the board is alive (%d cells)", maxGen, pop(e.boardNow()));
        // a pattern that dies (a lone pair) comes back
        Settings d = s; d.pattern = 0; Engine f; f.setSettings(d); f.prepare(kSr, 256);
        Board two {}; two.fill(0); two[2] = 3; f.setDrawn(two); f.requestReset();
        bool back = false;
        for (int st = 0; st < 6; ++st) { tr.ppq = st * 0.25; f.setTransport(tr); f.process(L.data(), R.data(), 6000, nullptr, 0); if (st > 1 && pop(f.boardNow()) == 2) back = true; }
        CHECK(back, "a drawn pair dies and is reseeded from the drawing");
    }
    printf("== the tones\n");
    {
        Settings s = quiet(); s.mode = kModeLoop;
        for (auto& t : s.tone) { t.decay = 2.0; t.level = 0.9; }
        s.tone[2].pitch = 69;
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        std::vector<float> L(24000), R(24000);
        synth::Midi on { 0, 0x90, 38, 110 };
        e.process(L.data(), R.data(), 24000, &on, 1);
        const double f = freqOf(L, 2400, 24000);
        CHECK(std::fabs(1200 * std::log2(f / 440.0)) < 3.0, "MIDI note 38 plays tone 3 at its pitch A4: %.2f Hz", f);
        // the parabolic sine: harmonics small (3rd about -40 dB)
        Settings k = quiet(); k.tone[0].pitch = 45; k.tone[0].kick = 1.0; k.tone[0].decay = 1.0;
        Engine g; g.setSettings(k); g.prepare(kSr, 256);
        std::vector<float> A(24000), B(24000);
        synth::Midi on0 { 0, 0x90, 36, 127 };
        g.process(A.data(), B.data(), 24000, &on0, 1);
        const double early = freqOf(A, 10, 240), late = freqOf(A, 9600, 24000);
        CHECK(early > 4 * late && std::fabs(late - 110.0) < 1.0, "KICK: %.0f Hz in the first 5 ms, settling to %.1f Hz", early, late);
        Settings m = quiet(); m.tone[0].mute = true;
        Engine h; h.setSettings(m); h.prepare(kSr, 256);
        h.process(A.data(), B.data(), 24000, &on0, 1);
        float pk = 0; for (float x : A) pk = std::max(pk, std::fabs(x));
        CHECK(pk < 1e-4f, "a muted tone stays silent (peak %.2g)", pk);
    }
    printf("== random settings and transport\n");
    {
        std::mt19937 rng(11); std::uniform_real_distribution<double> u(0, 1);
        bool fin = true; float pk = 0;
        for (int t = 0; t < 30; ++t)
        {
            Settings s; s.mode = (int)(u(rng) * 3); s.rate = (int)(u(rng) * 4); s.length = 1 + (int)(u(rng) * 64); s.offset = (int)(u(rng) * 64);
            s.sens = u(rng); s.xWrap = (int)(u(rng) * 8); s.yWrap = (int)(u(rng) * 8); s.colour = (int)(u(rng) * 8); s.cols = 4 + (int)(u(rng) * 13); s.rows = 4 + (int)(u(rng) * 13);
            s.rule = (int)(u(rng) * kNumRules); s.trigger = (int)(u(rng) * 3); s.pattern = (int)(u(rng) * kNumPatterns); s.reseed = u(rng) < 0.5;
            s.drive = u(rng); s.fmAll = u(rng); s.decayAll = 0.25 + u(rng) * 3.75; s.lfoDepth = u(rng); s.pitchRange = u(rng) * 48 - 24; s.lfoRate = 0.05 + u(rng) * 20;
            s.shift = u(rng) * 48 - 24; s.grain = 0.01 + u(rng) * 0.19; s.time = 0.001 + u(rng); s.feedback = u(rng); s.filter = u(rng) * 2 - 1; s.fxDecay = 0.005 + u(rng) * 2; s.mix = u(rng);
            for (auto& o : s.tone) { o.pitch = 24 + u(rng) * 72; o.kick = u(rng); o.fm = u(rng); o.ring = u(rng); o.decay = 0.005 + u(rng) * 2; o.level = u(rng); o.mute = u(rng) < 0.1; }
            Engine e; e.setSettings(s); e.prepare(t % 2 ? 44100.0 : 96000.0, 256);
            std::vector<float> L(48000), R(48000);
            Transport tr; tr.playing = u(rng) < 0.7; tr.valid = u(rng) < 0.9; tr.bpm = 60 + u(rng) * 180; tr.ppq = u(rng) * 100;
            for (int off = 0; off < 48000; off += 300)
            {
                e.setTransport(tr);
                e.process(L.data() + off, R.data() + off, 300, nullptr, 0);
                tr.ppq += 300 * tr.bpm / 60.0 / 48000.0;
                if (u(rng) < 0.02) tr.ppq = u(rng) * 50;   // jumps
            }
            for (float x : L) { fin = fin && std::isfinite(x); pk = std::max(pk, std::fabs(x)); }
        }
        CHECK(fin && pk < 2.0f, "random settings, rules, patterns and song jumps: finite and bounded (peak %.2f)", pk);
    }
    printf("== cpu\n");
    {
        Settings s; s.mode = kModeFree; s.rate = 3; s.sens = 1.0; s.mix = 0.5; s.feedback = 0.6;
        Engine e; e.setSettings(s); e.prepare(kSr, 256);
        std::vector<float> L((size_t)kSr * 4), R(L.size());
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t off = 0; off < L.size(); off += 256) e.process(L.data() + off, R.data() + off, 256, nullptr, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  cpu: 8 tones, FX, 1/32 generations: %.1f %% of one core\n", 100.0 * secs / 4.0);
        float pk = 0; for (float x : L) pk = std::max(pk, std::fabs(x));
        CHECK(pk > 0.05f && pk < 1.2f, "the default kit, free running: peak %.2f", pk);
    }
    printf("== FX sends and feedback\n");
    {
        // a voice with its FX send at 0 sends nothing: the wet signal is the same as with that voice muted
        Settings a = quiet(); a.mix = 1.0; a.feedback = 0.3; a.time = 0.05;
        Settings b = a; b.tone[0].fxSend = 0.0;
        Engine e1; e1.prepare(kSr, 256); e1.setSettings(a);
        Engine e2; e2.prepare(kSr, 256); e2.setSettings(b);
        float L1[256], R1[256], L2[256], R2[256]; double late1 = 0, late2 = 0;
        for (int k = 0; k < 200; ++k)
        {
            synth::Midi m { 0, 0x90, 36, 120 };
            for (int i = 0; i < 256; ++i) L1[i] = R1[i] = L2[i] = R2[i] = 0.0f;
            e1.process(L1, R1, 256, k == 0 ? &m : nullptr, k == 0 ? 1 : 0); e2.process(L2, R2, 256, k == 0 ? &m : nullptr, k == 0 ? 1 : 0);
            if (k > 60) for (int i = 0; i < 256; ++i) { late1 += L1[i] * L1[i]; late2 += L2[i] * L2[i]; }
        }
        CHECK(late1 > 1e-4 && late2 < late1 * 1e-3, "FX send 0: no echoes (%.2g vs %.2g after the hit)", late2, late1);
        // FEEDBACK 100 %: the echoes build into a wash but stay bounded
        Settings c = quiet(); c.mix = 1.0; c.feedback = 1.0; c.time = 0.12; c.mode = kModeFree;
        Engine e3; e3.prepare(kSr, 256); e3.setSettings(c);
        float pk = 0; bool fin = true; double tail = 0;
        for (int k = 0; k < (int)(20 * kSr / 256); ++k)
        {
            synth::Midi m { 0, 0x90, 40, 120 };
            for (int i = 0; i < 256; ++i) L1[i] = R1[i] = 0.0f;
            e3.process(L1, R1, 256, k == 0 ? &m : nullptr, k == 0 ? 1 : 0);
            for (int i = 0; i < 256; ++i) { pk = std::max(pk, std::fabs(L1[i])); fin = fin && std::isfinite(L1[i]); if (k > (int)(15 * kSr / 256)) tail += L1[i] * L1[i]; }
        }
        CHECK(fin && pk < 1.5f && tail > 1e-3, "FEEDBACK 100 %%: still ringing after 15 s (a wash), peak %.2f, finite", pk);
    }
    std::printf(fails ? "test_life: %d FAILED\n" : "test_life: all passed\n", fails);
    return fails ? 1 : 0;
}
