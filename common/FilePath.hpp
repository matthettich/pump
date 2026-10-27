// pump: file names as UTF-8 on every system.
//
// Paths reach pump as UTF-8: from DPF's file browser and from the host's saved session. On Windows the narrow C and
// C++ file functions (fopen, std::ifstream(std::string), std::filesystem::path(std::string)) read a string in the ANSI
// code page instead, so a name outside it (Japanese, accented, emoji) would not open. Everything that opens or lists
// files goes through these.
#pragma once

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace pump {

inline std::filesystem::path utf8Path(const std::string& s)
{
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
#else
    return std::filesystem::u8path(s);
#endif
}

inline std::string pathUtf8(const std::filesystem::path& p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

inline FILE* fopenUtf8(const std::string& path, const char* mode)
{
#ifdef _WIN32
    const std::wstring m(mode, mode + std::strlen(mode));
    return _wfopen(utf8Path(path).c_str(), m.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

} // namespace pump
