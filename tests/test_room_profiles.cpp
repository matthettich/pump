// Room Profiles (Windows): a profile becomes filters at every common rate and a config that picks the right one; the
// filters apply the profile's correction, delay and preamp; config.txt gets its include line once.
#include "../system/windows/ApoProfile.hpp"
#include <complex>
#include <cstdio>
#include <cstring>

using namespace pump;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double levelAt(const std::vector<float>& h, double fs, double hz)
{
    std::complex<double> s = 0;
    for (size_t i = 0; i < h.size(); ++i) s += (double)h[i] * std::polar(1.0, -2.0 * kPi * hz * i / fs);
    return 20.0 * std::log10(std::abs(s));
}

static std::vector<float> readChannel(const std::vector<uint8_t>& wav, int ch, int* fs)
{
    uint32_t rate, bytes; uint16_t nc;
    std::memcpy(&nc, &wav[22], 2); std::memcpy(&rate, &wav[24], 4); std::memcpy(&bytes, &wav[40], 4);
    *fs = (int)rate;
    std::vector<float> out(bytes / 4 / nc);
    for (size_t i = 0; i < out.size(); ++i) std::memcpy(&out[i], &wav[44 + (i * nc + ch) * 4], 4);
    return out;
}

int main()
{
    printf("room profiles (windows)\n");
    room::Profile p;
    p.name = "Studio A: NS10 / Hafler";
    p.preampDb = -4.0;
    const std::vector<double> g = room::correctionGrid();
    for (int c = 0; c < 2; ++c)
    {
        room::ProfileChannel& ch = p.ch[p.channels++];
        ch.side = c ? 'R' : 'L';
        ch.delayMs = c ? 0.5 : 0.0;
        ch.f = g;
        for (double f : g) ch.correction.push_back(c ? 4.0 * std::exp(-std::pow(std::log2(f / 100.0), 2) * 4) : -6.0 * std::exp(-std::pow(std::log2(f / 1000.0), 2) * 4));
        ch.measured.assign(g.size(), 0.0); ch.target.assign(g.size(), 0.0); ch.groupDelayMs.assign(g.size(), 0.0);
    }
    const std::string folder = apo::folderName(p.name);
    CHECK(folder == "Studio A NS10 Hafler", "folder name \"%s\"", folder.c_str());
    const apo::ApoFiles out = apo::buildApoFiles(p, folder);
    CHECK(out.files.size() == 5, "%zu filters, one per rate", out.files.size());
    CHECK(out.active.find("If: sampleRate == 44100\r\nConvolution: pump-room\\Studio A NS10 Hafler\\44100.wav") != std::string::npos &&
          out.active.find("ElseIf: sampleRate == 192000") != std::string::npos && out.active.find("EndIf:\r\nChannel: all") != std::string::npos,
          "the config picks the filter for the rate");
    int fs = 0;
    const std::vector<float> L = readChannel(out.files[1].second, 0, &fs), R = readChannel(out.files[1].second, 1, &fs);
    CHECK(fs == 48000 && L.size() == R.size() && L.size() > 16000, "48 kHz, stereo, %zu taps", L.size());
    const double l1k = levelAt(L, fs, 1000), l5k = levelAt(L, fs, 5000), r100 = levelAt(R, fs, 100), r5k = levelAt(R, fs, 5000);
    CHECK(std::fabs(l1k - (-10.0)) < 0.3 && std::fabs(l5k - (-4.0)) < 0.3, "left: -6 dB at 1 kHz with the -4 dB preamp: %.2f dB (%.2f at 5 kHz)", l1k, l5k);
    CHECK(std::fabs(r100 - 0.0) < 0.3 && std::fabs(r5k - (-4.0)) < 0.3, "right: +4 dB at 100 Hz with the preamp: %.2f dB (%.2f at 5 kHz)", r100, r5k);
    size_t pl = 0, pr = 0;
    for (size_t i = 0; i < L.size(); ++i) { if (std::fabs(L[i]) > std::fabs(L[pl])) pl = i; if (std::fabs(R[i]) > std::fabs(R[pr])) pr = i; }
    CHECK(pr >= pl + 20 && pr <= pl + 30, "right delayed 0.5 ms: its peak %zu samples after the left's", pr - pl);

    // one channel: used on both sides
    room::Profile one = p; one.channels = 1; one.headphones = true;
    const apo::ApoFiles o = apo::buildApoFiles(one, "x");
    const std::vector<float> oL = readChannel(o.files[0].second, 0, &fs), oR = readChannel(o.files[0].second, 1, &fs);
    CHECK(oL == oR && o.active.find("(headphones)") != std::string::npos, "a one-channel profile on both sides; headphones named");

    const apo::ApoFiles held = apo::buildApoFiles(p, folder, -3.5);
    CHECK(held.active.find("Preamp: -3.50 dB\r\nChannel: L R") != std::string::npos && out.active.find("Preamp") == std::string::npos,
          "the level hold's make-up comes before the filters (none when 0)");
    CHECK(apo::emptyActive(-6.25).find("Preamp: -6.25 dB") != std::string::npos && apo::emptyActive().find("Preamp") == std::string::npos &&
          apo::quitActive().find("Preamp") == std::string::npos, "no profile keeps the held level; quitting doesn't");
    bool changed = false;
    const std::string cfg = "Preamp: -3 dB\r\nInclude: example.txt";
    const std::string c1 = apo::withInclude(cfg, &changed);
    CHECK(changed && c1.find("example.txt\r\n# pump ROOM") != std::string::npos && c1.rfind(apo::kIncludeLine) != std::string::npos, "the include line added");
    bool again = true;
    CHECK(apo::withInclude(c1, &again) == c1 && !again, "and only once");
    CHECK(apo::withInclude("  Include: pump-room-active.txt  \n", &again) == "  Include: pump-room-active.txt  \n" && !again, "already there (spaces around it)");
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
