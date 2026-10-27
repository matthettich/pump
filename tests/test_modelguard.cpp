// pump AMP: the .nam pre-check (plugins/amp/ModelGuard.hpp). Every model NAM ships passes it (including the container
// models whose weights live in their submodels), obviously-not-models are turned away with a reason, and the crash
// marker behaves across the two loads a project restore makes. Needs no NAM code: only the JSON header.
#include "../plugins/amp/ModelGuard.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace pump::amp;
namespace fs = std::filesystem;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("  FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static std::string put(const char* name, const std::string& s) { const std::string p = std::string("build/tests/") + name; std::ofstream(p, std::ios::binary) << s; return p; }

int main()
{
    fs::create_directories("build/tests");
    // ---- every model NAM ships must pass
    int n = 0, bad = 0;
    for (auto& e : fs::directory_iterator("third_party/NeuralAmpModelerCore/example_models"))
        if (e.path().extension() == ".nam") { ++n; const std::string r = checkModelFile(e.path().string()); if (!r.empty()) { ++bad; std::printf("  FAIL: %s rejected: %s\n", e.path().filename().string().c_str(), r.c_str()); } }
    CHECK(n > 0 && bad == 0, "all %d shipped example models pass the pre-check (%d rejected)", n, bad);
    if (bad) ++fails;

    // ---- what it must turn away
    const char* good = R"({"version":"0.5.0","architecture":"LSTM","config":{},"weights":[0.1,0.2]})";
    CHECK(checkModelFile(put("g_ok.nam", good)).empty(), "a minimal well-formed model passes");
    CHECK(!checkModelFile("build/tests/nope.nam").empty(), "missing file: %s", checkModelFile("build/tests/nope.nam").c_str());
    CHECK(!checkModelFile(put("g_empty.nam", "")).empty(), "empty file");
    CHECK(!checkModelFile(put("g_text.nam", "hello")).empty(), "not JSON: %s", checkModelFile("build/tests/g_text.nam").c_str());
    CHECK(!checkModelFile(put("g_arr.nam", "[1,2,3]")).empty(), "JSON but not an object");
    CHECK(!checkModelFile(put("g_trunc.nam", std::string(good).substr(0, 40))).empty(), "truncated JSON");
    CHECK(!checkModelFile(put("g_nokey.nam", R"({"version":"0.5.0","architecture":"LSTM","weights":[1]})")).empty(), "no config");
    CHECK(!checkModelFile(put("g_noweights.nam", R"({"version":"0.5.0","architecture":"LSTM","config":{}})")).empty(), "no weights key");
    CHECK(!checkModelFile(put("g_wstr.nam", R"({"version":"0.5.0","architecture":"LSTM","config":{},"weights":"abc"})")).empty(), "weights as a string");
    CHECK(!checkModelFile(put("g_huge.nam", R"({"version":"0.5.0","architecture":"WaveNet","config":{"layers":[{"input_size":4294967296}]},"weights":[1]})")).empty(), "a size that overflows an int");
    CHECK(checkModelFile(put("g_meta.nam", R"({"version":"0.5.0","architecture":"LSTM","config":{"metadata":{"date":4294967296}},"weights":[1]})")).empty(), "big numbers in the metadata are fine");
    CHECK(!checkModelFile(put("g_wnull.nam", R"({"version":"0.5.0","architecture":"LSTM","config":{},"weights":[1,null,2]})")).empty(), "a null among the weights");
    CHECK(!checkModelFile(put("g_wtext.nam", R"({"version":"0.5.0","architecture":"LSTM","config":{},"weights":[1,"x"]})")).empty(), "text among the weights");
    CHECK(!checkModelFile(put("g_arch.nam", R"({"version":"0.5.0","architecture":5,"config":{},"weights":[1]})")).empty(), "architecture not a string");
    CHECK(!checkModelFile(put("g_nested.nam", R"({"version":"0.5.0","architecture":"SlimmableContainer","config":{"submodels":[{"model":{"weights":"x"}}]},"weights":[]})")).empty(), "a bad weights array nested in a submodel");
    CHECK(checkModelFile(put("g_cont.nam", R"({"version":"0.7.0","architecture":"SlimmableContainer","config":{"submodels":[{"model":{"weights":[1,2]}}]},"weights":[]})")).empty(), "a container with no weights of its own passes");
    { std::string deep = R"({"version":"0.5.0","architecture":"X","config":)"; for (int i = 0; i < 200; ++i) deep += "["; for (int i = 0; i < 200; ++i) deep += "]"; deep += R"(,"weights":[]})";
      CHECK(!checkModelFile(put("g_deep.nam", deep)).empty(), "nested 200 deep"); }

    // ---- the crash marker, across a restore's two loads (setState, then activate)
    const std::string path = "/tmp/some/model.nam";
    clearLoadMarker(path);
    CHECK(!previousLoadCrashed(path), "no marker to begin with");
    markLoadStart(path);
    CHECK(previousLoadCrashed(path), "a load that started and never finished is seen next time");
    markLoadDone(path);
    CHECK(!previousLoadCrashed(path), "a load that finished leaves nothing behind");
    markLoadStart(path);
    clearLoadMarker(path);
    CHECK(!previousLoadCrashed(path), "clearing removes it");
    CHECK(loadMarkerPath("/a/b.nam") != loadMarkerPath("/a/c.nam"), "different paths, different markers");
    CHECK(loadMarkerPath("/a/b.nam") == loadMarkerPath("/a/b.nam"), "the same path, the same marker");

    // a refusal must hold across repeated opens (the project keeps pointing at the file), and a changed file is retried
    {
        const std::string f = put("g_marker.nam", good);
        clearLoadMarker(f);
        markLoadStart(f);   // the host died inside the load
        CHECK(previousLoadCrashed(f), "crash seen on the next open");
        CHECK(previousLoadCrashed(f), "and again on the open after (a refusal does not clear it)");
        CHECK(previousLoadCrashed(f), "and again");
        put("g_marker.nam", std::string(good) + " ");   // the file is replaced: new size
        CHECK(!previousLoadCrashed(f), "a changed file has a new key and is tried again");
        markLoadStart(f); clearLoadMarker(f);
        CHECK(!previousLoadCrashed(f), "CLEAR removes the marker for the file as it is now");
        markLoadStart(f);
        CHECK(!previousLoadCrashed("build/tests/does_not_exist.nam"), "a different file is unaffected");
        clearLoadMarker(f);
    }

    std::printf(fails ? "test_modelguard: %d FAILED\n" : "test_modelguard: all passed\n", fails);
    return fails ? 1 : 0;
}
