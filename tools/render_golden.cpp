// Renders one plugin's sound for every setting it ships with, to check that a refactor didn't change it.
// The plugin runs through DPF's own exporter (no host, no UI): the defaults and every factory preset, each from a
// fresh instance at 48 kHz in 256-sample blocks, for 2.5 s. Effects hear the test mix (tests/data/mix.f32);
// instruments play a fixed phrase (low drum notes, a chord, a melody); both get both when they take both.
// Writes <outdir>/<index>.f32 (stereo interleaved float). tools/render_golden.sh builds and runs it for every plugin
// and compares two sets of renders.
#include "DistrhoPluginInternal.hpp"
#include "src/DistrhoPlugin.cpp"
#define DISTRHO_IS_STANDALONE 0
#include "src/DistrhoUtils.cpp"
#include PARAMS_HDR
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

USE_NAMESPACE_DISTRHO

static bool writeMidi(void*, const MidiEvent&) { return true; }
static bool reqParam(void*, uint32_t, float) { return true; }
static bool updState(void*, const char*, const char*) { return true; }

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: render_golden <mix.f32> <outdir>\n"); return 2; }
    const double sr = 48000.0;
    const uint32_t bs = 256, total = (uint32_t)(2.5 * sr) / bs * bs;
    std::vector<float> mix;
    if (FILE* f = std::fopen(argv[1], "rb"))
    {
        float buf[4096]; size_t n;
        while ((n = std::fread(buf, sizeof(float), 4096, f)) > 0 && mix.size() < 2 * (size_t)total) mix.insert(mix.end(), buf, buf + n);
        std::fclose(f);
    }
    if (mix.size() < 2 * (size_t)total) { std::fprintf(stderr, "test mix too short\n"); return 1; }
    // the phrase: (time s, note, velocity; 0 = off)
    std::vector<std::tuple<double, int, int>> ev;
    for (int k = 0; k < 8; ++k) { const int n = k % 4 == 0 ? 36 : k % 4 == 2 ? 38 : 42; ev.push_back({ 0.02 + k * 0.125, n, k % 2 ? 90 : 120 }); ev.push_back({ 0.02 + k * 0.125 + 0.08, n, 0 }); }
    for (int n : { 48, 55, 60, 64 }) { ev.push_back({ 0.05, n, 100 }); ev.push_back({ 1.0, n, 0 }); }
    { const int mel[6] = { 60, 62, 64, 67, 69, 72 }; for (int k = 0; k < 6; ++k) { ev.push_back({ 1.1 + k * 0.2, mel[k], 110 }); ev.push_back({ 1.1 + k * 0.2 + 0.15, mel[k], 0 }); } }
    std::stable_sort(ev.begin(), ev.end(), [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });

    const auto& ps = pump::NS::presets();
    for (int p = -1; p < (int)ps.size(); ++p)   // -1: the defaults
    {
        d_nextBufferSize = bs; d_nextSampleRate = sr; d_nextCanRequestParameterValueChanges = false;
        PluginExporter ex(nullptr, writeMidi, reqParam, updState);
        ex.setSampleRate(sr); ex.setBufferSize(bs);
        const uint32_t np = ex.getParameterCount();
        if (p >= 0)
            for (const auto& v : ps[(size_t)p].values)
                for (uint32_t i = 0; i < np; ++i)
                    if (ex.getParameterSymbol(i) == v.symbol) ex.setParameterValue(i, v.value);
        ex.activate();
        std::vector<float> L(bs), R(bs), inL(bs), inR(bs), sil(bs, 0.0f), out;
        out.reserve(2 * (size_t)total);
        float* outs[2] = { L.data(), R.data() };
        size_t e = 0;
        for (uint32_t off = 0; off < total; off += bs)
        {
            for (uint32_t i = 0; i < bs; ++i) { inL[i] = mix[2 * (off + i)]; inR[i] = mix[2 * (off + i) + 1]; }
            const float* ins[4] = { inL.data(), inR.data(), sil.data(), sil.data() };
           #if DISTRHO_PLUGIN_WANT_MIDI_INPUT
            std::vector<MidiEvent> me;
            while (e < ev.size() && std::get<0>(ev[e]) * sr < off + bs)
            {
                MidiEvent m {}; m.frame = (uint32_t)std::max(0.0, std::get<0>(ev[e]) * sr - off); m.size = 3;
                const int vel = std::get<2>(ev[e]);
                m.data[0] = vel ? 0x90 : 0x80; m.data[1] = (uint8_t)std::get<1>(ev[e]); m.data[2] = (uint8_t)vel;
                me.push_back(m); ++e;
            }
            ex.run(ins, outs, bs, me.data(), (uint32_t)me.size());
           #else
            (void)e;
            ex.run(ins, outs, bs);
           #endif
            for (uint32_t i = 0; i < bs; ++i) { out.push_back(L[i]); out.push_back(R[i]); }
        }
        ex.deactivate();
        const std::string fn = std::string(argv[2]) + "/" + std::to_string(p + 1) + ".f32";
        if (FILE* f = std::fopen(fn.c_str(), "wb")) { std::fwrite(out.data(), sizeof(float), out.size(), f); std::fclose(f); }
    }
    std::printf("%zu settings\n", ps.size() + 1);
    return 0;
}
