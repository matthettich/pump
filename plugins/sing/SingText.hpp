// pump SING: lyrics -> phonemes -> syllables. Framework-free. A port of Thunder Plus's VoxCore text front end.
//
// Words go through a small dictionary of common words, else letter-to-sound rules (after the public-domain 1976 US
// Naval Research Lab rules); "[HH AX L OW]" spells phonemes out (ARPAbet), "hel-lo" forces a syllable break. A full
// stop, comma, ; : ! ? or _ is a rest (that note is silent). "~" holds the last vowel over another note (a melisma).
// Each syllable is its onset consonants, its vowel (the nucleus) and its closing consonants.
#pragma once

#include "SingTables.hpp"
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace pump { namespace sing {

// The phonemes. t: V vowel, L approximant, N nasal, F / Z unvoiced / voiced fricative, P / B unvoiced / voiced stop,
// C / J affricate, H aspiration. f: formants F1-F3 (Hz); g: where a diphthong ends; av: voicing (-1 = 1); nf / nb / fa:
// frication centre, width, level; bf / bb / ba: a stop's burst; d: length (ms).
struct Phone { const char* name; char t; float f[3]; float g[3]; float av; float nf, nb, fa, bf, bb, ba, d; };
static const Phone kPhones[] = {
    { "IY", 'V', { 270, 2290, 3010 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 }, { "IH", 'V', { 390, 1990, 2550 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 },
    { "EH", 'V', { 530, 1840, 2480 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 }, { "AE", 'V', { 660, 1720, 2410 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 },
    { "AA", 'V', { 730, 1090, 2440 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 }, { "AO", 'V', { 570, 840, 2410 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 },
    { "UH", 'V', { 440, 1020, 2240 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 }, { "UW", 'V', { 300, 870, 2240 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 },
    { "AH", 'V', { 640, 1190, 2390 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 }, { "ER", 'V', { 490, 1350, 1690 }, {}, -1, 0, 0, 0, 0, 0, 0, 0 },
    { "AX", 'V', { 500, 1500, 2500 }, {}, 0.85f, 0, 0, 0, 0, 0, 0, 0 },
    { "EY", 'V', { 480, 1720, 2520 }, { 330, 2200, 2800 }, -1, 0, 0, 0, 0, 0, 0, 0 }, { "AY", 'V', { 710, 1150, 2450 }, { 360, 2150, 2750 }, -1, 0, 0, 0, 0, 0, 0, 0 },
    { "OW", 'V', { 540, 900, 2350 }, { 380, 800, 2300 }, -1, 0, 0, 0, 0, 0, 0, 0 }, { "AW", 'V', { 700, 1250, 2450 }, { 420, 900, 2300 }, -1, 0, 0, 0, 0, 0, 0, 0 },
    { "OY", 'V', { 560, 860, 2420 }, { 360, 2100, 2700 }, -1, 0, 0, 0, 0, 0, 0, 0 },
    { "L", 'L', { 360, 1000, 2750 }, {}, 0.85f, 0, 0, 0, 0, 0, 0, 55 }, { "R", 'L', { 330, 1060, 1380 }, {}, 0.9f, 0, 0, 0, 0, 0, 0, 55 },
    { "W", 'L', { 290, 610, 2150 }, {}, 0.85f, 0, 0, 0, 0, 0, 0, 55 }, { "Y", 'L', { 260, 2070, 3020 }, {}, 0.85f, 0, 0, 0, 0, 0, 0, 50 },
    { "M", 'N', { 270, 1000, 2200 }, {}, 0.55f, 0, 0, 0, 0, 0, 0, 65 }, { "N", 'N', { 270, 1600, 2600 }, {}, 0.55f, 0, 0, 0, 0, 0, 0, 60 },
    { "NG", 'N', { 270, 2000, 2600 }, {}, 0.55f, 0, 0, 0, 0, 0, 0, 65 },
    { "F", 'F', { 300, 1100, 2100 }, {}, -1, 5000, 5000, 0.1f, 0, 0, 0, 85 }, { "TH", 'F', { 300, 1400, 2600 }, {}, -1, 5500, 5000, 0.072f, 0, 0, 0, 80 },
    { "S", 'F', { 320, 1700, 2600 }, {}, -1, 5600, 2400, 0.22f, 0, 0, 0, 100 }, { "SH", 'F', { 300, 1800, 2400 }, {}, -1, 2700, 1600, 0.48f, 0, 0, 0, 100 },
    { "V", 'Z', { 280, 1100, 2100 }, {}, 0.3f, 5000, 5000, 0.045f, 0, 0, 0, 55 }, { "DH", 'Z', { 280, 1400, 2600 }, {}, 0.3f, 5500, 5000, 0.035f, 0, 0, 0, 45 },
    { "Z", 'Z', { 300, 1700, 2600 }, {}, 0.35f, 5600, 2400, 0.14f, 0, 0, 0, 75 }, { "ZH", 'Z', { 300, 1800, 2400 }, {}, 0.35f, 2700, 1600, 0.28f, 0, 0, 0, 75 },
    { "P", 'P', { 220, 900, 2100 }, {}, -1, 0, 0, 0, 1100, 2200, 0.7f, 0 }, { "B", 'B', { 220, 900, 2100 }, {}, -1, 0, 0, 0, 1100, 2200, 0.36f, 0 },
    { "T", 'P', { 250, 1700, 2600 }, {}, -1, 0, 0, 0, 4200, 3000, 0.35f, 0 }, { "D", 'B', { 250, 1700, 2600 }, {}, -1, 0, 0, 0, 4200, 3000, 0.22f, 0 },
    { "K", 'P', { 280, 1900, 2300 }, {}, -1, 0, 0, 0, 2100, 1500, 0.5f, 0 }, { "G", 'B', { 280, 1900, 2300 }, {}, -1, 0, 0, 0, 2100, 1500, 0.28f, 0 },
    { "CH", 'C', { 300, 1800, 2400 }, {}, -1, 0, 0, 0, 2800, 2000, 0.39f, 0 }, { "JH", 'J', { 300, 1800, 2400 }, {}, -1, 0, 0, 0, 2800, 2000, 0.28f, 0 },
    { "HH", 'H', { 500, 1500, 2500 }, {}, -1, 0, 0, 0, 0, 0, 0, 55 },
};
constexpr int kNumPhones = (int)(sizeof(kPhones) / sizeof(kPhones[0]));
inline int phoneId(const std::string& n)
{
    for (int i = 0; i < kNumPhones; ++i) if (n == kPhones[i].name) return i;
    return -1;
}
inline int idAX() { static const int i = phoneId("AX"); return i; }
inline int idSH() { static const int i = phoneId("SH"); return i; }
inline int idZH() { static const int i = phoneId("ZH"); return i; }
inline bool isVowel(int p) { return p >= 0 && kPhones[p].t == 'V'; }

struct Syllable {
    bool rest = false, extend = false;
    std::vector<int> on, co;
    int nu = -1;
    int word = 0;
    std::string text;   // the word (or mark) it came from, for the window
};

class Lyrics {
public:
    static std::vector<Syllable> parse(const std::string& txt)
    {
        std::vector<Syllable> out;
        std::string s;
        for (char c : txt) s += (char)std::tolower((unsigned char)c);
        int wi = 0;
        size_t i = 0;
        while (i < s.size())
        {
            const char c = s[i];
            if (c == '[')
            {
                const size_t e = s.find(']', i);
                const std::string tok = s.substr(i, e == std::string::npos ? std::string::npos : e - i + 1);
                i = e == std::string::npos ? s.size() : e + 1;
                std::vector<int> ph; std::string cur;
                auto flush = [&]() {
                    std::string u; for (char x : cur) if (!std::isdigit((unsigned char)x)) u += (char)std::toupper((unsigned char)x);
                    if (u == "-") ph.push_back(-2); else { const int id = phoneId(u); if (id >= 0) ph.push_back(id); }
                    cur.clear();
                };
                for (size_t k = 1; k < tok.size(); ++k) { const char x = tok[k]; if (x == ']' || x == ' ' || x == ',' || x == '\t' || x == '\n') { if (!cur.empty()) flush(); } else cur += x; }
                addWord(out, ph, tok, wi++);
                continue;
            }
            if (std::strchr(".,;:!?_", c) && c) { Syllable r; r.rest = true; r.word = wi; r.text = std::string(1, c); out.push_back(r); ++i; continue; }
            if (c == '~') { Syllable r; r.extend = true; r.word = wi; r.text = "~"; out.push_back(r); ++i; continue; }
            if ((c >= 'a' && c <= 'z') || c == '\'')
            {
                size_t e = i;
                while (e < s.size())
                {
                    if ((s[e] >= 'a' && s[e] <= 'z') || s[e] == '\'') { ++e; continue; }
                    if (s[e] == '-' && e + 1 < s.size() && ((s[e + 1] >= 'a' && s[e + 1] <= 'z') || s[e + 1] == '\'')) { ++e; continue; }
                    break;
                }
                const std::string tok = s.substr(i, e - i);
                i = e;
                std::vector<int> ph;
                size_t a = 0;
                while (a <= tok.size())
                {
                    size_t b = tok.find('-', a); if (b == std::string::npos) b = tok.size();
                    std::string part = tok.substr(a, b - a);
                    while (!part.empty() && part.front() == '\'') part.erase(part.begin());
                    while (!part.empty() && part.back() == '\'') part.pop_back();
                    if (!ph.empty()) ph.push_back(-2);
                    if (!part.empty())
                    {
                        const auto& D = dict();
                        auto it = D.find(part);
                        const std::vector<int> w = it != D.end() ? it->second : rules(part);
                        ph.insert(ph.end(), w.begin(), w.end());
                    }
                    a = b + 1;
                }
                addWord(out, ph, tok, wi++);
                continue;
            }
            ++i;
        }
        return out;
    }

    static std::vector<Syllable> syllabify(const std::vector<int>& ph)
    {
        std::vector<int> V;
        for (int i = 0; i < (int)ph.size(); ++i) if (isVowel(ph[(size_t)i])) V.push_back(i);
        std::vector<Syllable> syl;
        if (ph.empty()) return syl;
        if (V.empty())   // no vowel (hmm, shh): the most sonorous sound is the nucleus
        {
            auto rank = [](int p) { const char t = kPhones[p].t; return t == 'N' || t == 'L' ? 3 : t == 'Z' ? 2 : t == 'F' ? 1 : 0; };
            int bi = 0;
            for (int i = 0; i < (int)ph.size(); ++i) if (rank(ph[(size_t)i]) > rank(ph[(size_t)bi])) bi = i;
            Syllable s; s.on.assign(ph.begin(), ph.begin() + bi); s.nu = ph[(size_t)bi]; s.co.assign(ph.begin() + bi + 1, ph.end());
            syl.push_back(s);
            return syl;
        }
        static const int ng = phoneId("NG");
        int start = 0;
        for (size_t k = 0; k < V.size(); ++k)
        {
            const int v = V[k];
            int end;
            if (k + 1 >= V.size()) end = (int)ph.size();
            else
            {
                const int cs = V[k + 1] - v - 1;
                end = v + 1 + (cs <= 1 ? 0 : 1);   // one consonant goes to the next syllable; of more, the first stays
                if (cs >= 1 && ph[(size_t)v + 1] == ng) end = std::max(end, v + 2);   // NG never starts one
            }
            Syllable s; s.on.assign(ph.begin() + start, ph.begin() + v); s.nu = ph[(size_t)v]; s.co.assign(ph.begin() + v + 1, ph.begin() + end);
            syl.push_back(s);
            start = end;
        }
        return syl;
    }

    // letter-to-sound: rules "left|MATCH|right|PHONEMES" tried in order. Contexts: # vowels, : any consonants,
    // ^ a consonant, . a voiced consonant, + E I Y, % a suffix (ER E ES ED ING ELY), & a sibilant, @ T S R D L Z N J
    // TH CH SH, _ the word's edge.
    static std::vector<int> rules(const std::string& word)
    {
        const auto& R = ruleTable();
        std::string w = " ";
        for (char c : word) w += (char)std::toupper((unsigned char)c);
        w += " ";
        std::vector<int> out;
        int i = 1;
        while (i < (int)w.size() - 1)
        {
            const char c = w[(size_t)i];
            auto it = R.find(c);
            if (it == R.end()) { ++i; continue; }
            bool hit = false;
            for (const Rule& r : it->second)
            {
                if (w.compare((size_t)i, r.m.size(), r.m) != 0) continue;
                if (!ctxL(w, i - 1, r.l) || !ctxR(w, i + (int)r.m.size(), r.r)) continue;
                out.insert(out.end(), r.p.begin(), r.p.end());
                i += (int)r.m.size(); hit = true; break;
            }
            if (!hit) ++i;
        }
        return out;
    }

private:
    struct Rule { std::string l, m, r; std::vector<int> p; };

    static void addWord(std::vector<Syllable>& out, const std::vector<int>& ph, const std::string& tok, int wi)
    {
        // '-' (-2) is a forced syllable break
        std::vector<int> g;
        auto go = [&]() { if (g.empty()) return; for (auto& s : syllabify(g)) { s.word = wi; s.text = tok; out.push_back(s); } g.clear(); };
        for (int x : ph) { if (x == -2) go(); else g.push_back(x); }
        go();
    }

    static bool cls(char ch, const char* set) { return ch && std::strchr(set, ch) != nullptr; }
    static bool ctxL(const std::string& w, int j, const std::string& pat)
    {
        static const char* VOW = "AEIOU"; static const char* CON = "BCDFGHJKLMNPQRSTVWXYZ";
        auto at = [&](int k) { return k >= 0 && k < (int)w.size() ? w[(size_t)k] : '\0'; };
        auto two = [&](int k) { return k >= 0 && k + 1 < (int)w.size() ? w.substr((size_t)k, 2) : std::string(); };
        for (int k = (int)pat.size() - 1; k >= 0; --k)
        {
            const char s = pat[(size_t)k];
            if (s == '#') { if (j < 0 || !cls(at(j), VOW)) return false; while (j >= 0 && cls(at(j), VOW)) --j; }
            else if (s == ':') { while (j >= 0 && cls(at(j), CON)) --j; }
            else if (s == '^') { if (j < 0 || !cls(at(j), CON)) return false; --j; }
            else if (s == '.') { if (j < 0 || !cls(at(j), "BDVGJLMNRWZ")) return false; --j; }
            else if (s == '+') { if (j < 0 || !cls(at(j), "EIY")) return false; --j; }
            else if (s == '&') { if (j >= 1 && (two(j - 1) == "CH" || two(j - 1) == "SH")) j -= 2; else if (j >= 0 && cls(at(j), "SCGZXJ")) --j; else return false; }
            else if (s == '@') { if (j >= 1 && (two(j - 1) == "TH" || two(j - 1) == "CH" || two(j - 1) == "SH")) j -= 2; else if (j >= 0 && cls(at(j), "TSRDLZNJ")) --j; else return false; }
            else if (s == '_') { if (j >= 0 && at(j) != ' ') return false; --j; }
            else { if (j < 0 || at(j) != s) return false; --j; }
        }
        return true;
    }
    static bool ctxR(const std::string& w, int j, const std::string& pat)
    {
        static const char* VOW = "AEIOU"; static const char* CON = "BCDFGHJKLMNPQRSTVWXYZ";
        const int n = (int)w.size();
        auto at = [&](int k) { return k >= 0 && k < n ? w[(size_t)k] : '\0'; };
        auto sub = [&](int k, int len) { return k >= 0 && k < n ? w.substr((size_t)k, (size_t)len) : std::string(); };
        for (size_t k = 0; k < pat.size(); ++k)
        {
            const char s = pat[k];
            if (s == '#') { if (j >= n || !cls(at(j), VOW)) return false; while (j < n && cls(at(j), VOW)) ++j; }
            else if (s == ':') { while (j < n && cls(at(j), CON)) ++j; }
            else if (s == '^') { if (j >= n || !cls(at(j), CON)) return false; ++j; }
            else if (s == '.') { if (j >= n || !cls(at(j), "BDVGJLMNRWZ")) return false; ++j; }
            else if (s == '+') { if (j >= n || !cls(at(j), "EIY")) return false; ++j; }
            else if (s == '%')
            {
                static const char* const suf[6] = { "ING", "ELY", "ER", "ES", "ED", "E" };
                int got = -1;
                for (int q = 0; q < 6; ++q)
                {
                    const int L = (int)std::strlen(suf[q]);
                    if (sub(j, L) == suf[q] && (at(j + L) == ' ' || q <= 2)) { got = L; break; }
                }
                if (got < 0) return false;
                j += got;
            }
            else if (s == '&') { if (sub(j, 2) == "CH" || sub(j, 2) == "SH") j += 2; else if (j < n && cls(at(j), "SCGZXJ")) ++j; else return false; }
            else if (s == '@') { const std::string t = sub(j, 2); if (t == "TH" || t == "CH" || t == "SH") j += 2; else if (j < n && cls(at(j), "TSRDLZNJ")) ++j; else return false; }
            else if (s == '_') { if (j < n && at(j) != ' ') return false; ++j; }
            else { if (j >= n || at(j) != s) return false; ++j; }
        }
        return true;
    }
    static std::vector<int> phones(const std::string& list)
    {
        std::vector<int> v; std::string cur;
        for (size_t i = 0; i <= list.size(); ++i)
        {
            const char c = i < list.size() ? list[i] : ' ';
            if (std::isspace((unsigned char)c)) { if (!cur.empty()) { const int id = phoneId(cur == "WH" ? "W" : cur); if (id >= 0) v.push_back(id); cur.clear(); } }
            else cur += c;
        }
        return v;
    }
    static std::string trim(const std::string& x) { size_t a = 0, b = x.size(); while (a < b && std::isspace((unsigned char)x[a])) ++a; while (b > a && std::isspace((unsigned char)x[b - 1])) --b; return x.substr(a, b - a); }
    static const std::map<char, std::vector<Rule>>& ruleTable()
    {
        static std::map<char, std::vector<Rule>> R;
        if (!R.empty()) return R;
        // lines start with "X:"; a line that doesn't continues the one before
        std::string src = kRulesSrc, cur; char key = 0;
        std::vector<std::pair<char, std::string>> lines;
        size_t p = 0;
        while (p <= src.size())
        {
            size_t e = src.find('\n', p); if (e == std::string::npos) e = src.size();
            const std::string ln = src.substr(p, e - p);
            if (ln.size() >= 2 && ln[0] >= 'A' && ln[0] <= 'Z' && ln[1] == ':') lines.push_back({ ln[0], ln.substr(2) });
            else if (!lines.empty()) lines.back().second += " " + ln;
            p = e + 1;
        }
        (void)cur; (void)key;
        for (const auto& L : lines)
        {
            const std::string& body = L.second;
            size_t a = 0;
            while (a <= body.size())
            {
                size_t b = body.find(';', a); if (b == std::string::npos) b = body.size();
                const std::string x = trim(body.substr(a, b - a));
                a = b + 1;
                if (x.empty()) continue;
                std::vector<std::string> f; size_t q = 0;
                while (true) { size_t r = x.find('|', q); if (r == std::string::npos) { f.push_back(x.substr(q)); break; } f.push_back(x.substr(q, r - q)); q = r + 1; }
                while (f.size() < 4) f.push_back("");
                Rule r { trim(f[0]), trim(f[1]), trim(f[2]), phones(trim(f[3])) };
                R[L.first].push_back(r);
            }
        }
        return R;
    }
    static const std::map<std::string, std::vector<int>>& dict()
    {
        static std::map<std::string, std::vector<int>> D;
        if (!D.empty()) return D;
        std::string s; for (const char* c = kDictSrc; *c; ++c) if (*c != '\n') s += *c;
        size_t a = 0;
        while (a <= s.size())
        {
            size_t b = s.find('|', a); if (b == std::string::npos) b = s.size();
            const std::string e = s.substr(a, b - a);
            const size_t k = e.find(':');
            if (k != std::string::npos && k > 0) D[trim(e.substr(0, k))] = phones(trim(e.substr(k + 1)));
            a = b + 1;
        }
        return D;
    }
};

}} // namespace pump::sing
