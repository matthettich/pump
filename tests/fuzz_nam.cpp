// pump AMP: fuzz the .nam model loader. A .nam file is JSON that NAM's core parses and builds a network from; pump
// loads user-chosen files, so a damaged or hostile one must come back as a "couldn't load" message and never crash,
// hang, or read out of range. This mutates NAM's real example models (truncation, random byte damage, JSON-aware
// edits: wrong array sizes, absurd dimensions, missing / retyped fields, huge numbers) and loads every result, then
// runs audio through whatever did load.
//
// Not part of `make tests` (it is about NAM's code more than pump's). It used to fail (about 4 % of cases corrupted
// memory, see docs/HANDOVER.md); with patches/nam/0001, NamJson.hpp and ModelGuard's range check, 600 of 600 pass. There is no make target: it needs NAM built with the sanitizers too, and
// the exact commands are in docs/HANDOVER.md ("Fuzzing it"). Usage once built:
//   ./build/asan/fuzz_nam <cases> <seed>   cases defaults to 600, seed to 1; the same seed repeats the same run.
// It stops at the first crash and leaves that input in build/fuzz/crash.nam.
#include "../plugins/amp/AmpEngine.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <filesystem>
#include "NamJson.hpp"

using namespace pump;
using namespace pump::amp;
using nlohmann::json;
namespace fs = std::filesystem;

static const char* kModelDir = "third_party/NeuralAmpModelerCore/example_models/";
static const char* kOut = "build/fuzz/";

static std::string slurp(const std::string& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static void spit(const std::string& p, const std::string& s) { std::ofstream o(p, std::ios::binary | std::ios::trunc); o << s; }

static std::mt19937_64 rng;
static size_t pick(size_t n) { return n ? (size_t)(rng() % n) : 0; }

// ---- byte-level damage
static std::string mutateBytes(std::string s)
{
    if (s.empty()) return s;
    switch (pick(5))
    {
    case 0: s.resize(pick(s.size())); break;                                                    // truncate anywhere
    case 1: for (int k = 0, n = 1 + (int)pick(20); k < n; ++k) s[pick(s.size())] = (char)rng(); break;   // random bytes
    case 2: { const size_t a = pick(s.size()), n = pick(std::min<size_t>(s.size() - a, 4096)); s.erase(a, n); break; }   // cut a chunk out
    case 3: { const size_t a = pick(s.size()), n = pick(std::min<size_t>(s.size() - a, 4096)); s.insert(pick(s.size()), s.substr(a, n)); break; }   // duplicate a chunk
    default: for (int k = 0, n = 1 + (int)pick(8); k < n; ++k) s[pick(s.size())] ^= (char)(1 << pick(8)); break;   // bit flips
    }
    return s;
}

// ---- structure-aware damage: parse, change something meaningful, write back
static void collect(json& j, std::vector<json*>& out, int depth = 0)
{
    out.push_back(&j);
    if (depth > 12) return;
    if (j.is_object()) for (auto& kv : j.items()) collect(kv.value(), out, depth + 1);
    else if (j.is_array()) for (auto& v : j) collect(v, out, depth + 1);
}
static json hostileValue()
{
    switch (pick(12))
    {
    case 0: return nullptr;
    case 1: return "text";
    case 2: return -1;
    case 3: return 0;
    case 4: return 2147483647;
    case 5: return 4294967296.0;
    case 6: return -2147483648.0;
    case 7: return 1e300;
    case 8: return json::array();
    case 9: return json::object();
    case 10: return true;
    default: return 65536;
    }
}
static std::string mutateJson(const std::string& src)
{
    json j = json::parse(src, nullptr, false);
    if (j.is_discarded()) return mutateBytes(src);
    for (int round = 0, rounds = 1 + (int)pick(3); round < rounds; ++round)
    {
        std::vector<json*> nodes; collect(j, nodes);
        json& n = *nodes[pick(nodes.size())];
        switch (pick(6))
        {
        case 0: n = hostileValue(); break;                                                          // retype / absurd value
        case 1: if (n.is_array() && !n.empty()) n.erase(n.begin() + (long)pick(n.size())); break;   // an array one short
        case 2: if (n.is_array() && !n.empty()) n.push_back(n[pick(n.size())]); break;              // one too long
        case 3: if (n.is_array()) n.clear(); break;                                                 // empty it
        case 4: if (n.is_object() && !n.empty()) { auto it = n.begin(); std::advance(it, (long)pick(n.size())); n.erase(it.key()); } break;   // drop a field
        default: if (n.is_array() && n.size() > 1) n.erase(n.begin() + (long)pick(n.size() / 2 + 1), n.end()); break;   // weights cut short
        }
    }
    return j.dump();
}

int main(int argc, char** argv)
{
    const int cases = argc > 1 ? std::atoi(argv[1]) : 600;
    const uint64_t seed = argc > 2 ? (uint64_t)std::strtoull(argv[2], nullptr, 10) : 1;
    rng.seed(seed);
    fs::create_directories(kOut);

    std::vector<std::pair<std::string, std::string>> seeds;   // (name, contents) of every model we can start from
    for (auto& e : fs::directory_iterator(kModelDir))
        if (e.path().extension() == ".nam") seeds.push_back({ e.path().filename().string(), slurp(e.path().string()) });
    if (seeds.empty()) { std::printf("no example models in %s (git submodule update --init --recursive)\n", kModelDir); return 2; }
    std::printf("fuzz_nam: %d cases, seed %llu, %zu seed models\n", cases, (unsigned long long)seed, seeds.size());

    Engine e; e.prepare(48000.0, 512);
    std::vector<float> L(512), R(512);
    int loaded = 0, rejected = 0, ran = 0;
    double worstMs = 0; std::string worstName;
    const std::string path = std::string(kOut) + "crash.nam";

    for (int c = 0; c < cases; ++c)
    {
        const auto& base = seeds[pick(seeds.size())];
        const std::string mut = pick(3) == 0 ? mutateBytes(base.second) : mutateJson(base.second);
        spit(path, mut);   // kept on disk: if the next line crashes, this is the input that did it
        std::printf("\r  case %d  (%s, %zu bytes)   ", c, base.first.c_str(), mut.size()); std::fflush(stdout);

        const auto t0 = std::chrono::steady_clock::now();
        const ModelInfo info = e.loadModel(path);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms > worstMs) { worstMs = ms; worstName = base.first + " case " + std::to_string(c); }

        if (!info.loaded)
        {
            ++rejected;
            if (info.error.empty()) { std::printf("\nFAIL case %d: rejected with no message (kept: %s)\n", c, path.c_str()); return 1; }
            continue;
        }
        ++loaded;
        e.installNow();   // as the plugin does on activation
        for (int k = 0; k < 4; ++k)
        {
            for (int i = 0; i < 512; ++i) { L[(size_t)i] = R[(size_t)i] = 0.25f * (float)std::sin(0.05 * (c * 2048 + k * 512 + i)); }
            e.process(L.data(), R.data(), 512);
            ++ran;
        }
    }
    e.clearModel();
    std::printf("\nfuzz_nam: %d cases: %d rejected cleanly, %d loaded and ran (%d blocks); slowest load %.0f ms (%s)\n",
                cases, rejected, loaded, ran, worstMs, worstName.c_str());
    std::printf("fuzz_nam: no crash, no sanitizer report\n");
    fs::remove(path);
    return 0;
}
