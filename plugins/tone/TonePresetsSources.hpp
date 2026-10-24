// pump TONE: presets for sources and stems (vocals, drums, bass, guitars, keys/synths, buses).
// Shown under SOURCES in the preset menu. Included inside the plugin's namespace by ToneParams.hpp, after the
// parameter table and the factory presets, so the same enums and symbols are available here.
#pragma once

inline void addSourcePresets(std::vector<Preset>& v)
{
    const char* S = "SOURCES";
    // The adaptive part pulls dark sources toward a mastering slope (it brightens them), so the presets meant to
    // smooth or tuck a source carry a negative Brighten to hold the top where it was.
    // Speed follows the source: fast enough on vocals and bass to ride phrases / notes, slower on beds and buses so
    // the balance moves with the arrangement, not with each hit.
    v.push_back({ "Vocals: smooth & even", { { "recover", 20 }, { "tame", 50 }, { "bias", 20 }, { "brighten", -35 }, { "body", -10 }, { "speed", 60 } }, S });
    v.push_back({ "Vocals: forward & airy", { { "recover", 50 }, { "tame", 30 }, { "bias", -10 }, { "brighten", 25 }, { "body", -25 }, { "speed", 50 } }, S });
    v.push_back({ "Backing vocals: soft & tucked", { { "recover", 0 }, { "tame", 50 }, { "bias", 40 }, { "brighten", -70 }, { "body", -40 }, { "speed", 45 } }, S });
    v.push_back({ "Drums: bus balance", { { "recover", 35 }, { "tame", 45 }, { "body", 10 }, { "speed", 30 }, { "quality", 1 } }, S });
    v.push_back({ "Bass: even notes", { { "recover", 30 }, { "tame", 70 }, { "speed", 80 }, { "quality", 1 } }, S });
    v.push_back({ "Guitars: tame fizz & mud", { { "recover", 25 }, { "tame", 55 }, { "bias", 20 }, { "brighten", -40 }, { "body", -25 }, { "speed", 40 } }, S });
    v.push_back({ "Keys: open up", { { "recover", 55 }, { "tame", 25 }, { "bias", -25 }, { "brighten", 15 }, { "body", -15 }, { "speed", 35 } }, S });
    v.push_back({ "Synths: even out a peaky lead", { { "recover", 10 }, { "tame", 60 }, { "bias", 35 }, { "brighten", -45 }, { "speed", 65 } }, S });
    v.push_back({ "Bus: mix bus pre-master", { { "recover", 30 }, { "tame", 40 }, { "bias", 10 }, { "speed", 45 }, { "quality", 1 } }, S });
}
