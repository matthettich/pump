// Room Profiles (Windows): from a pump ROOM profile to what the system-wide equalizer for Windows reads. Platform-free,
// so tests/test_room_profiles.cpp checks it on any system.
//
// The equalizer reads config.txt in its config folder and reloads whenever a file there changes. Room Profiles adds
// one line to config.txt (Include: pump-room-active.txt) and, to switch, rewrites pump-room-active.txt: the chosen
// profile's filters (minimum phase, the preamp and the time alignment inside, one stereo .wav per common sample rate,
// in pump-room\<folder>\) and the lines that pick the one for the device's rate.
#pragma once

#include "../../common/dsp/RoomCorrect.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace pump {
namespace apo {

inline const int kRates[] = { 44100, 48000, 88200, 96000, 192000 };
inline const char* kActiveFile = "pump-room-active.txt";
inline const char* kIncludeLine = "Include: pump-room-active.txt";

// a 32-bit float WAV, channels interleaved
inline std::vector<uint8_t> floatWav(const std::vector<std::vector<float>>& ch, int fs)
{
    const size_t nc = ch.size(), n = nc ? ch[0].size() : 0;
    std::vector<uint8_t> out;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { out.push_back((uint8_t)v); out.push_back((uint8_t)(v >> 8)); };
    auto tag = [&](const char* t) { out.insert(out.end(), t, t + 4); };
    const uint32_t bytes = (uint32_t)(n * nc * 4);
    tag("RIFF"); u32(36 + bytes); tag("WAVE"); tag("fmt "); u32(16); u16(3); u16((uint16_t)nc); u32((uint32_t)fs);
    u32((uint32_t)fs * (uint32_t)nc * 4); u16((uint16_t)(nc * 4)); u16(32); tag("data"); u32(bytes);
    out.reserve(out.size() + bytes);
    for (size_t i = 0; i < n; ++i)
        for (size_t c = 0; c < nc; ++c)
        {
            float v = i < ch[c].size() ? ch[c][i] : 0.0f;
            uint32_t b;
            std::memcpy(&b, &v, 4);
            u32(b);
        }
    return out;
}

// a folder name from a profile name: letters, digits, space, - and _ (at most 60), never empty
inline std::string folderName(const std::string& name)
{
    std::string s;
    for (unsigned char c : name)
    {
        if (s.size() >= 60) break;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_') s += (char)c;
        else if (c == ' ' || c == '.') s += (s.empty() || s.back() == ' ') ? "" : " ";
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s.empty() ? "profile" : s;
}

struct ApoFiles {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;   // paths relative to the config folder ('\')
    std::string active;                                                   // pump-room-active.txt
};

// The filters and the active file for a profile. One-channel profiles use their filter on both sides.
inline ApoFiles buildApoFiles(const room::Profile& p, const std::string& folder)
{
    ApoFiles out;
    const std::string dir = "pump-room\\" + folder + "\\";
    std::string conf = "# pump ROOM profile: " + p.name + (p.headphones ? " (headphones)" : "") + "\r\n";
    conf += "# Written by Room Profiles: choose another profile from its menu rather than editing this file.\r\n";
    conf += "Channel: L R\r\n";
    bool first = true;
    for (int fs : kRates)
    {
        std::vector<std::vector<float>> ch;
        for (char side : { 'L', 'R' })
        {
            const room::ProfileChannel* c = p.side(side);
            if (c == nullptr) c = p.side(side == 'L' ? 'R' : 'L');
            ch.push_back(room::designFir(c->f, c->correction, fs, room::correctionTaps(fs), false,
                                         c->delayMs * 0.001 * fs, p.preampDb));
        }
        const size_t n = std::max(ch[0].size(), ch[1].size());
        for (auto& v : ch) v.resize(n, 0.0f);
        const std::string name = dir + std::to_string(fs) + ".wav";
        out.files.emplace_back(name, floatWav(ch, fs));
        conf += std::string(first ? "If: " : "ElseIf: ") + "sampleRate == " + std::to_string(fs) + "\r\n";
        conf += "Convolution: " + name + "\r\n";
        first = false;
    }
    conf += "EndIf:\r\nChannel: all\r\n";
    out.active = conf;
    return out;
}

// the active file for no profile
inline std::string emptyActive()
{
    return "# pump ROOM: no profile (choose one from Room Profiles' menu)\r\n";
}

// config.txt with the include line added at the end (unchanged when it is there already, even commented differently)
inline std::string withInclude(const std::string& config, bool* changed = nullptr)
{
    size_t pos = 0;
    while (pos < config.size())
    {
        size_t eol = config.find('\n', pos);
        if (eol == std::string::npos) eol = config.size();
        std::string line = config.substr(pos, eol - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
        if (line.compare(a, std::string::npos, kIncludeLine) == 0) { if (changed) *changed = false; return config; }
        pos = eol + 1;
    }
    std::string s = config;
    if (!s.empty() && s.back() != '\n') s += "\r\n";
    s += "# pump ROOM's room and headphone profiles (Room Profiles switches what this file holds)\r\n";
    s += kIncludeLine;
    s += "\r\n";
    if (changed) *changed = true;
    return s;
}

} // namespace apo
} // namespace pump
