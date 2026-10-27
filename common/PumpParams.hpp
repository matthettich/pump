// pump: parameter and preset descriptions shared by every plugin's DSP and UI (no framework dependency).
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace pump {

// Hooks for headless screenshots and renders (tools/shot.sh, tools/render_presets.cpp): environment variables that are
// read only in builds made with PUMP_TEST_HOOKS=1 (`make jack` and the render tool do that). Released plugins never
// look at them.
inline const char* testEnv(const char* name)
{
#if defined(PUMP_TEST_HOOKS) && PUMP_TEST_HOOKS
    return std::getenv(name);
#else
    (void)name;
    return nullptr;
#endif
}

enum class PScale : int { Linear = 0, Log, Power };   // Log: geometric (frequencies, times); Power: skew toward min
enum class PUnit : int { None = 0, dB, Hz, ms, Percent, Ratio, Octave, Times, LU, Seconds };

enum PFlags : unsigned {
    kPAutomatable = 1u << 0,
    kPInteger     = 1u << 1,
    kPBoolean     = 1u << 2,
    kPOutput      = 1u << 3,   // read-only meter value for hosts
    kPBypass      = 1u << 4,   // the host bypass parameter
    kPHidden      = 1u << 5,   // not shown to hosts (kept for state)
    kPNoUndo      = 1u << 6,   // listening/display state (solo, analyzer): not part of undo, A/B or presets
};

// Locale-independent number parsing: optional sign, digits, '.' or ',' as the decimal point, optional exponent.
// (strtod follows the host process's locale and also accepts "nan"/"inf", neither of which we want.)
inline double parseNumber(const char* s, const char** end)
{
    const char* p = s;
    while (*p == ' ') ++p;
    bool neg = false;
    if (*p == '+' || *p == '-') { neg = *p == '-'; ++p; }
    double v = 0.0; bool digits = false;
    while (*p >= '0' && *p <= '9') { v = v * 10.0 + (*p - '0'); ++p; digits = true; }
    if ((*p == '.' || *p == ',') && p[1] >= '0' && p[1] <= '9')
    {
        ++p;
        double scale = 0.1;
        while (*p >= '0' && *p <= '9') { v += (*p - '0') * scale; scale *= 0.1; ++p; digits = true; }
    }
    else if ((*p == '.' || *p == ',') && digits) ++p;   // "12." is 12
    if (!digits) { if (end) *end = s; return 0.0; }
    if ((*p == 'e' || *p == 'E') && ((p[1] >= '0' && p[1] <= '9') || ((p[1] == '-' || p[1] == '+') && p[2] >= '0' && p[2] <= '9')))
    {
        ++p;
        bool eneg = false;
        if (*p == '+' || *p == '-') { eneg = *p == '-'; ++p; }
        int e = 0;
        while (*p >= '0' && *p <= '9') { if (e < 400) e = e * 10 + (*p - '0'); ++p; }
        v *= std::pow(10.0, eneg ? -e : e);
    }
    if (!std::isfinite(v)) { if (end) *end = s; return 0.0; }   // "1e999", "0e999" (0 x inf): not a number we accept
    if (end) *end = p;
    return neg ? -v : v;
}

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
        if (!std::isfinite(v)) v = std::isnan(v) ? def : (v > 0 ? max : min);   // never let NaN/inf through
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

    // Parse typed text: "1.2k", "-3", "4:1", "inf"; frequencies also as note names ("A4" = 440 Hz,
    // "C#3+13" = 13 cents above C#3); dB also as a gain factor ("2x" = +6.02 dB); and "50%" on any non-percent
    // control puts it halfway along its range. Returns false if nothing usable was typed.
    bool parse(const char* text, float& out) const
    {
        float v = 0.0f;
        if (!parseRaw(text, v) || !std::isfinite(v)) return false;
        out = v;
        return true;
    }
    bool parseRaw(const char* text, float& out) const
    {
        while (*text == ' ') ++text;
        const size_t len = std::strlen(text);
        // note names for frequencies
        if (unit == PUnit::Hz && len >= 2)
        {
            static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G relative to C
            const char c = (char)std::toupper((unsigned char)text[0]);
            if (c >= 'A' && c <= 'G')
            {
                const char* p = text + 1;
                int s = semis[c - 'A'];
                if (*p == '#') { ++s; ++p; } else if (*p == 'b') { --s; ++p; }
                char* e = nullptr;
                const long oct = std::strtol(p, &e, 10);
                if (e != p)
                {
                    double cents = 0.0;
                    if (*e == '+' || *e == '-') { const char* ce = nullptr; cents = parseNumber(e, &ce); if (ce == e) cents = 0.0; }
                    const double midi = 12.0 * (oct + 1) + s + cents / 100.0;
                    out = clamp((float)(440.0 * std::pow(2.0, (midi - 69.0) / 12.0)));
                    return true;
                }
            }
        }
        // "50%" on a control whose unit isn't percent: a position along the range
        if (len >= 2 && text[len - 1] == '%' && unit != PUnit::Percent)
        {
            const char* e = nullptr;
            const double pc = parseNumber(text, &e);
            if (e != text) { out = fromNorm((float)(pc / 100.0)); return true; }
        }
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
        const char* end = nullptr;
        double v = parseNumber(text, &end);
        if (end == text) return false;
        while (*end == ' ') ++end;
        if ((*end == 'x' || *end == 'X') && unit == PUnit::dB) v = 20.0 * std::log10(std::max(v, 1e-6));   // "2x" = +6 dB
        else if (*end == 'k' || *end == 'K') v *= 1000.0;
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
// A factory preset. `section` groups presets under a heading in the preset menu (nullptr = the main factory list,
// e.g. "SOURCES", "CHAINS"); sections appear in the order their first preset is added.
struct Preset { const char* name; std::vector<PresetValue> values; const char* section = nullptr; };

} // namespace pump
