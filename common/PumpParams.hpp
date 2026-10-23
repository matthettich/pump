// pump: parameter and preset descriptions shared by every plugin's DSP and UI (no framework dependency).
#pragma once

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace pump {

enum class PScale : int { Linear = 0, Log, Power };   // Log: geometric (frequencies, times); Power: skew toward min
enum class PUnit : int { None = 0, dB, Hz, ms, Percent, Ratio, Octave, Times, LU, Seconds };

enum PFlags : unsigned {
    kPAutomatable = 1u << 0,
    kPInteger     = 1u << 1,
    kPBoolean     = 1u << 2,
    kPOutput      = 1u << 3,   // read-only meter value for hosts
    kPBypass      = 1u << 4,   // the host bypass parameter
    kPHidden      = 1u << 5,   // not shown to hosts (kept for state)
};

struct ParamSpec {
    const char* symbol;       // stable id (saved in sessions and presets): never rename
    const char* name;         // shown in hosts
    const char* shortName;    // shown on controls
    float min, max, def;
    PUnit unit;
    PScale scale;
    unsigned flags;
    const char* const* choices;   // for integer parameters with named values (nullptr otherwise)
    float skew;               // PScale::Power exponent (> 1 favours the low end)

    bool isChoice() const { return choices != nullptr; }
    int numChoices() const { return (int)(max - min) + 1; }

    // 0..1 <-> value
    float toNorm(float v) const
    {
        if (max <= min) return 0.0f;
        v = v < min ? min : (v > max ? max : v);
        switch (scale)
        {
        case PScale::Log: return (float)(std::log(v / min) / std::log(max / min));
        case PScale::Power: return (float)std::pow((v - min) / (max - min), 1.0 / (skew > 0 ? skew : 1.0));
        default: return (v - min) / (max - min);
        }
    }
    float fromNorm(float n) const
    {
        n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
        float v;
        switch (scale)
        {
        case PScale::Log: v = (float)(min * std::pow(max / min, (double)n)); break;
        case PScale::Power: v = (float)(min + (max - min) * std::pow((double)n, (double)(skew > 0 ? skew : 1.0))); break;
        default: v = min + (max - min) * n; break;
        }
        if (flags & (kPInteger | kPBoolean)) v = std::round(v);
        return v;
    }
    float clamp(float v) const
    {
        v = v < min ? min : (v > max ? max : v);
        if (flags & (kPInteger | kPBoolean)) v = std::round(v);
        return v;
    }

    // Display text for a value.
    std::string format(float v) const
    {
        char buf[64];
        if (choices) { const int i = (int)std::lround(v - min); return (i >= 0 && i < numChoices()) ? choices[i] : "?"; }
        if (flags & kPBoolean) return v >= 0.5f ? "ON" : "OFF";
        switch (unit)
        {
        case PUnit::dB:
            if (v <= -119.5f && min <= -119.0f) return "-INF";
            std::snprintf(buf, sizeof buf, std::fabs(v) < 9.95f ? "%+.1f dB" : "%+.0f dB", v);
            if (std::fabs(v) < 0.05f) std::snprintf(buf, sizeof buf, "0.0 dB");
            return buf;
        case PUnit::LU: std::snprintf(buf, sizeof buf, "%.1f LU", v); return buf;
        case PUnit::Hz:
            if (v >= 10000.0f) std::snprintf(buf, sizeof buf, "%.1f kHz", v / 1000.0f);
            else if (v >= 1000.0f) std::snprintf(buf, sizeof buf, "%.2f kHz", v / 1000.0f);
            else if (v >= 100.0f) std::snprintf(buf, sizeof buf, "%.0f Hz", v);
            else std::snprintf(buf, sizeof buf, "%.1f Hz", v);
            return buf;
        case PUnit::ms:
            if (v >= 1000.0f) std::snprintf(buf, sizeof buf, "%.2f s", v / 1000.0f);
            else if (v >= 100.0f) std::snprintf(buf, sizeof buf, "%.0f ms", v);
            else if (v >= 10.0f) std::snprintf(buf, sizeof buf, "%.1f ms", v);
            else std::snprintf(buf, sizeof buf, "%.2f ms", v);
            return buf;
        case PUnit::Percent: std::snprintf(buf, sizeof buf, "%.0f%%", v); return buf;
        case PUnit::Ratio:
            if (v >= max - 1e-3f && max >= 20.0f) return "INF:1";
            std::snprintf(buf, sizeof buf, v < 10.0f ? "%.1f:1" : "%.0f:1", v); return buf;
        case PUnit::Octave: std::snprintf(buf, sizeof buf, "%.2f oct", v); return buf;
        case PUnit::Times: std::snprintf(buf, sizeof buf, "%.0fx", v); return buf;
        case PUnit::Seconds: std::snprintf(buf, sizeof buf, "%.1f s", v); return buf;
        default:
            if (flags & kPInteger) std::snprintf(buf, sizeof buf, "%.0f", v);
            else if (std::fabs(v) >= 100.0f) std::snprintf(buf, sizeof buf, "%.0f", v);
            else std::snprintf(buf, sizeof buf, "%.2f", v);
            return buf;
        }
    }

    // Parse typed text ("1.2k", "-3", "50%", "4:1", "inf"). Returns false if nothing usable was typed.
    bool parse(const char* text, float& out) const
    {
        if (choices)
        {
            for (int i = 0; i < numChoices(); ++i)
                if (strncasecmp_portable(text, choices[i], std::strlen(text)) == 0 && std::strlen(text) > 0) { out = min + (float)i; return true; }
        }
        if (strncasecmp_portable(text, "inf", 3) == 0 || strncasecmp_portable(text, "-inf", 4) == 0)
        {
            out = text[0] == '-' ? min : max;
            return true;
        }
        if (flags & kPBoolean)
        {
            if (strncasecmp_portable(text, "on", 2) == 0) { out = 1.0f; return true; }
            if (strncasecmp_portable(text, "of", 2) == 0) { out = 0.0f; return true; }
        }
        char* end = nullptr;
        double v = std::strtod(text, &end);
        if (end == text) return false;
        while (*end == ' ') ++end;
        if (*end == 'k' || *end == 'K') v *= 1000.0;
        else if (unit == PUnit::ms && (*end == 's' || *end == 'S') && end[1] != 'm') v *= 1000.0;
        out = clamp((float)v);
        return true;
    }

    static int strncasecmp_portable(const char* a, const char* b, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const int ca = std::tolower((unsigned char)a[i]), cb = std::tolower((unsigned char)b[i]);
            if (ca != cb || ca == 0) return ca - cb;
        }
        return 0;
    }
};

// A factory preset: name and a list of symbol=value pairs (anything unlisted takes its default).
struct PresetValue { const char* symbol; float value; };
struct Preset { const char* name; std::vector<PresetValue> values; };

} // namespace pump
