// pump SING: lyrics parse into the expected syllables; a held note sings at its pitch; each note sings the next
// syllable and a rest is silent; vowels reach their formants; poly chords share a syllable, the choir doubles the
// voices; a legato note glides; random settings and MIDI stay finite. CPU.
#include "../plugins/sing/SingEngine.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

using namespace pump;
using namespace pump::sing;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::string show(const std::string& txt)
{
    std::string o;
    for (const auto& s : Lyrics::parse(txt))
    {
        if (!o.empty()) o += " | ";
        if (s.rest) { o += "."; continue; }
        if (s.extend) { o += "~"; continue; }
        for (int p : s.on) { o += kPhones[p].name; o += " "; }
        o += kPhones[s.nu].name;
        for (int p : s.co) { o += " "; o += kPhones[p].name; }
    }
    return o;
}
static std::vector<float> run(Engine& e, int n, std::vector<synth::Midi> ev)
{
    std::vector<float> L((size_t)n), R((size_t)n);
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        e.process(L.data() + off, R.data() + off, m, here.data(), (int)here.size());
    }
    return L;
}
// the period by autocorrelation over [a, b), between 60 and 1000 Hz
static double pitchOf(const std::vector<float>& x, size_t a, size_t b, double sr)
{
    const int lo = (int)(sr / 1000), hi = (int)(sr / 60);
    double best = -1e9; int lag = lo;
    for (int L = lo; L <= hi; ++L)
    {
        double c = 0, e1 = 0, e2 = 0;
        for (size_t i = a; i + (size_t)L < b; ++i) { c += (double)x[i] * x[i + (size_t)L]; e1 += (double)x[i] * x[i]; e2 += (double)x[i + (size_t)L] * x[i + (size_t)L]; }
        const double r = c / std::sqrt(e1 * e2 + 1e-20);
        if (r > best + 1e-9) { best = r; lag = L; }
        if (best > 0.9 && r < best - 0.2) break;   // the first strong peak
    }
    // parabolic refinement
    return sr / lag;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }

int main()
{
    printf("== lyrics\n");
    CHECK(show("hello world, I am a robot") == "HH AX | L OW | W ER L D | . | AY | AE M | AX | R OW | B AA T", "hello world, I am a robot -> %s", show("hello world, I am a robot").c_str());
    CHECK(show("[HH AX - L OW] la ~ shh") == "HH AX | L OW | L AA | ~ | SH", "phonemes, a hold and a vowelless word -> %s", show("[HH AX - L OW] la ~ shh").c_str());
    CHECK(show("singing") == "S IH NG | IH NG", "singing -> %s", show("singing").c_str());
    for (double sr : { 44100.0, 48000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)sr;
        {
            Engine e; Settings s; s.vib = 0; s.rough = 0; e.setSettings(s); e.prepare(sr, 512); e.setLyrics("ah");
            const auto L = run(e, n, { { 0, 0x90, 57, 110 } });
            const double f = pitchOf(L, (size_t)(0.4 * sr), (size_t)(0.6 * sr), sr);
            CHECK(std::fabs(f - 220.0) < 3.0, "A3 held on \"ah\": %.1f Hz", f);
        }
        {
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 512); e.setLyrics("la la, la");
            std::vector<synth::Midi> ev;
            for (int k = 0; k < 4; ++k) { ev.push_back({ (uint32_t)(k * 0.25 * sr), 0x90, 60, 100 }); ev.push_back({ (uint32_t)(k * 0.25 * sr + 0.18 * sr), 0x80, 60, 0 }); }
            const auto L = run(e, n, ev);
            const double sung = rms(L, (size_t)(0.05 * sr), (size_t)(0.17 * sr)), rest = rms(L, (size_t)(0.55 * sr), (size_t)(0.67 * sr)), after = rms(L, (size_t)(0.80 * sr), (size_t)(0.92 * sr));
            CHECK(e.syllableNow() == 3 && rest < 0.02 * sung && after > 0.3 * sung, "four notes on \"la la, la\": now at syllable %d, the third (a rest) silent (%.1f dB), the fourth sung", e.syllableNow(), 20 * std::log10(rest / sung + 1e-9));
        }
        {
            Engine a; Settings s; s.vib = 0; a.setSettings(s); a.prepare(sr, 512); a.setLyrics("[IY]");
            run(a, (int)(0.5 * sr), { { 0, 0x90, 57, 110 } });
            Engine b; b.setSettings(s); b.prepare(sr, 512); b.setLyrics("[AA]");
            run(b, (int)(0.5 * sr), { { 0, 0x90, 57, 110 } });
            CHECK(std::fabs(a.f1Now() - 270) < 15 && std::fabs(a.f2Now() - 2290) < 60 && std::fabs(b.f1Now() - 730) < 30 && std::fabs(b.f2Now() - 1090) < 40,
                  "vowels: IY at F1 %.0f F2 %.0f, AA at F1 %.0f F2 %.0f", a.f1Now(), a.f2Now(), b.f1Now(), b.f2Now());
        }
        {
            Engine e; Settings s; s.mode = kModePoly; e.setSettings(s); e.prepare(sr, 512); e.setLyrics("one two three");
            run(e, 4096, { { 0, 0x90, 60, 100 }, { 10, 0x90, 64, 100 }, { 20, 0x90, 67, 100 } });
            const int v1 = e.voicesNow(), s1 = e.syllableNow();
            Engine f; s.mode = kModeMono; s.choir = 6; f.setSettings(s); f.prepare(sr, 512); f.setLyrics("one");
            run(f, 4096, { { 0, 0x90, 60, 100 } });
            CHECK(v1 == 3 && s1 == 0 && f.voicesNow() == 6, "poly chord: %d voices on syllable %d; a 6-voice choir: %d voices", v1, s1, f.voicesNow());
        }
        {
            // legato: the second note (overlapping) slides up from the first
            Engine e; Settings s; s.vib = 0; s.glide = 120; e.setSettings(s); e.prepare(sr, 512); e.setLyrics("ah ~");
            const auto L = run(e, n, { { 0, 0x90, 57, 110 }, { (uint32_t)(0.5 * sr), 0x90, 69, 110 } });
            const double mid = pitchOf(L, (size_t)(0.5 * sr + 0.02 * sr), (size_t)(0.5 * sr + 0.06 * sr), sr), end = pitchOf(L, (size_t)(0.85 * sr), (size_t)(0.98 * sr), sr);
            CHECK(mid > 235 && mid < 420 && std::fabs(end - 440) < 6, "legato A3 -> A4: %.0f Hz on the way, %.0f Hz after", mid, end);
        }
        if (sr != 48000.0) continue;
        {
            std::mt19937 rng(51); std::uniform_real_distribution<double> u(0.0, 1.0);
            Engine e; Settings s; e.setSettings(s); e.prepare(sr, 512);
            e.setLyrics("the quick brown fox, jumps ~ over the lazy dog [S T R IY M] hmm shh! yes");
            bool finite = true; double peak = 0;
            std::vector<float> L(512), R(512);
            for (int it = 0; it < 2000; ++it)
            {
                if (it % 13 == 0)
                {
                    s.mode = u(rng) < 0.5 ? kModeMono : kModePoly; s.choir = 1 + (int)(u(rng) * 8); s.octave = (int)(u(rng) * 4) - 2; s.size = u(rng) * 2 - 1;
                    s.robot = u(rng); s.voice = u(rng); s.breath = u(rng); s.bright = u(rng); s.soft = u(rng); s.clarity = u(rng); s.rough = u(rng);
                    s.vib = u(rng); s.vibRate = 2 + 7 * u(rng); s.vibDelay = 1500 * u(rng); s.glide = 600 * u(rng) * u(rng); s.hiss = u(rng); s.diction = 0.5 + 1.5 * u(rng);
                    s.attack = 0.5 + 400 * u(rng) * u(rng); s.release = 1 + 3000 * u(rng) * u(rng); s.spread = u(rng); s.width = u(rng); s.drive = u(rng);
                    e.setSettings(s);
                }
                if (it % 500 == 250) e.setLyrics(it % 1000 == 250 ? "la la la" : "");
                std::vector<synth::Midi> ev;
                if (u(rng) < 0.3) ev.push_back({ (uint32_t)(u(rng) * 511), (uint8_t)(u(rng) < 0.6 ? 0x90 : 0x80), (uint8_t)(36 + u(rng) * 48), (uint8_t)(1 + u(rng) * 126) });
                if (u(rng) < 0.05) ev.push_back({ 511, 0xB0, 1, (uint8_t)(u(rng) * 127) });
                if (u(rng) < 0.05) ev.push_back({ 300, 0xE0, (uint8_t)(u(rng) * 127), (uint8_t)(u(rng) * 127) });
                std::sort(ev.begin(), ev.end(), [](const synth::Midi& a, const synth::Midi& b) { return a.offset < b.offset; });
                e.process(L.data(), R.data(), 512, ev.data(), (int)ev.size());
                for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite(L[(size_t)i]) && std::isfinite(R[(size_t)i]); peak = std::max(peak, (double)std::fabs(L[(size_t)i])); }
            }
            CHECK(finite && peak < 6.0, "random settings, lyrics and MIDI: finite (peak %.2f)", peak);
        }
        {
            Engine e; Settings s; s.mode = kModePoly; s.choir = 4; s.rough = 0.3; e.setSettings(s); e.prepare(sr, 512); e.setLyrics("aah");
            const auto t0 = std::chrono::steady_clock::now();
            run(e, n, { { 0, 0x90, 48, 100 }, { 0, 0x90, 55, 100 }, { 0, 0x90, 60, 100 }, { 0, 0x90, 64, 100 } });
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: a 4-note chord, 4 choir voices each (16 voices): %.1f %% of one core\n", 100.0 * secs);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
