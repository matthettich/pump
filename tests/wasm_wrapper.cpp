// The WebAssembly wrapper (wasm/pump_wasm.cpp), built natively: every effect through its C interface as Thunder Plus
// drives it (128-sample blocks, values committed by index), checking that sound passes, each effect acts, bypass is
// latency-matched, the telemetry and feeds have the documented sizes, and a value change applies.
#include "../wasm/pump_wasm.cpp"
#include <cstdio>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("  FAIL: "); } else printf("  ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

static int idx(int type, const char* sym)
{
    const auto& s = specsOf(type);
    for (size_t i = 0; i < s.size(); ++i) if (std::strcmp(s[i].symbol, sym) == 0) return (int)i;
    return -1;
}
static int make(int type, std::initializer_list<std::pair<const char*, float>> vals)
{
    const auto& s = specsOf(type);
    for (size_t i = 0; i < s.size(); ++i) pump_stage()[i] = s[i].def;
    for (const auto& v : vals) pump_stage()[idx(type, v.first)] = v.second;
    return pump_new(type, 48000.0);
}
// runs `secs` of a 1 kHz sine at `db` dBFS (both channels; right at a different phase) and returns the output peak
// over the last half second
static float run(int h, double secs, float db, bool sc = false, float scDb = -120.0f)
{
    const int N = (int)(secs * 48000.0);
    const float a = std::pow(10.0f, db / 20.0f), b = std::pow(10.0f, scDb / 20.0f);
    float pk = 0.0f;
    for (int off = 0; off < N; off += 128)
    {
        for (int i = 0; i < 128; ++i)
        {
            const double t = (off + i) / 48000.0;
            pump_io(0)[i] = a * (float)std::sin(2.0 * pump::kPi * 1000.0 * t);
            pump_io(1)[i] = a * (float)std::sin(2.0 * pump::kPi * 1000.0 * t + 0.3);
            pump_io(2)[i] = pump_io(3)[i] = b * (float)std::sin(2.0 * pump::kPi * 60.0 * t);
        }
        pump_process(h, 128, sc ? 1 : 0);
        if (off >= N - 24000)
            for (int i = 0; i < 128; ++i) pk = std::max(pk, std::max(std::fabs(pump_io(0)[i]), std::fabs(pump_io(1)[i])));
        for (int i = 0; i < 128; ++i) if (!std::isfinite(pump_io(0)[i]) || !std::isfinite(pump_io(1)[i])) return NAN;
    }
    return pk;
}
static float db(float g) { return 20.0f * std::log10(std::max(g, 1e-9f)); }

int main()
{
    printf("== wasm wrapper (native build of wasm/pump_wasm.cpp)\n");
    CHECK(pump_types() == 9, "nine effect types");
    for (int t = 0; t < 9; ++t)
    {
        const int h = make(t, {});
        const float pk = run(h, 2.0, -12.0f);
        CHECK(h >= 0 && std::isfinite(pk) && pk > 0.05f, "type %d: sound passes at defaults (out peak %.1f dB, latency %d)", t, db(pk), pump_latency(h));
        const int n = (int)(pump_tel(h) - pump_tel(h)) + pump_tel_len(t);
        CHECK(n > 5 && n <= 512 && pump_tel(h)[0] == (float)pump_latency(h), "type %d: telemetry %d values, [0] = latency", t, n);
        bool rings = pump_rings(h) >= 2;
        for (int k = 0; k < pump_rings(h); ++k) rings = rings && pump_ring_cap(h, k) >= 4096 && pump_ring_pos(h, k) > 0;
        CHECK(rings, "type %d: %d sample feeds, all written", t, pump_rings(h));
        pump_free(h);
    }
    // COMP: gain reduction at -6 dBFS over a -30 dB threshold, 4:1
    {
        const int h = make(kComp, { { "threshold", -30 }, { "ratio", 4 }, { "knee", 0 }, { "attack", 1 }, { "release", 50 } });
        const float pk = run(h, 2.0, -6.0f);
        const float* t = pump_tel(h);
        CHECK(db(pk) < -18.0f && db(pk) > -26.0f, "COMP: 4:1 over -30 dB takes -6 dBFS to %.1f dB", db(pk));
        CHECK(std::min(t[5], t[6]) < -10.0f, "COMP: telemetry gain reduction %.1f dB", std::min(t[5], t[6]));
        // external sidechain: silent keys do nothing, a loud key ducks
        const int e = make(kComp, { { "threshold", -30 }, { "ratio", 8 }, { "sc_source", 1 }, { "attack", 1 } });
        const float quiet = run(e, 1.5, -12.0f, true, -120.0f);
        const float keyed = run(e, 1.5, -12.0f, true, -3.0f);
        CHECK(db(quiet) > -13.5f && db(keyed) < db(quiet) - 6.0f, "COMP: external sidechain (%.1f dB unkeyed, %.1f dB keyed)", db(quiet), db(keyed));
        pump_free(h); pump_free(e);
    }
    // LIMIT: holds its ceiling with 12 dB of gain
    {
        const int h = make(kLimit, { { "gain", 12 }, { "ceiling", -1 }, { "oversampling", 0 } });
        const float pk = run(h, 3.0, -6.0f);
        CHECK(db(pk) <= -0.99f && db(pk) > -2.5f, "LIMIT: +12 dB into a -1 dB ceiling peaks at %.2f dB", db(pk));
        CHECK(pump_tel(h)[5] > 3.0f, "LIMIT: telemetry gain reduction");
        pump_free(h);
    }
    // METER: a -20 dBFS sine in both channels reads about -20 LUFS (sine RMS -3 dB, two channels +3 dB, K-weighting at 1 kHz ~0)
    {
        const int h = make(kMeter, {});
        run(h, 4.0, -20.0f);
        const float* t = pump_tel(h);
        CHECK(t[6] > -21.5f && t[6] < -18.0f && std::fabs(t[7] - t[6]) < 0.5f, "METER: short-term %.1f LUFS, integrated %.1f LUFS", t[6], t[7]);
        pump_free(h);
    }
    // bypass: latency-matched pass-through (CALM, 2048 samples)
    {
        const int h = make(kCalm, { { "bypass", 1 }, { "depth", 100 } });
        const int lat = pump_latency(h);
        std::vector<float> in, out;
        for (int off = 0; off < 48000; off += 128)
        {
            for (int i = 0; i < 128; ++i) { const float v = (float)std::sin(0.01 * (off + i)) * 0.3f; pump_io(0)[i] = pump_io(1)[i] = v; in.push_back(v); }
            pump_process(h, 128, 0);
            for (int i = 0; i < 128; ++i) out.push_back(pump_io(0)[i]);
        }
        float mx = 0.0f;
        for (int i = lat + 1000; i < 48000; ++i) mx = std::max(mx, std::fabs(out[(size_t)i] - in[(size_t)(i - lat)]));
        CHECK(lat == 2048 && mx < 1e-6f, "CALM bypassed: the input delayed by its latency (%d), difference %.1e", lat, mx);
        // a value change through pump_values / pump_commit
        pump_values(h)[idx(kCalm, "quality")] = 0; pump_commit(h);
        run(h, 0.5, -12.0f);
        CHECK(pump_latency(h) == 1024, "CALM: quality change applies (latency %d)", pump_latency(h));
        pump_free(h);
    }
    // EQ linear phase without a worker thread
    {
        const int h = make(kEQ, { { "phase", 1 }, { "b1_used", 1 }, { "b1_shape", 0 }, { "b1_freq", 1000 }, { "b1_gain", -12 } });
        const float pk = run(h, 2.0, -6.0f);
        CHECK(pump_latency(h) > 4000 && db(pk) < -15.0f, "EQ linear phase: latency %d, -12 dB bell at 1 kHz gives %.1f dB", pump_latency(h), db(pk));
        pump_free(h);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
