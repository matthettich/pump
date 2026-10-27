// pump TUNE: the graph's notes and captured pitch as text (the plugin's "notes" and "curve" states), and the note
// finder that turns a captured pitch curve into notes. Framework-free; shared by the DSP, the UI and the tests.
//
// "notes": a line "tune-notes 1", then one line per note: t0 t1 src dst straighten vibrato formant gain (seconds on
// the host's timeline, MIDI notes, 0..1, 0..2, semitones, dB), with '.' decimals whatever the locale.
// "curve": a line "tune-curve 1", then runs of evenly spaced frames: "R <t0> <hop> <count>" and a line of two
// characters per frame (the pitch in 1/32 semitones, 0 = unvoiced).
#pragma once

#include "TuneEngine.hpp"
#include "PumpParams.hpp"
#include <string>
#include <vector>

namespace pump { namespace tune {

struct CurveFrame { double t; float midi; };   // midi < 0: unvoiced

inline void appendNum(std::string& s, double v, const char* fmt)
{
    char b[48];
    std::snprintf(b, sizeof b, fmt, v);
    for (char* c = b; *c; ++c) if (*c == ',') *c = '.';
    s += b;
}

inline std::string notesToText(const std::vector<GraphNote>& notes)
{
    std::string s = "tune-notes 1\n";
    for (const GraphNote& n : notes)
    {
        appendNum(s, n.t0, "%.4f"); s += ' '; appendNum(s, n.t1, "%.4f"); s += ' ';
        appendNum(s, n.src, "%.3f"); s += ' '; appendNum(s, n.dst, "%.3f"); s += ' ';
        appendNum(s, n.straighten, "%.3f"); s += ' '; appendNum(s, n.vibrato, "%.3f"); s += ' ';
        appendNum(s, n.formant, "%.2f"); s += ' '; appendNum(s, n.gainDb, "%.2f"); s += '\n';
    }
    return s;
}

// Parses what notesToText wrote; notes out of order, overlapping or with absurd values are dropped or clamped.
inline std::vector<GraphNote> notesFromText(const char* text)
{
    std::vector<GraphNote> out;
    if (!text) return out;
    const char* p = text;
    if (std::strncmp(p, "tune-notes", 10) != 0) return out;
    while (*p && *p != '\n') ++p;
    while (*p)
    {
        while (*p == '\n' || *p == '\r') ++p;
        if (!*p) break;
        double v[8]; int k = 0;
        while (k < 8)
        {
            const char* e = nullptr;
            v[k] = parseNumber(p, &e);
            if (e == p) break;
            ++k; p = e;
            while (*p == ' ') ++p;
        }
        while (*p && *p != '\n') ++p;
        if (k < 8 || !(v[1] > v[0]) || !std::all_of(v, v + 8, [](double x) { return std::isfinite(x); })) continue;
        GraphNote n;
        n.t0 = v[0]; n.t1 = v[1];
        n.src = (float)clampv(v[2], 0.0, 127.0); n.dst = (float)clampv(v[3], 0.0, 127.0);
        n.straighten = (float)clampv(v[4], 0.0, 1.0); n.vibrato = (float)clampv(v[5], 0.0, 2.0);
        n.formant = (float)clampv(v[6], -12.0, 12.0); n.gainDb = (float)clampv(v[7], -48.0, 12.0);
        if (!out.empty() && n.t0 < out.back().t1) { if (n.t0 < out.back().t0) continue; n.t0 = out.back().t1; if (!(n.t1 > n.t0)) continue; }
        out.push_back(n);
    }
    return out;
}

inline std::string curveToText(const std::vector<CurveFrame>& c)
{
    static const char* kAlpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s = "tune-curve 1\n";
    size_t i = 0;
    while (i < c.size())
    {
        // a run: frames evenly spaced (within 10 %) from i
        size_t j = i + 1;
        const double hop = j < c.size() ? c[j].t - c[i].t : 0.0053;
        while (j < c.size() && j - i < 100000 && hop > 0.0 && std::fabs((c[j].t - c[j - 1].t) - hop) < 0.1 * hop) ++j;
        if (j == i + 1 && j < c.size() && !(hop > 0.0 && hop < 0.05)) j = i + 1;
        s += "R "; appendNum(s, c[i].t, "%.5f"); s += ' '; appendNum(s, hop > 0.0 && hop < 0.05 ? hop : 0.0053, "%.7f"); s += ' ';
        s += std::to_string(j - i); s += '\n';
        for (size_t k = i; k < j; ++k)
        {
            const int q = c[k].midi > 0.0f ? clampv((int)std::lround(c[k].midi * 32.0f), 1, 4095) : 0;
            s += kAlpha[q >> 6]; s += kAlpha[q & 63];
        }
        s += '\n';
        i = j;
    }
    return s;
}

inline std::vector<CurveFrame> curveFromText(const char* text)
{
    std::vector<CurveFrame> out;
    if (!text || std::strncmp(text, "tune-curve", 10) != 0) return out;
    auto val = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };
    const char* p = text;
    while (*p && *p != '\n') ++p;
    while (*p)
    {
        while (*p == '\n' || *p == '\r') ++p;
        if (*p != 'R') { while (*p && *p != '\n') ++p; continue; }
        ++p;
        const char* e = nullptr;
        const double t0 = parseNumber(p, &e); if (e == p) break; p = e;
        const double hop = parseNumber(p, &e); if (e == p) break; p = e;
        const double cnt = parseNumber(p, &e); if (e == p) break; p = e;
        while (*p && *p != '\n') ++p;
        if (*p) ++p;
        const long n = (long)clampv(cnt, 0.0, 1e7);
        for (long k = 0; k < n; ++k)
        {
            const int a = val(p[0]);
            const int b = a >= 0 ? val(p[1]) : -1;
            if (a < 0 || b < 0) break;
            const int q = (a << 6) | b;
            out.push_back({ t0 + k * hop, q > 0 ? q / 32.0f : -1.0f });
            p += 2;
        }
        while (*p && *p != '\n') ++p;
    }
    return out;
}

// ---- finding notes in a pitch curve -----------------------------------------------------------------------------------
// Voiced frames are grouped into stretches (gaps up to 40 ms are bridged); a stretch is split where the pitch moves to
// a new level for at least 45 ms (more than 0.6 semitone from the running note's centre); notes shorter than 50 ms
// join their neighbour. A note's source pitch is the median of its frames.
inline std::vector<GraphNote> findNotes(const std::vector<CurveFrame>& c, size_t from, size_t to)
{
    std::vector<GraphNote> notes;
    to = std::min(to, c.size());
    if (from >= to) return notes;
    auto median = [&](size_t a, size_t b) {
        std::vector<float> v;
        for (size_t k = a; k < b; ++k) if (c[k].midi > 0.0f) v.push_back(c[k].midi);
        if (v.empty()) return 60.0f;
        std::nth_element(v.begin(), v.begin() + (long)(v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    auto hopAt = [&](size_t k) { return k + 1 < c.size() ? std::min(0.02, c[k + 1].t - c[k].t) : 0.0053; };
    size_t k = from;
    while (k < to)
    {
        while (k < to && !(c[k].midi > 0.0f)) ++k;
        if (k >= to) break;
        // a voiced stretch [s, e)
        size_t s = k, e = k + 1, lastVoiced = k;
        while (e < to)
        {
            if (c[e].midi > 0.0f) lastVoiced = e;
            else if (c[e].t - c[lastVoiced].t > 0.040) break;
            ++e;
        }
        e = lastVoiced + 1;
        // split where the pitch settles somewhere new
        size_t ns = s;
        double centre = c[s].midi, sum = 0.0; int cnt = 0;
        size_t away = 0; bool isAway = false;
        auto emit = [&](size_t a, size_t b) {
            if (b <= a) return;
            GraphNote n;
            n.t0 = c[a].t - 0.5 * hopAt(a);
            n.t1 = c[b - 1].t + 0.5 * hopAt(b - 1);
            n.src = n.dst = median(a, b);
            notes.push_back(n);
        };
        for (size_t i = s; i < e; ++i)
        {
            if (!(c[i].midi > 0.0f)) continue;
            const double m = c[i].midi;
            if (std::fabs(m - centre) > 0.6)
            {
                if (!isAway) { isAway = true; away = i; }
                else if (c[i].t - c[away].t >= 0.045)
                {
                    emit(ns, away);
                    // the new note starts where the move began; its centre is where the pitch is now (a slide's
                    // frames don't count)
                    ns = away; centre = m; sum = m; cnt = 1; isAway = false;
                }
            }
            else
            {
                isAway = false;
                sum += m; ++cnt;
                centre = cnt < 8 ? sum / cnt : centre + (m - centre) * 0.05;
            }
        }
        emit(ns, e);
        k = e;
    }
    // short notes join a neighbour (the one nearer in pitch)
    for (size_t i = 0; i < notes.size();)
    {
        if (notes[i].t1 - notes[i].t0 >= 0.050 || notes.size() == 1) { ++i; continue; }
        const bool hasL = i > 0 && notes[i].t0 - notes[i - 1].t1 < 0.045;
        const bool hasR = i + 1 < notes.size() && notes[i + 1].t0 - notes[i].t1 < 0.045;
        if (!hasL && !hasR) { notes.erase(notes.begin() + (long)i); continue; }
        const bool left = hasL && (!hasR || std::fabs(notes[i - 1].src - notes[i].src) <= std::fabs(notes[i + 1].src - notes[i].src));
        if (left) { notes[i - 1].t1 = notes[i].t1; notes.erase(notes.begin() + (long)i); }
        else { notes[i + 1].t0 = notes[i].t0; notes.erase(notes.begin() + (long)i); }
    }
    return notes;
}

}} // namespace pump::tune
