// The web build's instruments against the plugins: for one instrument (PTYPE, its namespace PNS, its plugin source on
// the include path), plays the same MIDI phrase through the real plugin (DPF's own exporter) and through
// wasm/pump_synth.cpp, with the defaults and every factory preset (and, halfway, the next preset arriving as parameter
// changes), and checks that both give the same output. Built once per instrument (Makefile: build/tests/parity_<name>).
#include "DistrhoPluginInternal.hpp"
#include "src/DistrhoPlugin.cpp"
#define DISTRHO_IS_STANDALONE 0
#include "src/DistrhoUtils.cpp"
#include "../wasm/pump_synth.cpp"
#include <cmath>
#include <cstdio>
#include <tuple>
#include <vector>

USE_NAMESPACE_DISTRHO

static bool writeMidi(void*, const MidiEvent&) { return true; }
static bool reqParam(void*, uint32_t, float) { return true; }
static bool updState(void*, const char*, const char*) { return true; }

int main()
{
    const double sr = 48000.0;
    const uint32_t bs = 128, total = (uint32_t)(2.5 * sr) / bs * bs;
    // the phrase: (sample, note, velocity; 0 = off)
    std::vector<std::tuple<uint32_t, int, int>> ev;
    auto at = [&](double t, int n, int v) { ev.push_back({ (uint32_t)(t * sr), n, v }); };
    for (int n : { 48, 55, 60, 64 }) { at(0.01, n, 100); at(0.9, n, 0); }
    { const int mel[6] = { 60, 62, 64, 67, 69, 72 }; for (int k = 0; k < 6; ++k) { at(1.0 + k * 0.2, mel[k], 110); at(1.0 + k * 0.2 + 0.15, mel[k], 0); } }
    std::stable_sort(ev.begin(), ev.end(), [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });

    const auto& specs = pump::PNS::params();
    const auto& presets = pump::PNS::presets();
    int fails = 0;
    double worst = 0.0;
    for (int p = -1; p < (int)presets.size(); ++p)
    {
        std::vector<float> vals(specs.size());
        for (size_t i = 0; i < specs.size(); ++i) vals[i] = specs[i].def;
        if (p >= 0)
            for (const auto& v : presets[(size_t)p].values)
                for (size_t i = 0; i < specs.size(); ++i)
                    if (std::strcmp(specs[i].symbol, v.symbol) == 0) vals[i] = v.value;

        d_nextBufferSize = bs; d_nextSampleRate = sr; d_nextCanRequestParameterValueChanges = false;
        PluginExporter ex(nullptr, writeMidi, reqParam, updState);
        ex.setSampleRate(sr); ex.setBufferSize(bs);
        for (size_t i = 0; i < specs.size() && i < ex.getParameterCount(); ++i)
            if (!(specs[i].flags & pump::kPOutput)) ex.setParameterValue((uint32_t)i, vals[i]);
        ex.activate();
        for (size_t i = 0; i < specs.size() && i < 512; ++i) psyn_stage()[i] = vals[i];
        const int h = psyn_new(PTYPE, sr);
        if (h < 0) { std::printf("FAIL: psyn_new\n"); return 1; }

        std::vector<float> L(bs), R(bs);
        float* outs[2] = { L.data(), R.data() };
        double diff = 0.0;
        size_t e = 0;
        for (uint32_t off = 0; off < total; off += bs)
        {
            if (off == total / 2 / bs * bs && !presets.empty())
            {
                const auto& next = presets[(size_t)((p + 1) % (int)presets.size())];
                for (const auto& v : next.values)
                    for (size_t i = 0; i < specs.size(); ++i)
                        if (std::strcmp(specs[i].symbol, v.symbol) == 0 && !(specs[i].flags & pump::kPOutput))
                        {
                            ex.setParameterValue((uint32_t)i, v.value);
                            psyn_values(h)[i] = v.value;
                        }
                psyn_commit(h);
            }
            std::vector<MidiEvent> me;
            while (e < ev.size() && std::get<0>(ev[e]) < off + bs)
            {
                MidiEvent m {}; m.frame = std::get<0>(ev[e]) - off; m.size = 3;
                const int vel = std::get<2>(ev[e]);
                m.data[0] = vel ? 0x90 : 0x80; m.data[1] = (uint8_t)std::get<1>(ev[e]); m.data[2] = (uint8_t)vel;
                me.push_back(m);
                psyn_midi(h, (int)m.frame, m.data[0], m.data[1], m.data[2]);
                ++e;
            }
            const float* ins[2] = { nullptr, nullptr };
            ex.run(ins, outs, bs, me.data(), (uint32_t)me.size());
            psyn_process(h, (int)bs);
            const float* wl = psyn_out(0); const float* wr = psyn_out(1);
            for (uint32_t i = 0; i < bs; ++i) diff = std::max(diff, (double)std::max(std::fabs(L[i] - wl[i]), std::fabs(R[i] - wr[i])));
        }
        psyn_free(h);
        ex.deactivate();
        worst = std::max(worst, diff);
        if (diff > 1e-6)
        {
            ++fails;
            std::printf("  FAIL: %-28s max difference %.2e\n", p < 0 ? "(defaults)" : presets[(size_t)p].name, diff);
        }
    }
    std::printf("  %zu settings (defaults + factory presets): worst difference %.2e\n", presets.size() + 1, worst);
    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
