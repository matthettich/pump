// Minimal CLAP host smoke test for the pump plugins: loads each .clap, instantiates at several sample rates,
// checks audio ports, parameters (info, value text round trip, defaults inside range), processes real audio with
// random parameter automation (no NaN/inf, no runaway levels), saves and restores state (parameters must come back
// identical), and reports latency. Exit code = number of failures.
// Build: g++ -O2 -std=c++17 -I<dpf>/distrho/src tools/clap_check.cpp -ldl -o clap_check
#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/plugin.h"
#include "clap/host.h"
#include "clap/process.h"
#include "clap/events.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/ext/latency.h"
#include "clap/ext/audio-ports.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <random>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("    FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const void* hostExt(const clap_host*, const char*) { return nullptr; }
static void hostReq(const clap_host*) {}
static clap_host host = { CLAP_VERSION, nullptr, "pump-check", "wake", "", "0.1", hostExt, hostReq, hostReq, hostReq };

struct Buf { std::vector<uint8_t> d; size_t r = 0; };
static int64_t ostreamWrite(const clap_ostream* s, const void* b, uint64_t n)
{ auto* x = (Buf*)s->ctx; x->d.insert(x->d.end(), (const uint8_t*)b, (const uint8_t*)b + n); return (int64_t)n; }
static int64_t istreamRead(const clap_istream* s, void* b, uint64_t n)
{ auto* x = (Buf*)s->ctx; const uint64_t k = std::min<uint64_t>(n, x->d.size() - x->r); std::memcpy(b, x->d.data() + x->r, k); x->r += k; return (int64_t)k; }

// parameter events for one block
struct Events { std::vector<clap_event_param_value> v; };
static uint32_t evSize(const clap_input_events* l) { return (uint32_t)((Events*)l->ctx)->v.size(); }
static const clap_event_header* evGet(const clap_input_events* l, uint32_t i) { return &((Events*)l->ctx)->v[i].header; }
static bool outPush(const clap_output_events*, const clap_event_header*) { return true; }

static std::vector<float> loadMix(const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen(path, "rb")) { std::fseek(f, 0, SEEK_END); v.resize((size_t)std::ftell(f) / 4); std::fseek(f, 0, SEEK_SET); if (std::fread(v.data(), 4, v.size(), f) != v.size()) v.clear(); std::fclose(f); }
    return v;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: clap_check plugin.clap [mix.f32]\n"); return 1; }
    const std::vector<float> mix = loadMix(argc > 2 ? argv[2] : "/home/claude/testkit/mix.f32");
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::printf("dlopen failed: %s\n", dlerror()); return 1; }
    auto* entry = (const clap_plugin_entry*)dlsym(lib, "clap_entry");
    if (!entry || !entry->init(argv[1])) { std::printf("no clap_entry\n"); return 1; }
    auto* fac = (const clap_plugin_factory*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    const uint32_t np = fac->get_plugin_count(fac);
    for (uint32_t pi = 0; pi < np; ++pi)
    {
        const clap_plugin_descriptor* desc = fac->get_plugin_descriptor(fac, pi);
        std::printf("%s (%s) by %s\n", desc->name, desc->id, desc->vendor);
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            const clap_plugin* p = fac->create_plugin(fac, &host, desc->id);
            CHECK(p && p->init(p), "create/init at %.0f", sr);
            if (!p) continue;
            auto* params = (const clap_plugin_params*)p->get_extension(p, CLAP_EXT_PARAMS);
            auto* state = (const clap_plugin_state*)p->get_extension(p, CLAP_EXT_STATE);
            auto* lat = (const clap_plugin_latency*)p->get_extension(p, CLAP_EXT_LATENCY);
            auto* ports = (const clap_plugin_audio_ports*)p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
            uint32_t nin = 0, nout = 0, inCh = 0;
            if (ports)
            {
                nin = ports->count(p, true); nout = ports->count(p, false);
                clap_audio_port_info info;
                if (nin > 0 && ports->get(p, 0, true, &info)) inCh = info.channel_count;
            }
            CHECK(nout >= 1 && nin >= 1 && inCh == 2, "audio ports: %u in (%u ch), %u out", nin, inCh, nout);
            const uint32_t n = params ? params->count(p) : 0;
            std::vector<clap_param_info> pinfo(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                params->get_info(p, i, &pinfo[i]);
                const auto& in = pinfo[i];
                CHECK(in.min_value <= in.default_value && in.default_value <= in.max_value, "param %s default outside range", in.name);
                double v = 0; params->get_value(p, in.id, &v);
                CHECK(std::isfinite(v) && v >= in.min_value - 1e-6 && v <= in.max_value + 1e-6, "param %s value %g out of range", in.name, v);
                char txt[256] = {};
                if (params->value_to_text(p, in.id, v, txt, sizeof txt)) CHECK(txt[0] != 0, "param %s empty text", in.name);
            }
            const int block = 256;
            CHECK(p->activate(p, sr, 32, 4096), "activate at %.0f", sr);
            p->start_processing(p);
            std::vector<float> inL(block), inR(block), sc(block, 0.0f), outL(block), outR(block);
            float* ins[2] = { inL.data(), inR.data() };
            float* scs[2] = { sc.data(), sc.data() };
            float* outs[2] = { outL.data(), outR.data() };
            std::vector<clap_audio_buffer> ib(std::max(1u, nin)), ob(1);
            ib[0] = { ins, nullptr, 2, 0, 0 };
            for (uint32_t k = 1; k < nin; ++k) ib[k] = { scs, nullptr, 2, 0, 0 };
            ob[0] = { outs, nullptr, 2, 0, 0 };
            Events ev; clap_input_events iev = { &ev, evSize, evGet }; clap_output_events oev = { nullptr, outPush };
            clap_process proc = {};
            proc.frames_count = block; proc.audio_inputs = ib.data(); proc.audio_inputs_count = nin;
            proc.audio_outputs = ob.data(); proc.audio_outputs_count = 1; proc.in_events = &iev; proc.out_events = &oev; proc.steady_time = -1;
            std::mt19937 rng(7);
            double peak = 0.0; bool bad = false;
            const int blocks = (int)(sr * 6 / block);
            size_t pos = 0;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i)
                {
                    inL[i] = mix.empty() ? 0.0f : mix[pos]; inR[i] = mix.empty() ? 0.0f : mix[pos + 1];
                    pos += 2; if (pos + 1 >= mix.size()) pos = 0;
                }
                ev.v.clear();
                // after 2 s: random automation of a few automatable parameters per block
                if (b > blocks / 3 && n > 0)
                    for (int k = 0; k < 3; ++k)
                    {
                        const auto& in = pinfo[rng() % n];
                        if (!(in.flags & CLAP_PARAM_IS_AUTOMATABLE) || (in.flags & CLAP_PARAM_IS_BYPASS)) continue;
                        std::uniform_real_distribution<double> u(in.min_value, in.max_value);
                        double v = u(rng);
                        if (in.flags & CLAP_PARAM_IS_STEPPED) v = std::round(v);
                        clap_event_param_value e = {};
                        e.header = { sizeof(e), (uint32_t)(rng() % block), CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                        e.param_id = in.id; e.cookie = in.cookie; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
                        ev.v.push_back(e);
                    }
                std::sort(ev.v.begin(), ev.v.end(), [](const auto& a, const auto& c) { return a.header.time < c.header.time; });
                p->process(p, &proc);
                for (int i = 0; i < block; ++i)
                {
                    if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) bad = true;
                    peak = std::max(peak, (double)std::max(std::fabs(outL[i]), std::fabs(outR[i])));
                }
            }
            CHECK(!bad, "NaN/inf in the output at %.0f Hz", sr);
            // back to defaults: whatever the automation did, the plugin must settle back to sane levels
            // (an unstable filter or a stuck state would stay huge)
            {
                ev.v.clear();
                for (uint32_t i = 0; i < n; ++i)
                {
                    const auto& in = pinfo[i];
                    if (!(in.flags & CLAP_PARAM_IS_AUTOMATABLE)) continue;
                    clap_event_param_value e = {};
                    e.header = { sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                    e.param_id = in.id; e.cookie = in.cookie; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = in.default_value;
                    ev.v.push_back(e);
                }
                double settle = 0.0, inPeak = 0.0;
                const int nb = (int)(sr * 3 / block);
                for (int b = 0; b < nb; ++b)
                {
                    for (int i = 0; i < block; ++i)
                    {
                        inL[i] = mix.empty() ? 0.0f : mix[pos]; inR[i] = mix.empty() ? 0.0f : mix[pos + 1];
                        pos += 2; if (pos + 1 >= mix.size()) pos = 0;
                        inPeak = std::max(inPeak, (double)std::max(std::fabs(inL[i]), std::fabs(inR[i])));
                    }
                    p->process(p, &proc);
                    ev.v.clear();
                    if (b > nb / 2)
                        for (int i = 0; i < block; ++i)
                        {
                            if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) bad = true;
                            settle = std::max(settle, (double)std::max(std::fabs(outL[i]), std::fabs(outR[i])));
                        }
                }
                CHECK(!bad && settle < 4.0 * inPeak + 1e-3, "after returning to defaults the output peak is %.3f (input %.3f) at %.0f Hz", settle, inPeak, sr);
                peak = settle;
            }
            const uint32_t latency = lat ? lat->get(p) : 0;
            // state round trip
            if (state && params)
            {
                std::vector<double> before(n);
                for (uint32_t i = 0; i < n; ++i) params->get_value(p, pinfo[i].id, &before[i]);
                Buf buf; clap_ostream os = { &buf, ostreamWrite };
                CHECK(state->save(p, &os), "state save");
                p->stop_processing(p);
                p->deactivate(p);
                const clap_plugin* q = fac->create_plugin(fac, &host, desc->id);
                q->init(q);
                auto* qs = (const clap_plugin_state*)q->get_extension(q, CLAP_EXT_STATE);
                auto* qp = (const clap_plugin_params*)q->get_extension(q, CLAP_EXT_PARAMS);
                buf.r = 0; clap_istream is = { &buf, istreamRead };
                CHECK(qs->load(q, &is), "state load");
                int diffs = 0;
                for (uint32_t i = 0; i < n; ++i)
                {
                    if (pinfo[i].flags & CLAP_PARAM_IS_READONLY) continue;
                    double v = 0; qp->get_value(q, pinfo[i].id, &v);
                    if (std::fabs(v - before[i]) > 1e-4 * std::max(1.0, std::fabs(before[i]))) { if (diffs++ < 3) std::printf("    state differs: %s %g vs %g\n", pinfo[i].name, v, before[i]); }
                }
                CHECK(diffs == 0, "%d parameters not restored from state", diffs);
                q->destroy(q);
                std::printf("  %.0f Hz: %u params, %u in ports, latency %u, %zu-byte state, peak %.2f — %s\n", sr, n, nin, latency, buf.d.size(), peak, fails ? "see above" : "ok");
            }
            else { p->stop_processing(p); p->deactivate(p); }
            p->destroy(p);
        }
    }
    entry->deinit();
    std::printf(fails ? "%d FAILED\n" : "all checks passed\n", fails);
    return fails;
}
