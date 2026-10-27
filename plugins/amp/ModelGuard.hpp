// pump AMP: a cheap check of a .nam file before NAM's core is allowed to build a network from it, and a marker that
// stops a project from crashing the host again and again.
//
// Why: the fuzzer (tests/fuzz_nam.cpp) found that NAM's core read past the end of its weights array when a file's
// architecture and weights disagreed (about 4 % of randomly damaged models; heap-buffer-overflow in set_weights_).
// patches/nam/0001 fixes that in NAM itself (a bounds-checked weights cursor: a short file now throws). What is left
// here: reject files that are obviously not a model (not JSON, not an object, missing the keys NAM needs, a `weights`
// that isn't an array of numbers, absurd sizes, architecture numbers that overflow an int), and the crash marker below,
// which still limits anything not yet found to one crash per file instead of one per project open.
#pragma once

#include "NamJson.hpp"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace pump { namespace amp {

static constexpr std::uintmax_t kMaxModelBytes = 64u * 1024 * 1024;   // the biggest NAM models are a few MB
static constexpr size_t kMaxModelWeights = 16u * 1000 * 1000;
static constexpr int kMaxModelDepth = 64;

// Every "weights" array anywhere in the document must be an array of numbers; counts them. Returns "" when fine.
inline std::string checkWeights(const nlohmann::json& j, size_t& total, int depth = 0)
{
    if (depth > kMaxModelDepth) return "nested too deeply";
    if (j.is_object())
    {
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (it.key() == "weights")
            {
                if (!it.value().is_array()) return "weights is not a list";
                for (const auto& w : it.value()) if (!w.is_number()) return "weights has something that is not a number";
                total += it.value().size();
                if (total > kMaxModelWeights) return "too many weights";
            }
            else { const std::string e = checkWeights(it.value(), total, depth + 1); if (!e.empty()) return e; }
        }
    }
    else if (j.is_array())
        for (const auto& v : j) { const std::string e = checkWeights(v, total, depth + 1); if (!e.empty()) return e; }
    return "";
}

// The architecture's numbers (sizes, channels, dilations, rates; not the weights, not the metadata) must fit NAM's
// ints: a size of 2^32 wraps to a negative matrix size inside NAM (an assertion or a huge allocation).
static constexpr double kMaxConfigNumber = 16.0 * 1024 * 1024;
inline std::string checkConfigNumbers(const nlohmann::json& j, int depth = 0)
{
    if (depth > kMaxModelDepth) return "nested too deeply";
    if (j.is_object())
    {
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (it.key() == "weights" || it.key() == "metadata") continue;
            const std::string e = checkConfigNumbers(it.value(), depth + 1); if (!e.empty()) return e;
        }
    }
    else if (j.is_array()) { for (const auto& v : j) { const std::string e = checkConfigNumbers(v, depth + 1); if (!e.empty()) return e; } }
    else if (j.is_number())
    {
        const double v = j.get<double>();
        if (!(v > -kMaxConfigNumber && v < kMaxConfigNumber)) return "a size in the architecture is out of range";
    }
    return "";
}

// "" when the file looks like a model NAM can be given; otherwise why not (shown to the user).
inline std::string checkModelFile(const std::string& path)
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(std::filesystem::path(path), ec);
    if (ec) return "can't read the file";
    if (size == 0) return "empty file";
    if (size > kMaxModelBytes) return "file too large for a model";
    std::ifstream in(path, std::ios::binary);
    if (!in) return "can't read the file";
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) return "not a valid .nam file (damaged or incomplete?)";
    if (!j.is_object()) return "not a valid .nam file";
    for (const char* key : { "version", "architecture", "config", "weights" })
    {
        const auto it = j.find(key);
        // the container models keep their weights in the submodels and have none of their own, but `weights` is still present
        if (it == j.end()) return std::string("not a valid .nam file (no \"") + key + "\")";
    }
    if (!j["architecture"].is_string()) return "not a valid .nam file (architecture)";
    if (!j["version"].is_string()) return "not a valid .nam file (version)";
    size_t total = 0;
    const std::string e = checkWeights(j, total);
    if (!e.empty()) return "damaged .nam file (" + e + ")";
    const std::string c = checkConfigNumbers(j["config"]);
    if (!c.empty()) return "damaged .nam file (" + c + ")";
    // the rate sizes the resampler: a wild one would ask for gigabytes
    if (const auto it = j.find("sample_rate"); it != j.end() && !it->is_null())
    {
        if (!it->is_number()) return "damaged .nam file (sample rate)";
        const double r = it->get<double>();
        if (!(r >= 8000.0 && r <= 384000.0)) return "unsupported sample rate in the model";
    }
    if (const auto m = j.find("metadata"); m != j.end() && m->is_object())
        if (const auto l = m->find("loudness"); l != m->end() && l->is_number())
        {
            const double v = l->get<double>();
            if (!(v > -100.0 && v < 100.0)) return "damaged .nam file (loudness)";
        }
    return "";
}

// ---- the crash marker: a file that exists only while a load is in progress.
// A load that takes the host down leaves it behind; the next load of the same file sees it and refuses instead of
// crashing the project again. The marker is keyed on the path AND the file's size and modification time, so repairing
// or replacing the file gives it a fresh key (it is tried again), while an unchanged file stays refused for as long as
// it is unchanged: the saved project keeps pointing at it, and "refuse once, then try again" would just alternate
// between crashing and not crashing on every second open. One marker per key, in the temp folder.
inline std::filesystem::path loadMarkerPath(const std::string& modelPath)
{
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    std::string key = modelPath;
    {
        std::error_code e1, e2;
        const auto sz = std::filesystem::file_size(std::filesystem::path(modelPath), e1);
        const auto mt = std::filesystem::last_write_time(std::filesystem::path(modelPath), e2);
        if (!e1 && !e2) key += "|" + std::to_string((unsigned long long)sz) + "|" + std::to_string((long long)mt.time_since_epoch().count());
    }
    uint64_t h = 1469598103934665603ULL;   // FNV-1a of the key
    for (unsigned char c : key) { h ^= c; h *= 1099511628211ULL; }
    char name[64]; std::snprintf(name, sizeof name, "pump-amp-load-%016llx.lock", (unsigned long long)h);
    return dir / name;
}
// true when a previous load of this path never finished
inline bool previousLoadCrashed(const std::string& modelPath)
{
    const auto p = loadMarkerPath(modelPath);
    std::error_code ec;
    return !p.empty() && std::filesystem::exists(p, ec);
}
inline void markLoadStart(const std::string& modelPath)
{
    const auto p = loadMarkerPath(modelPath);
    if (!p.empty()) { std::ofstream o(p, std::ios::binary | std::ios::trunc); o << modelPath; }
}
inline void markLoadDone(const std::string& modelPath)
{
    const auto p = loadMarkerPath(modelPath);
    std::error_code ec;
    if (!p.empty()) std::filesystem::remove(p, ec);
}
// the user picked it again on purpose: forget the old crash
inline void clearLoadMarker(const std::string& modelPath) { markLoadDone(modelPath); }

}} // namespace pump::amp
