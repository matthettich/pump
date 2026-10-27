// pump SING voicebanks: a little bank made here (three syllables, its oto.ini in Shift-JIS with kana aliases, one in a
// subfolder in UTF-8) is read whole; Shift-JIS kana come out right; SING's syllables find their aliases (kana, "- ka",
// romaji, a vowel); a voice sings an entry at the note's pitch while keeping the recording's formants (PSOLA, not a
// resample: a resample would move them with the pitch); it holds as long as the note and ends after note-off; GENDER
// moves the formants and not the pitch.
#include "dsp/Synth.hpp"
#include "../plugins/sing/SingEngine.hpp"
#include <cstdio>
#include <vector>

using namespace pump;
using namespace pump::sing;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static void writeWav(const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + (uint32_t)x.size() * 2); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32((uint32_t)sr); u32((uint32_t)sr * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32((uint32_t)x.size() * 2);
    for (float v : x) { const int16_t s = (int16_t)std::lround(clampv(v, -1.0f, 1.0f) * 32000.0f); std::fwrite(&s, 2, 1, f); }
    std::fclose(f);
}
// a sung vowel: a 200 Hz glottal-ish pulse train through two resonators (F1, F2), with a noise burst in front
static std::vector<float> syllable(double f1, double f2, double burst, int sr, double secs)
{
    std::vector<float> y((size_t)(secs * sr));
    double ph = 0.0, a1 = 0, b1 = 0, a2 = 0, b2 = 0;
    uint32_t r = 1u;
    auto res = [&](double x, double f, double bw, double& s1, double& s2) {
        const double R = std::exp(-kPi * bw / sr), c = 2.0 * R * std::cos(kTwoPi * f / sr);
        const double o = x + c * s1 - R * R * s2; s2 = s1; s1 = o; return o * (1.0 - R);
    };
    for (size_t i = 0; i < y.size(); ++i)
    {
        const double t = (double)i / sr;
        double x = 0.0;
        if (t < burst) { r = r * 1664525u + 1013904223u; x = ((r >> 8) / 8388608.0 - 1.0) * 0.4; }
        else { ph += 200.0 / sr; if (ph >= 1.0) { ph -= 1.0; x = 1.0; } }
        const double v = res(x, f1, 80.0, a1, b1) + 0.7 * res(x, f2, 100.0, a2, b2);
        y[i] = (float)(v * 3.0 * std::min(1.0, t * 50.0) * std::min(1.0, (secs - t) * 20.0));
    }
    return y;
}
static double harm(const std::vector<float>& x, size_t a, size_t n, double f, double sr)
{
    double re = 0, im = 0;
    for (size_t i = a; i < a + n; ++i) { const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i - a) / n); re += x[i] * w * std::cos(kTwoPi * f * i / sr); im += x[i] * w * std::sin(kTwoPi * f * i / sr); }
    return 2.0 * std::sqrt(re * re + im * im) / (0.5 * n);
}
static Syllable syl(const char* word) { auto v = Lyrics::parse(word); return v.empty() ? Syllable() : v[0]; }

int main()
{
    const std::string dir = "build/tests/bank";
    std::system(("mkdir -p " + dir + "/sub").c_str());
    const int fsr = 44100;
    writeWav(dir + "/a.wav", syllable(800, 1200, 0.0, fsr, 1.0), fsr);
    writeWav(dir + "/ka.wav", syllable(800, 1200, 0.06, fsr, 1.0), fsr);
    writeWav(dir + "/sub/i.wav", syllable(300, 2300, 0.0, fsr, 1.0), fsr);
    {
        // Shift-JIS: a = 0x82A0, ka = 0x82A9
        FILE* f = std::fopen((dir + "/oto.ini").c_str(), "wb");
        const unsigned char l1[] = "a.wav=\x82\xA0,50,30,-800,20,10\r\n";
        const unsigned char l2[] = "ka.wav=- \x82\xA9,40,90,-800,80,20\r\n";
        std::fwrite(l1, 1, sizeof(l1) - 1, f); std::fwrite(l2, 1, sizeof(l2) - 1, f); std::fclose(f);
        f = std::fopen((dir + "/sub/oto.ini").c_str(), "wb");
        std::fputs("i.wav=i,50,30,-800,20,10\n", f); std::fclose(f);
        f = std::fopen((dir + "/character.txt").c_str(), "wb"); std::fputs("name=Test singer\n", f); std::fclose(f);
    }
    printf("== reading the bank\n");
    const auto B = Bank::load(dir + "/oto.ini");
    CHECK(B->entries.size() == 3 && B->name == "Test singer", "three entries from two oto.ini files, the singer named (%zu, \"%s\")", B->entries.size(), B->name.c_str());
    CHECK(B->find("\xE3\x81\x82") && B->find("- \xE3\x81\x8B") && B->find("i"), "Shift-JIS kana read as kana (\"\xE3\x81\x82\", \"- \xE3\x81\x8B\"), a UTF-8 romaji alias as it is");
    CHECK(toHiragana("\xE3\x82\xAB") == "\xE3\x81\x8B" && romajiToHiragana("kya") == "\xE3\x81\x8D\xE3\x82\x83", "katakana to hiragana, romaji to kana (kya)");
    printf("== finding syllables\n");
    {
        std::string t;
        const Entry* e1 = B->lookup(syl("ka"), 0, &t);
        CHECK(e1 && e1->alias == "- \xE3\x81\x8B", "\"ka\" at a phrase start finds \"- \xE3\x81\x8B\" (tried %s)", t.c_str());
        const Entry* e2 = B->lookup(syl("ah"), 0, &t);
        CHECK(e2 && e2->alias == "\xE3\x81\x82", "\"ah\" finds \"\xE3\x81\x82\" (as %s)", t.c_str());
        const Entry* e3 = B->lookup(syl("tea"), 'a', &t);
        CHECK(e3 && e3->alias == "i", "\"tea\" (no ti / chi in the bank) falls back to its vowel \"i\" (as %s)", t.c_str());
        char vw; CHECK(syllableRomaji(syl("she"), vw) == "shi" && syllableRomaji(syl("two"), vw) == "tsu" && syllableRomaji(syl("who"), vw) == "fu", "Japanese spellings: she -> shi, two -> tsu, who -> fu");
    }
    printf("== singing\n");
    const double sr = 48000.0;
    auto sing = [&](const Entry* e, int note, double gender, int n, int offAt) {
        BankVoice v; v.prepare(sr); v.on(B.get(), e, synth::midiToHz(note));
        std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
        for (int i = 0; i < n; i += 256)
        {
            if (offAt >= 0 && i >= offAt && v.held) v.off();
            v.render(L.data() + i, R.data() + i, std::min(256, n - i), 0.0, 0.0, gender, 0.005, 0.1);
        }
        return std::make_pair(L, v.active);
    };
    {
        const Entry* a = B->find("\xE3\x81\x82");
        const auto r = sing(a, 69, 0.0, (int)(2.0 * sr), -1);   // A4: 440 Hz, the recording was 200 Hz
        const auto& y = r.first;
        double best = 0, bf = 0; for (double f = 400; f < 480; f += 0.5) { const double h = harm(y, (size_t)(0.5 * sr), 32768, f, sr); if (h > best) { best = h; bf = f; } }
        CHECK(std::fabs(bf - 440.0) < 3.0, "sings at %.1f Hz (440; the recording is at 200)", bf);
        // the formants stay: the harmonic nearest F1 (800: harmonic 2 = 880) stronger than those far above (1760, 2640);
        // a plain resample (x2.2) would have moved F1 to 1760
        const double h2 = harm(y, (size_t)(0.5 * sr), 32768, 880, sr), h4 = harm(y, (size_t)(0.5 * sr), 32768, 1760, sr), h6 = harm(y, (size_t)(0.5 * sr), 32768, 2640, sr);
        CHECK(h2 > 2.0 * h4 && h2 > 2.0 * h6, "the formants stay where they were: 880 Hz %.3f, 1760 Hz %.3f, 2640 Hz %.3f", h2, h4, h6);
        double e = 0; for (size_t i = (size_t)(1.8 * sr); i < (size_t)(1.9 * sr); ++i) e += (double)y[i] * y[i];
        CHECK(std::sqrt(e / (0.1 * sr)) > 0.01 && r.second, "held for two seconds (the recording is one): still singing at 1.8 s");
        const auto q = sing(a, 69, 0.0, (int)(1.0 * sr), (int)(0.3 * sr));
        CHECK(!q.second, "after note-off it plays out and stops");
        // gender: the pitch stays, the formant moves up
        const auto g = sing(a, 57, 1.0, (int)(1.5 * sr), -1);
        double gb = 0, gf = 0; for (double f = 200; f < 240; f += 0.5) { const double h = harm(g.first, (size_t)(0.5 * sr), 32768, f, sr); if (h > gb) { gb = h; gf = f; } }
        const auto g0 = sing(a, 57, 0.0, (int)(1.5 * sr), -1);
        auto centroid = [&](const std::vector<float>& z) { double nn = 0, dd = 0; for (int k = 1; k <= 20; ++k) { const double h = harm(z, (size_t)(0.5 * sr), 16384, 220.0 * k, sr); nn += k * h * h; dd += h * h; } return nn / std::max(1e-20, dd); };
        CHECK(std::fabs(gf - 220.0) < 3.0 && centroid(g.first) > 1.2 * centroid(g0.first), "GENDER up: still %.1f Hz, the spectrum's centre from harmonic %.2f to %.2f", gf, centroid(g0.first), centroid(g.first));
    }
    printf("== in SING\n");
    {
        Engine e; e.prepare(sr, 256);
        Settings st; st.engine = kEngineBank; st.vib = 0.0; e.setSettings(st);
        e.setLyrics("ka ah zo"); e.setBank(B.get());
        std::vector<float> L(256), R(256);
        int sung[3] = { -2, -2, -2 }; double lv[3] = { 0, 0, 0 };
        for (int k = 0; k < 3; ++k)
        {
            synth::Midi on { 0, 0x90, 60, 100 }, off { 0, 0x80, 60, 0 };
            for (int b = 0; b < 120; ++b)
            {
                e.process(L.data(), R.data(), 256, b == 0 ? &on : (b == 100 ? &off : nullptr), b == 0 || b == 100);
                if (b > 20 && b < 90) for (int i = 0; i < 256; ++i) lv[k] = std::max(lv[k], (double)std::fabs(L[(size_t)i]));
                if (b == 30) sung[k] = e.entryNow();
            }
        }
        const bool k0 = sung[0] >= 0 && B->entries[(size_t)sung[0]].alias == "- \xE3\x81\x8B", k1 = sung[1] >= 0 && B->entries[(size_t)sung[1]].alias == "\xE3\x81\x82";
        CHECK(k0 && k1 && sung[2] == -1 && lv[0] > 0.01 && lv[1] > 0.01 && lv[2] > 0.01, "lyrics \"ka ah zo\": the bank sings \"- \xE3\x81\x8B\" and \"\xE3\x81\x82\", the formant voice sings \"zo\" (the bank hasn't it); all three sound");
        // random play with the bank: finite
        bool finite = true;
        st.mode = kModePoly; st.choir = 3; e.setSettings(st);
        uint32_t r = 3u;
        for (int b = 0; b < 600; ++b)
        {
            r = r * 1664525u + 1013904223u;
            synth::Midi ev { 0, (uint8_t)((r >> 30) & 1 ? 0x90 : 0x80), (uint8_t)(40 + (r >> 8) % 40), 100 };
            e.process(L.data(), R.data(), 256, &ev, 1);
            for (int i = 0; i < 256; ++i) finite = finite && std::isfinite(L[(size_t)i]) && std::fabs(L[(size_t)i]) < 20.0f;
        }
        CHECK(finite, "chords and a choir on the bank stay finite");
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
