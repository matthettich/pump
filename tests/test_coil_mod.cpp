// pump COIL modulation (CoilDsp + common/dsp/Mod.hpp): with no connection the sound is exactly the unmodulated one;
// an LFO on TENSION changes the sound over time; LFO shapes stay in range and DRIFT has no jumps; velocity per note
// gives each note its own decay; the mod wheel moves its target; a removed connection puts everything back; two
// sources on one target add up; LFO 2 can move LFO 1's rate; sync locks to the song. CPU with connections.
#include "CoilDsp.hpp"
#include <chrono>
#include <cstdio>
#include <vector>

using namespace pump;
using namespace pump::coil;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

// a stand-in for the plugin: parameter values and the rate
struct TestBase {
    float values[kNumParams];
    double sr = 48000.0;
    TestBase() { for (int i = 0; i < kNumParams; ++i) values[i] = params()[(size_t)i].def; }
    float pv(uint32_t i) const { return values[i]; }
    double getSampleRate() const { return sr; }
    uint32_t getBufferSize() const { return 256; }
};
using Dsp = CoilDsp<TestBase>;

static std::vector<float> render(Dsp& d, double seconds, std::vector<synth::Midi> ev)
{
    const int n = (int)(seconds * 48000.0);
    std::vector<float> out((size_t)n);
    float L[256], R[256];
    for (int off = 0; off < n; off += 256)
    {
        const int m = std::min(256, n - off);
        std::vector<synth::Midi> here;
        for (auto x : ev) if ((int)x.offset >= off && (int)x.offset < off + m) { x.offset -= (uint32_t)off; here.push_back(x); }
        d.renderChunk(L, R, m, here.data(), (int)here.size());
        for (int i = 0; i < m; ++i) out[(size_t)(off + i)] = 0.5f * (L[i] + R[i]);
    }
    return out;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double)x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }
static double diff(const std::vector<float>& a, const std::vector<float>& b) { double m = 0; for (size_t i = 0; i < a.size(); ++i) m = std::max(m, (double)std::fabs(a[i] - b[i])); return m; }
static void connect(Dsp& d, int slot, int src, int target, float amt)
{
    d.values[slotParam(slot, kSlotSrc)] = (float)src; d.values[slotParam(slot, kSlotDst)] = (float)target; d.values[slotParam(slot, kSlotAmt)] = amt;
}

int main()
{
    const std::vector<synth::Midi> chord = { { 0, 0x90, 48, 110 }, { 0, 0x90, 55, 110 }, { 0, 0x90, 64, 110 } };
    printf("== no connections\n");
    {
        Dsp a; a.prepareDsp();
        Dsp b; for (int k = 0; k < mod::kSlots; ++k) b.values[slotParam(k, kSlotDst)] = 3.0f;   // targets set, sources none
        b.values[lfoParam(0, kLfoRate)] = 7.0f; b.prepareDsp();
        const auto x = render(a, 2.0, chord), y = render(b, 2.0, chord);
        CHECK(diff(x, y) == 0.0, "targets and LFO settings without a source change nothing: max difference %g", diff(x, y));
        CHECK(rms(x, 0, x.size()) > 1e-3, "and it sounds (%.4f rms)", rms(x, 0, x.size()));
    }
    printf("== LFO 1 on TENSION\n");
    {
        Dsp a; a.prepareDsp();
        Dsp b; connect(b, 0, mod::kSrcLfo1, targetOfParam(kTension), 80.0f); b.values[lfoParam(0, kLfoRate)] = 4.0f; b.prepareDsp();
        const auto x = render(a, 2.0, chord), y = render(b, 2.0, chord);
        double finite = 1; for (float v : y) if (!std::isfinite(v)) finite = 0;
        CHECK(finite && diff(x, y) > 1e-3, "the sound changes (max difference %.3f), stays finite", diff(x, y));
    }
    printf("== shapes\n");
    {
        bool inRange = true; double jump = 0.0;
        for (int sh = 0; sh < mod::kNumShapes; ++sh)
        {
            mod::Lfo l; l.seed(77);
            double prev = l.value(sh);
            for (int i = 0; i < 20000; ++i)
            {
                l.advance(0.001);
                const double v = l.value(sh);
                if (v < -1.0001 || v > 1.0001 || !std::isfinite(v)) inRange = false;
                if (sh == mod::kDrift) jump = std::max(jump, std::fabs(v - prev));
                prev = v;
            }
        }
        CHECK(inRange, "every shape stays in -1..1");
        CHECK(jump < 0.02, "DRIFT moves smoothly (largest step %.4f at 1/1000 cycle a step)", jump);
    }
    printf("== velocity, per note\n");
    {
        // VELOCITY -> DECAY: a soft note dies sooner than a hard one (velocity to kick off, so both are struck alike)
        auto tail = [&](uint8_t vel, bool wired) {
            Dsp d; d.values[kVelocity] = 0.0f; d.values[kDecay] = 1.0f;
            if (wired) connect(d, 3, mod::kSrcVelocity, targetOfParam(kDecay), 60.0f);
            d.prepareDsp();
            const auto x = render(d, 3.0, { { 0, 0x90, 52, vel } });
            return rms(x, (size_t)(2.0 * 48000), (size_t)(3.0 * 48000)) / rms(x, 0, 4800);
        };
        const double hard = tail(127, true), soft = tail(20, true), plain = tail(127, false);
        CHECK(hard > soft * 1.5, "hard note's tail %.2g, soft note's %.2g (relative to their starts)", hard, soft);
        CHECK(hard > plain, "and VELOCITY lengthens the hard one past the plain decay (%.2g vs %.2g)", hard, plain);
    }
    printf("== mod wheel, remove, two sources\n");
    {
        Dsp a; a.values[kVolume] = 0.0f; a.prepareDsp();
        Dsp b; b.values[kVolume] = 0.0f; connect(b, 5, mod::kSrcWheel, targetOfParam(kVolume), -50.0f); b.prepareDsp();
        std::vector<synth::Midi> ev = chord; ev.insert(ev.begin(), { 0, 0xB0, 1, 127 });
        const auto x = render(a, 1.0, ev), y = render(b, 1.0, ev);
        CHECK(rms(y, 24000, 48000) < 0.25 * rms(x, 24000, 48000), "wheel up, VOLUME -50 %%: %.4f vs %.4f rms", rms(y, 24000, 48000), rms(x, 24000, 48000));
        // the connection removed (as the window does: source back to none, settings re-read)
        b.values[slotParam(5, kSlotSrc)] = 0.0f; b.syncSettings();
        render(b, 8.0, { { 0, 0x80, 48, 0 }, { 0, 0x80, 55, 0 }, { 0, 0x80, 64, 0 } });   // the old notes die away
        Dsp c; c.values[kVolume] = 0.0f; c.prepareDsp();   // the same history, never connected
        render(c, 1.0, ev); render(c, 8.0, { { 0, 0x80, 48, 0 }, { 0, 0x80, 55, 0 }, { 0, 0x80, 64, 0 } });
        const auto y2 = render(b, 1.0, chord), x2 = render(c, 1.0, chord);
        CHECK(diff(y2, x2) < 1e-6, "removed: exactly as if it had never been connected (max difference %g)", diff(y2, x2));
        // two connections on one target add up
        Dsp d; d.values[kVolume] = 0.0f; connect(d, 0, mod::kSrcWheel, targetOfParam(kVolume), -25.0f); connect(d, 1, mod::kSrcWheel, targetOfParam(kVolume), -25.0f); d.prepareDsp();
        const auto z = render(d, 1.0, ev);
        CHECK(std::fabs(rms(z, 24000, 48000) / rms(y, 24000, 48000) - 1.0) < 0.15, "two -25 %% connections act like one -50 %% (%.4f vs %.4f)", rms(z, 24000, 48000), rms(y, 24000, 48000));
    }
    printf("== LFO 2 moves LFO 1's rate; sync\n");
    {
        Dsp d; connect(d, 0, mod::kSrcLfo1, targetOfParam(kTone), 50.0f); connect(d, 1, mod::kSrcLfo2, kNumTargets - 2, 100.0f);
        d.values[lfoParam(1, kLfoShape)] = (float)mod::kSquare; d.values[lfoParam(1, kLfoRate)] = 0.5f; d.prepareDsp();
        // over one LFO 2 cycle LFO 1 runs fast for half and slow for half: count LFO 1's cycles in each half
        int fast = 0, slow = 0; float prev = 0.0f, L[256], R[256];
        for (int b = 0; b < (int)(2.0 * 48000 / 256); ++b)
        {
            d.renderChunk(L, R, 256, nullptr, 0);
            const float ph = d.lfoPhaseNow[0].load();
            if (ph < prev) (b < (int)(48000 / 256) ? fast : slow)++;
            prev = ph;
        }
        CHECK(fast > 2 * std::max(1, slow) || slow > 2 * std::max(1, fast), "LFO 1 cycles: %d in the first second, %d in the next", fast, slow);
        Dsp s; connect(s, 0, mod::kSrcLfo1, targetOfParam(kTone), 50.0f);
        s.values[lfoParam(0, kLfoSync)] = 1.0f; s.values[lfoParam(0, kLfoDiv)] = 11.0f;   // 1/4
        s.prepareDsp();
        s.hostTime.valid = true; s.hostTime.playing = true; s.hostTime.bpm = 120.0; s.hostTime.ppq = 3.25;
        s.renderChunk(L, R, 256, nullptr, 0);
        CHECK(std::fabs(s.lfoPhaseNow[0].load() - 0.25f) < 1e-4, "synced to 1/4 at beat 3.25: phase %.4f", s.lfoPhaseNow[0].load());
    }
    printf("== cpu\n");
    {
        auto cost = [&](bool wired) {
            Dsp d; d.values[kModel] = 1.0f;
            if (wired) { connect(d, 0, mod::kSrcLfo1, targetOfParam(kTension), 40.0f); connect(d, 1, mod::kSrcLfo2, targetOfParam(kTone), 30.0f);
                         connect(d, 2, mod::kSrcVelocity, targetOfParam(kDecay), 40.0f); connect(d, 3, mod::kSrcRandom, targetOfParam(kSpread), 20.0f); }
            d.prepareDsp();
            std::vector<synth::Midi> ev; for (int k = 0; k < 8; ++k) ev.push_back({ 0, 0x90, (uint8_t)(36 + 7 * k), 110 });
            render(d, 0.2, ev);
            const auto t0 = std::chrono::steady_clock::now();
            render(d, 4.0, {});
            return 100.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
        };
        const double plain = cost(false), wired = cost(true);
        printf("  cpu: 8 three-spring voices %.1f %% of one core; with 2 LFOs and 2 per-note connections %.1f %%\n", plain, wired);
        CHECK(wired < plain * 1.6 + 2.0, "modulation costs at most about half again");
    }
    std::printf(fails ? "test_coil_mod: %d FAILED\n" : "test_coil_mod: all passed\n", fails);
    return fails ? 1 : 0;
}
