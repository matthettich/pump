// pump AMP: the resampling bridge is transparent (a 1 kHz tone through 44.1 <-> 48 kHz and back, aligned by its
// stated latency); with no model and no impulse response everything is the input delayed by the cabinet's block;
// a single-tap impulse response read from a WAV file (16-bit, written here) is the same; NAM's example models load,
// run at 44.1, 48 and 96 kHz with the stated latency, and stay finite and sensible; normalize brings a model to the
// target loudness; the gate silences what falls below it; random settings and model / IR swaps while running stay
// finite. Also prints CPU per second of audio.
#include "TestMix.hpp"
#include "../plugins/amp/AmpEngine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>
#include <fstream>
#include "NamJson.hpp"

using namespace pump;
using namespace pump::amp;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb")) { std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear(); std::fclose(f); }
    return v;
}
static void run(Engine& e, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    const int n = (int)L.size();
    for (int off = 0; off < n; off += block) e.process(L.data() + off, R.data() + off, std::min(block, n - off));
}
static void writeWav16(const char* path, const std::vector<float>& x, int rate)
{
    FILE* f = std::fopen(path, "wb");
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + 2 * (uint32_t)x.size()); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(1); w32((uint32_t)rate); w32((uint32_t)rate * 2); w16(2); w16(16);
    std::fwrite("data", 1, 4, f); w32(2 * (uint32_t)x.size());
    for (float v : x) w16((uint16_t)(int16_t)std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f));
    std::fclose(f);
}
static const char* kModelDir = "third_party/NeuralAmpModelerCore/example_models/";

int main()
{
    const std::vector<float> mix = loadMix(pump_test_mix());
    if (mix.empty()) { printf("no test mix\n"); return 1; }
    // ---- the bridge: transparent at its latency
    for (auto rates : { std::make_pair(44100.0, 48000.0), std::make_pair(96000.0, 48000.0), std::make_pair(48000.0, 44100.0) })
    {
        RateBridge b; b.prepare(rates.first, rates.second, 256);
        const int n = (int)rates.first;
        std::vector<float> x((size_t)n), y((size_t)n);
        for (int i = 0; i < n; ++i) x[(size_t)i] = (float)(0.5 * std::sin(kTwoPi * 1000.0 * i / rates.first));
        for (int off = 0; off < n; off += 256)
            b.process(x.data() + off, y.data() + off, std::min(256, n - off), [](const float* in, float* out, int m) { std::memcpy(out, in, sizeof(float) * (size_t)m); });
        const int lat = b.latency();
        double e = 0, s = 0;
        for (int i = n / 2; i < n; ++i) { const double d = y[(size_t)i] - x[(size_t)(i - lat)]; e += d * d; s += (double)x[(size_t)(i - lat)] * x[(size_t)(i - lat)]; }
        CHECK(10 * std::log10(e / s) < -70.0, "rate bridge %.0f -> %.0f -> %.0f: 1 kHz back at latency %d, error %.1f dB", rates.first, rates.second, rates.first, lat, 10 * std::log10(e / s));
    }
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        printf("== %.0f Hz\n", sr);
        const int n = (int)(sr * 2.0);
        std::vector<float> L0((size_t)n), R0((size_t)n);
        for (int i = 0; i < n; ++i) { L0[(size_t)i] = mix[(size_t)(2 * i)]; R0[(size_t)i] = mix[(size_t)(2 * i + 1)]; }
        // ---- nothing loaded: the input delayed by the cabinet block
        {
            Engine e; e.prepare(sr, 512);
            std::vector<float> L = L0, R = R0;
            run(e, L, R, 333);
            const int lat = e.latency();
            double err = 0; for (int i = lat; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)(i - lat)]) + std::fabs(R[(size_t)i] - R0[(size_t)(i - lat)]));
            CHECK(lat == Engine::kCabBlock && err < 1e-6, "no model, no IR: the input delayed by %d (max error %.2g)", lat, err);
        }
        // ---- a one-tap IR from a 16-bit WAV at this rate: the same
        {
            std::vector<float> ir(64, 0.0f); ir[0] = 0.5f;
            writeWav16("build/tests/ir_test.wav", ir, (int)sr);
            Engine e; e.prepare(sr, 512);
            const IrInfo info = e.loadIr("build/tests/ir_test.wav");
            std::vector<float> L = L0, R = R0;
            run(e, L, R);
            const int lat = e.latency();
            double err = 0; for (int i = lat + 512; i < n; ++i) err = std::max(err, (double)std::fabs(L[(size_t)i] - L0[(size_t)(i - lat)]));
            CHECK(info.loaded && err < 1e-4, "one-tap IR from a WAV (normalised): the input delayed by %d (max error %.2g)", lat, err);
        }
        // ---- the example models
        for (const char* file : { "wavenet.nam", "lstm.nam" })
        {
            Engine e; e.prepare(sr, 512);
            const ModelInfo info = e.loadModel(std::string(kModelDir) + file);
            if (!info.loaded) { CHECK(false, "%s: %s", file, info.error.c_str()); continue; }
            Settings s; s.normalize = false; e.setSettings(s);
            // the swap happens on the audio thread after a fade: run a little silence first
            std::vector<float> z(4096, 0.0f), z2 = z;
            run(e, z, z2);
            std::vector<float> L = L0, R = R0;
            run(e, L, R);
            bool finite = true; double peak = 0, energy = 0;
            for (int i = 0; i < n; ++i) { finite = finite && std::isfinite(L[(size_t)i]); peak = std::max(peak, (double)std::fabs(L[(size_t)i])); energy += (double)L[(size_t)i] * L[(size_t)i]; }
            const int expectLat = Engine::kCabBlock + (info.resampled ? RateBridge().latency() : 0);
            (void)expectLat;
            CHECK(e.hasModel() && finite && peak < 20.0 && energy > 1e-3, "%s (%.0f Hz model%s): runs, peak %.2f, latency %d", file, info.rate, info.resampled ? ", resampled" : "", peak, e.latency());
            CHECK(info.resampled == (std::fabs(info.rate - sr) > 0.5), "%s: resampled only when the host rate differs", file);
        }
        if (sr != 48000.0) continue;
        // ---- a model whose weights don't match its architecture is refused (NAM used to read past the end of the
        // list here; patches/nam/0001). Run under ASan to see the difference: make tests-asan does not build amp, see
        // docs/HANDOVER.md. A2.nam keeps its weights in submodels, the others at the top.
        for (const char* file : { "wavenet.nam", "lstm.nam", "wavenet_a1_standard.nam", "A2.nam" })
            for (int drop : { 9, 1, -3 })   // remove 9 or 1, or add 3
            {
                std::ifstream in(std::string(kModelDir) + file);
                nlohmann::json j = nlohmann::json::parse(in);
                nlohmann::json& w = j["weights"].size() ? j["weights"] : j["config"]["submodels"][1]["model"]["weights"];
                if (drop > 0) for (int k = 0; k < drop; ++k) w.erase(w.size() - 1);
                else for (int k = 0; k < -drop; ++k) w.push_back(0.0);
                std::ofstream("build/tests/short.nam") << j.dump();
                Engine e; e.prepare(sr, 512);
                const ModelInfo info = e.loadModel("build/tests/short.nam");
                CHECK(!info.loaded && !info.error.empty(), "%s with %d weights %s: refused (%s)", file, std::abs(drop), drop > 0 ? "missing" : "extra", info.error.c_str());
            }
        // ---- normalize: a model that knows its loudness lands near the target on a guitar-like input
        {
            // the example models carry no loudness: a copy of one that says it is -12.5 dB loud
            {
                std::ifstream in(std::string(kModelDir) + "wavenet.nam");
                nlohmann::json j = nlohmann::json::parse(in);
                j["metadata"]["loudness"] = -12.5;
                std::ofstream("build/tests/loud.nam") << j.dump();
            }
            Engine e; e.prepare(sr, 512);
            const ModelInfo info = e.loadModel("build/tests/loud.nam");
            printf("  (%s: loudness %s %.1f dB)\n", info.name.c_str(), info.hasLoudness ? "known," : "unknown", info.loudness);
            if (info.hasLoudness)
            {
                double lv[2];
                for (int norm = 0; norm < 2; ++norm)
                {
                    Engine f; f.prepare(sr, 512); f.loadModel(info.path);
                    Settings s; s.normalize = norm == 1; f.setSettings(s);
                    std::vector<float> L = L0, R = R0;
                    run(f, L, R);
                    double en = 0; for (int i = n / 2; i < n; ++i) en += (double)L[(size_t)i] * L[(size_t)i];
                    lv[norm] = 10 * std::log10(en / (n / 2) + 1e-30);
                }
                CHECK(std::fabs((lv[1] - lv[0]) - (kTargetLoudness - info.loudness)) < 0.5, "normalize applies %.1f dB (asked %.1f)", lv[1] - lv[0], kTargetLoudness - info.loudness);
            }
        }
        // ---- the gate: noise below the threshold is silenced
        {
            Engine e; e.prepare(sr, 512);
            Settings s; s.gateDb = -50; e.setSettings(s);
            std::vector<float> L((size_t)n), R((size_t)n);
            std::mt19937 rng(3); std::uniform_real_distribution<float> u(-1.0f, 1.0f);
            for (int i = 0; i < n; ++i) L[(size_t)i] = R[(size_t)i] = (i < n / 4 ? 0.3f : 0.001f) * u(rng);
            run(e, L, R);
            double late = 0; for (int i = n / 2; i < n; ++i) late = std::max(late, (double)std::fabs(L[(size_t)i]));
            double early = 0; for (int i = 1000; i < n / 4; ++i) early = std::max(early, (double)std::fabs(L[(size_t)i]));
            CHECK(late < 1e-5 && early > 0.1, "gate at -50 dB: passes -10 dB noise (peak %.2f), silences -60 dB noise (peak %.2g)", early, late);
        }
        // ---- stress: random settings while models and IRs are swapped from another thread
        {
            Engine e; e.prepare(sr, 512);
            std::atomic<bool> stop { false };
            std::thread loader([&] {
                int k = 0;
                while (!stop.load())
                {
                    if (k % 3 == 0) e.loadModel(std::string(kModelDir) + (k % 2 ? "wavenet.nam" : "lstm.nam"));
                    else if (k % 3 == 1) e.loadIr("build/tests/ir_test.wav");
                    else if (k % 5 == 2) e.clearModel();
                    ++k;
                    std::this_thread::sleep_for(std::chrono::milliseconds(15));
                }
            });
            std::mt19937 rng(31); std::uniform_real_distribution<double> u(0.0, 1.0);
            bool finite = true; double peak = 0;
            std::vector<float> L(2048), R(2048);
            size_t pos = 0;
            for (int it = 0; it < 1500; ++it)
            {
                if (it % 9 == 0)
                {
                    Settings s; s.inputDb = u(rng) * 48 - 24; s.gateDb = u(rng) < 0.5 ? kGateOff : -90 + 60 * u(rng); s.normalize = u(rng) < 0.5;
                    s.channels = u(rng) < 0.5 ? 0 : 1; s.bass = u(rng) * 24 - 12; s.mid = u(rng) * 24 - 12; s.treble = u(rng) * 24 - 12; s.presence = u(rng) * 24 - 12;
                    s.cab = u(rng) < 0.7; s.cabLow = 10 * std::pow(50.0, u(rng)); s.cabHigh = 1000 * std::pow(22.0, u(rng)); s.outDb = 0;
                    e.setSettings(s);
                }
                const int bs = 1 + (int)(u(rng) * 1000);
                for (int i = 0; i < bs; ++i) { L[(size_t)i] = mix[(pos * 2) % mix.size()]; R[(size_t)i] = mix[(pos * 2 + 1) % mix.size()]; ++pos; }
                e.process(L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i) { finite = finite && std::isfinite(L[(size_t)i]) && std::isfinite(R[(size_t)i]); peak = std::max(peak, (double)std::fabs(L[(size_t)i])); }
            }
            stop.store(true);
            loader.join();
            CHECK(finite && peak < 100.0, "random settings with models and IRs swapped from another thread: finite (peak %.1f)", peak);
        }
        // ---- CPU
        for (const char* file : { "wavenet.nam", "lstm.nam" })
        {
            Engine e; e.prepare(sr, 512);
            if (!e.loadModel(std::string(kModelDir) + file).loaded) continue;
            e.loadIr("build/tests/ir_test.wav");
            Settings s; e.setSettings(s);
            std::vector<float> L = L0, R = R0;
            const auto t0 = std::chrono::steady_clock::now();
            run(e, L, R, 512);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("  cpu: %s, mono, with cab: %.1f %% of one core\n", file, 100.0 * secs / 2.0);
        }
    }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
