// The web build against the plugin: for one effect (PTYPE, its namespace PNS, its plugin source on the include path),
// runs the test mix through the real plugin (DPF's own exporter, no host, no UI) and through wasm/pump_wasm.cpp's
// Host of the same effect, with the defaults and with every factory preset (and, halfway through, the next preset
// arriving as parameter changes), and checks that both give the same
// output and the same latency. wasm/pump_wasm.cpp is a separate copy of each plugin's run(): this is what catches
// the two drifting apart. Built once per effect (Makefile: build/tests/parity_<name>), both sides with
// PUMP_SYNC_KERNELS=1 (the web build's kernel building), so only the wrappers differ.
#include "DistrhoPluginInternal.hpp"
#include "src/DistrhoPlugin.cpp"
#define DISTRHO_IS_STANDALONE 0
#include "src/DistrhoUtils.cpp"
#include "../wasm/pump_wasm.cpp"
#include "TestMix.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

USE_NAMESPACE_DISTRHO

static bool writeMidi(void*, const MidiEvent&) { return true; }
static bool reqParam(void*, uint32_t, float) { return true; }
static bool updState(void*, const char*, const char*) { return true; }

int main()
{
    const double sr = 48000.0;
    const uint32_t bs = 128;   // what Thunder Plus sends
    std::vector<float> mix;
    if (FILE* f = std::fopen(pump_test_mix(), "rb"))
    {
        float buf[4096]; size_t n;
        while ((n = std::fread(buf, sizeof(float), 4096, f)) > 0 && mix.size() < (size_t)(2 * sr * 3.0)) mix.insert(mix.end(), buf, buf + n);
        std::fclose(f);
    }
    if (mix.empty()) { std::printf("FAIL: no test mix at %s\n", pump_test_mix()); return 1; }
    const uint32_t frames = (uint32_t)(mix.size() / 2) / bs * bs;

    const auto& specs = pump::PNS::params();
    const auto& presets = pump::PNS::presets();
    int fails = 0;
    double worst = 0.0;
    for (int p = -1; p < (int)presets.size(); ++p)   // -1: the defaults
    {
        std::vector<float> vals(specs.size());
        for (size_t i = 0; i < specs.size(); ++i) vals[i] = specs[i].def;
        if (p >= 0)
            for (const auto& v : presets[(size_t)p].values)
                for (size_t i = 0; i < specs.size(); ++i)
                    if (std::strcmp(specs[i].symbol, v.symbol) == 0) vals[i] = v.value;

        // the plugin
        d_nextBufferSize = bs; d_nextSampleRate = sr; d_nextCanRequestParameterValueChanges = false;
        PluginExporter ex(nullptr, writeMidi, reqParam, updState);
        ex.setSampleRate(sr); ex.setBufferSize(bs);
        for (size_t i = 0; i < specs.size() && i < ex.getParameterCount(); ++i)
            if (!(specs[i].flags & pump::kPOutput)) ex.setParameterValue((uint32_t)i, vals[i]);
        ex.activate();
        // the web build
        for (size_t i = 0; i < specs.size() && i < 512; ++i) pump_stage()[i] = vals[i];
        const int h = pump_new(PTYPE, sr);
        if (h < 0) { std::printf("FAIL: pump_new\n"); return 1; }

        std::vector<float> in[4], out[2];
        for (auto& b : in) b.assign(bs, 0.0f);
        for (auto& b : out) b.assign(bs, 0.0f);
        double diff = 0.0; int latA = -1, latB = -1; bool latSame = true;
        for (uint32_t off = 0; off < frames; off += bs)
        {
            if (off == frames / 2 / bs * bs && !presets.empty())
            {
                // halfway: the next preset arrives as parameter changes, on both (the changes path of run())
                const auto& next = presets[(size_t)((p + 1) % (int)presets.size())];
                for (const auto& v : next.values)
                    for (size_t i = 0; i < specs.size(); ++i)
                        if (std::strcmp(specs[i].symbol, v.symbol) == 0 && !(specs[i].flags & pump::kPOutput))
                        {
                            ex.setParameterValue((uint32_t)i, v.value);
                            pump_values(h)[i] = v.value;
                        }
                pump_commit(h);
            }
            for (uint32_t i = 0; i < bs; ++i) { in[0][i] = mix[2 * (off + i)]; in[1][i] = mix[2 * (off + i) + 1]; }
            const float* ins[4] = { in[0].data(), in[1].data(), in[2].data(), in[3].data() };
            float* outs[2] = { out[0].data(), out[1].data() };
            ex.run(ins, outs, bs);
            float* io0 = pump_io(0); float* io1 = pump_io(1);
            std::memcpy(io0, in[0].data(), bs * sizeof(float)); std::memcpy(io1, in[1].data(), bs * sizeof(float));
            pump_process(h, (int)bs, 0);
            for (uint32_t i = 0; i < bs; ++i)
                diff = std::max(diff, (double)std::max(std::fabs(out[0][i] - io0[i]), std::fabs(out[1][i] - io1[i])));
           #if DISTRHO_PLUGIN_WANT_LATENCY
            latA = (int)ex.getLatency();
           #else
            latA = 0;
           #endif
            latB = pumpwasm::at(h)->reportedLatency();   // what its run() reported, as the plugin reports to the host
            if (latA != latB) latSame = false;
        }
        pump_free(h);
        ex.deactivate();
        worst = std::max(worst, diff);
        const bool ok = diff <= 1e-6 && latSame;
        if (!ok)
        {
            ++fails;
            std::printf("  FAIL: %-28s max difference %.2e, latency plugin %d / web %d%s\n", p < 0 ? "(defaults)" : presets[(size_t)p].name,
                        diff, latA, latB, latSame ? "" : " (differed during the run)");
        }
    }
    std::printf("  %zu settings (defaults + factory presets): worst difference %.2e\n", presets.size() + 1, worst);
    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
