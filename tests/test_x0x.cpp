// pump X0X: every instrument on every machine sounds, stays finite and ends; the pads' notes (C-4 up the white keys)
// and the General MIDI notes below play the right instruments, black keys between the pads stay silent; TUNE moves
// the bass drum's pitch by semitones; the sequencer's hits land on the song grid (SONG, swing), SONG stops with the
// host and FREE runs without one; accented steps are louder; the closed hat chokes the open one; the default kit and
// pattern sit near the suite's loudness; random settings stay finite. 44.1 and 48 kHz. CPU.
#include "../plugins/x0x/X0xEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pump;
using namespace pump::x0x;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static void render(Engine& e, std::vector<float>& L, std::vector<float>& R, const std::vector<synth::Midi>& ev, Transport tr = Transport(), double sr = 48000.0)
{
    R.assign(L.size(), 0.0f);
    const int n = (int)L.size();
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        e.setTransport(tr);
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
        tr.ppq += m * tr.bpm / 60.0 / sr;
    }
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; b = std::min(b, x.size()); for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }
static double peak(const std::vector<float>& x, size_t a, size_t b) { double p = 0; b = std::min(b, x.size()); for (size_t i = a; i < b; ++i) p = std::max(p, (double)std::fabs(x[i])); return p; }
static double db(double g) { return 20.0 * std::log10(g + 1e-12); }
static long onsetAfter(const std::vector<float>& x, size_t from) { for (size_t i = from; i < x.size(); ++i) if (std::fabs(x[i]) > 0.003f) return (long)i; return -1; }
static Settings quiet()   // the sequencer off, nothing panned
{
    Settings s; s.seqMode = kSeqOff;
    for (auto& in : s.in) in.pan = 0.0;
    return s;
}

int main()
{
    for (double sr : { 44100.0, 48000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)sr;
        // ---- every instrument on every machine
        {
            int silent = 0, endless = 0; bool finite = true; double hi = 0;
            for (int m = 0; m < kNumMachines; ++m)
                for (int in = 0; in < kInstr; ++in)
                {
                    Engine e; Settings s = quiet(); for (auto& x : s.in) x.machine = m; e.setSettings(s); e.prepare(sr, 512);
                    e.hit(in, 1.0, false);
                    std::vector<float> L((size_t)(4 * n)), R;
                    render(e, L, R, {}, Transport(), sr);
                    for (float v : L) finite = finite && std::isfinite(v);
                    const double pk = peak(L, 0, L.size());
                    hi = std::max(hi, pk);
                    if (pk < 0.01) { ++silent; printf("    silent: %s %s\n", kMachineNames[m], kInstrInfo[in].name); }
                    if (e.voicesNow() != 0) { ++endless; printf("    still ringing after 4 s: %s %s\n", kMachineNames[m], kInstrInfo[in].name); }
                }
            CHECK(finite && silent == 0 && endless == 0 && hi < 1.5, "55 instrument x machine hits: all sound, end, stay finite (loudest peak %.1f dBFS)", db(hi));
        }
        // ---- notes
        {
            Engine e; e.setSettings(quiet()); e.prepare(sr, 512);
            std::vector<float> L(4096), R;
            bool ok = true;
            for (int in = 0; in < kInstr; ++in)
            {
                const uint32_t before = e.lastHit();
                render(e, L, R, { { 0, 0x90, (uint8_t)noteForInstr(in), 100 } }, Transport(), sr);
                const uint32_t after = e.lastHit();
                ok = ok && after != before && (int)(after & 0xFF) == in;
            }
            const uint32_t b0 = e.lastHit();
            render(e, L, R, { { 0, 0x90, 49, 100 }, { 1, 0x90, 51, 100 } }, Transport(), sr);
            const bool blackSilent = e.lastHit() == b0;
            render(e, L, R, { { 0, 0x90, 36, 100 } }, Transport(), sr);
            const bool gm = (e.lastHit() & 0xFF) == kBD;
            render(e, L, R, { { 0, 0x90, 42, 100 } }, Transport(), sr);
            const bool gmHat = (e.lastHit() & 0xFF) == kCH;
            CHECK(ok && blackSilent && gm && gmHat, "pads on C-4 D-4 E-4 ... F-5 (48 50 52 ... 65), black keys between them silent, GM 36 = bass drum, 42 = closed hat");
        }
        // ---- TUNE: the bass drum's pitch in semitones (the tail's period)
        {
            auto pitchOf = [&](double tune) {
                Engine e; Settings s = quiet(); s.in[kBD].tune = tune; s.in[kBD].decay = 1.0; s.in[kBD].snap = 0; e.setSettings(s); e.prepare(sr, 512);
                e.hit(kBD, 1.0, false);
                std::vector<float> L((size_t)n), R;
                render(e, L, R, {}, Transport(), sr);
                double first = -1, last = -1; int c = 0;
                for (size_t i = (size_t)(0.2 * sr); i < (size_t)(0.6 * sr); ++i)
                    if (L[i - 1] < 0 && L[i] >= 0) { const double t = (double)(i - 1) + L[i - 1] / (double)(L[i - 1] - L[i]); if (first < 0) first = t; last = t; ++c; }
                return c > 1 ? (c - 1) * sr / (last - first) : 0.0;
            };
            const double f0 = pitchOf(0), f12 = pitchOf(12), f5 = pitchOf(-5);
            CHECK(std::fabs(f0 - 49.0) < 1.0 && std::fabs(f12 / f0 - 2.0) < 0.02 && std::fabs(f5 / f0 - std::exp2(-5.0 / 12.0)) < 0.02,
                  "bass drum: %.1f Hz, an octave up %.1f Hz, -5 st %.1f Hz", f0, f12, f5);
        }
        // ---- the sequencer on the grid
        const double step = sr * 0.125;   // a 16th at 120 BPM
        {
            Engine e; Settings s = quiet(); s.seqMode = kSeqSong; s.pattern = makePattern({ { kRS, "x...x...x...x..." } });
            e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.playing = true; t.valid = true; t.bpm = 120.0;
            std::vector<float> L((size_t)(2 * n)), R;
            render(e, L, R, {}, t, sr);
            long worst = 0;
            for (int k = 0; k < 4; ++k)
            {
                const size_t at = (size_t)std::lround(k * 4 * step);
                worst = std::max(worst, std::labs(onsetAfter(L, at > 100 ? at - 100 : 0) - (long)at));
            }
            CHECK(worst < 8, "SONG: hits on the grid (worst %ld samples off)", worst);
            Transport stop = t; stop.playing = false;
            Engine e2; e2.setSettings(s); e2.prepare(sr, 512);
            std::vector<float> A((size_t)n), B((size_t)n);
            render(e2, A, R, {}, stop, sr);
            Settings f = s; f.seqMode = kSeqFree; e2.setSettings(f);
            render(e2, B, R, {}, stop, sr);
            CHECK(peak(A, 0, A.size()) < 1e-4 && peak(B, 0, B.size()) > 0.05, "SONG waits for the host; FREE plays without it");
        }
        {
            Engine e; Settings s = quiet(); s.seqMode = kSeqSong; s.swing = 1.0 / 3.0; s.pattern = makePattern({ { kRS, ".x.............." } });
            e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.playing = true; t.valid = true; t.bpm = 120.0;
            std::vector<float> L((size_t)n), R;
            render(e, L, R, {}, t, sr);
            const double at = onsetAfter(L, 0) / step;
            CHECK(std::fabs(at - 4.0 / 3.0) < 0.01, "SWING 33 %%: step 2 at %.3f steps", at);
        }
        // ---- accent
        {
            Engine e; Settings s = quiet(); s.seqMode = kSeqSong; s.accent = 1.0; s.pattern = makePattern({ { kSD, "X...x..........." } });
            e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.playing = true; t.valid = true; t.bpm = 120.0;
            std::vector<float> L((size_t)n), R;
            render(e, L, R, {}, t, sr);
            const double a = peak(L, 0, (size_t)(2 * step)), b = peak(L, (size_t)(4 * step), (size_t)(6 * step));
            CHECK(a > 1.6 * b, "accented snare %.1f dB, plain %.1f dB", db(a), db(b));
        }
        // ---- the closed hat chokes the open one
        {
            auto tail = [&](bool choke) {
                Engine e; Settings s = quiet(); s.in[kOH].decay = 1.0; e.setSettings(s); e.prepare(sr, 512);
                e.hit(kOH, 1.0, false);
                std::vector<float> L((size_t)n), R;
                std::vector<synth::Midi> ev; if (choke) ev.push_back({ (uint32_t)(0.05 * sr), 0x90, (uint8_t)noteForInstr(kCH), 1 });
                render(e, L, R, ev, Transport(), sr);
                return rms(L, (size_t)(0.25 * sr), (size_t)(0.5 * sr));
            };
            const double open = tail(false), choked = tail(true);
            CHECK(choked < 0.05 * open, "closed hat chokes the open one (tail %.1f dB -> %.1f dB)", db(open), db(choked));
        }
        // ---- loudness: the default kit and pattern
        {
            Engine e; Settings s; s.seqMode = kSeqFree; e.setSettings(s); e.prepare(sr, 512);
            Transport t; t.bpm = 124.0;
            std::vector<float> L((size_t)(4 * n)), R;
            render(e, L, R, {}, t, sr);
            const double r = db(rms(L, (size_t)n, L.size()));
            CHECK(r > -24 && r < -15, "default kit and pattern: %.1f dBFS RMS", r);
        }
        // ---- random settings
        {
            std::mt19937 rg(11); std::uniform_real_distribution<double> u(0, 1);
            bool finite = true; double hi = 0;
            for (int trial = 0; trial < 40; ++trial)
            {
                Engine e; Settings s;
                for (auto& in : s.in) { in.machine = (int)(u(rg) * kNumMachines); in.tune = -12 + 24 * u(rg); in.decay = u(rg); in.tone = u(rg); in.snap = u(rg); in.levelDb = -20 + 26 * u(rg); in.pan = 2 * u(rg) - 1; }
                s.accent = u(rg); s.drive = u(rg); s.swing = 0.75 * u(rg); s.rate = (int)(u(rg) * kNumRates); s.length = 1 + (int)(u(rg) * kSteps); s.seqMode = kSeqFree;
                for (auto& r : s.pattern.rows) r = (uint32_t)(u(rg) * 65536);
                e.setSettings(s); e.prepare(sr, 512);
                std::vector<synth::Midi> ev;
                for (int k = 0; k < 30; ++k) ev.push_back({ (uint32_t)(u(rg) * n), 0x90, (uint8_t)(30 + u(rg) * 40), (uint8_t)(1 + u(rg) * 126) });
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                Transport t; t.bpm = 60 + 150 * u(rg);
                std::vector<float> L((size_t)n), R;
                render(e, L, R, ev, t, sr);
                for (float v : L) finite = finite && std::isfinite(v);
                for (float v : R) finite = finite && std::isfinite(v);
                hi = std::max(hi, peak(L, 0, L.size()));
            }
            CHECK(finite && hi < 6.0, "random kits, patterns and MIDI: finite, peak %.2f", hi);
        }
    }
    // ---- the pattern text
    {
        const Pattern p = makePattern({ { kBD, "x...X..." }, { kCH, "..x." } }, "x");
        CHECK(p.rows[kBD] == 0x11u && p.rows[kCH] == 0x4u && p.rows[kAccentRow] == 0x11u, "pattern text: hits, 'X' accents, the accent row");
    }
    // ---- CPU: every instrument on every step
    {
        Engine e; Settings s; s.seqMode = kSeqFree; for (int r = 0; r < kInstr; ++r) s.pattern.rows[r] = 0xFFFF; e.setSettings(s); e.prepare(48000, 512);
        Transport t; t.bpm = 140; e.setTransport(t);
        std::vector<float> L(48000 * 10), R(L.size());
        const auto a = std::chrono::steady_clock::now();
        for (size_t o = 0; o < L.size(); o += 256) e.process(L.data() + o, R.data() + o, 256, nullptr, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        printf("  CPU: %.2f %% of one core (all eleven on every 16th at 140 BPM)\n", secs * 10.0);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
