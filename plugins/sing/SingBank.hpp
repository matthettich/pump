// pump SING: voicebanks. Framework-free.
//
// A voicebank in the oto.ini voicebank layout: a folder of recorded syllables (.wav) and an oto.ini that names them, one line each:
//     file.wav=alias,offset,consonant,cutoff,preutterance,overlap        (milliseconds)
// offset: where the syllable starts in the file; consonant: how much after that is played as recorded (the fixed
// part); cutoff: where it ends (negative: that far after the offset; positive: that far before the end of the file);
// preutterance, overlap: how it is timed against the note and the last one. Banks keep oto.ini in Shift-JIS (most
// Japanese ones) or UTF-8, often one per subfolder (each a pitch or a style): all of them are read. Aliases are kana
// ("か"), romaji ("ka"), and either with "- " or "a " in front (a syllable at the start of a phrase, or after "a": VCV
// banks).
//
// Singing one: SING's syllables become romaji (the onset consonant and the vowel, spelled the Japanese way: shi, chi,
// tsu, fu...) and the aliases are tried in order: the word as typed, VCV after the last vowel, phrase-initial, plain,
// each in romaji and in hiragana; then the vowel alone. A syllable the bank doesn't have is sung by the formant voice.
// A voice plays its sample by PSOLA (pitch-synchronous overlap-add): the recording's pitch periods are found (marks at
// each period's peak; 5 ms apart where it is unvoiced), and grains two periods long are laid down at the note's period,
// so the pitch changes and the formants (the singer's vowel and timbre) stay; GENDER resamples the grains, moving the
// formants alone. The consonant plays through at its own speed; then the vowel is held as long as the note by going
// back and forth over its steady middle; at note-off it plays on to its end and fades.
#pragma once

#include "dsp/Util.hpp"
#include "dsp/Wav.hpp"
#include "SingText.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace pump { namespace sing {

// ---- text: Shift-JIS kana to UTF-8, katakana to hiragana, romaji to hiragana ---------------------------------------
inline bool validUtf8(const std::string& s)
{
    size_t i = 0;
    while (i < s.size())
    {
        const unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + (size_t)n >= s.size() + (n ? 0 : 1)) return false;
        for (int k = 1; k <= n; ++k) if (i + (size_t)k >= s.size() || ((unsigned char)s[i + (size_t)k] >> 6) != 2) return false;
        i += (size_t)n + 1;
    }
    return true;
}
inline void putUtf8(std::string& o, uint32_t cp)
{
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}
// the kana and the few marks banks use; anything else double-byte becomes '?'
inline std::string sjisToUtf8(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { o += (char)c; continue; }
        if (c >= 0xA1 && c <= 0xDF) { o += '?'; continue; }   // half-width katakana: rare in banks
        if (i + 1 >= s.size()) break;
        const unsigned w = (unsigned)c << 8 | (unsigned char)s[++i];
        if (w >= 0x829F && w <= 0x82F1) putUtf8(o, 0x3041 + (w - 0x829F));
        else if (w >= 0x8340 && w <= 0x8396) putUtf8(o, 0x30A1 + (w - 0x8340) - (w > 0x837F ? 1 : 0));
        else if (w == 0x815B) putUtf8(o, 0x30FC);
        else if (w == 0x8140) o += ' ';
        else o += '?';
    }
    return o;
}
// katakana (U+30A1..U+30F6) to hiragana; everything else as it is
inline std::string toHiragana(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); )
    {
        const unsigned char c = (unsigned char)s[i];
        if (c == 0xE3 && i + 2 < s.size())
        {
            uint32_t cp = ((c & 0x0F) << 12) | (((unsigned char)s[i + 1] & 0x3F) << 6) | ((unsigned char)s[i + 2] & 0x3F);
            if (cp >= 0x30A1 && cp <= 0x30F6) cp -= 0x60;
            putUtf8(o, cp); i += 3;
        }
        else { o += s[i]; ++i; }
    }
    return o;
}
inline const std::map<std::string, std::string>& romajiTable()
{
    static const std::map<std::string, std::string> T = [] {
        std::map<std::string, std::string> m;
        // the gojuon rows in order of their hiragana: (romaji, code point)
        struct R { const char* r; uint32_t cp; };
        static const R rows[] = {
            { "a", 0x3042 }, { "i", 0x3044 }, { "u", 0x3046 }, { "e", 0x3048 }, { "o", 0x304A },
            { "ka", 0x304B }, { "ki", 0x304D }, { "ku", 0x304F }, { "ke", 0x3051 }, { "ko", 0x3053 },
            { "ga", 0x304C }, { "gi", 0x304E }, { "gu", 0x3050 }, { "ge", 0x3052 }, { "go", 0x3054 },
            { "sa", 0x3055 }, { "shi", 0x3057 }, { "su", 0x3059 }, { "se", 0x305B }, { "so", 0x305D },
            { "za", 0x3056 }, { "ji", 0x3058 }, { "zu", 0x305A }, { "ze", 0x305C }, { "zo", 0x305E },
            { "ta", 0x305F }, { "chi", 0x3061 }, { "tsu", 0x3064 }, { "te", 0x3066 }, { "to", 0x3068 },
            { "da", 0x3060 }, { "de", 0x3067 }, { "do", 0x3069 },
            { "na", 0x306A }, { "ni", 0x306B }, { "nu", 0x306C }, { "ne", 0x306D }, { "no", 0x306E },
            { "ha", 0x306F }, { "hi", 0x3072 }, { "fu", 0x3075 }, { "he", 0x3078 }, { "ho", 0x307B },
            { "ba", 0x3070 }, { "bi", 0x3073 }, { "bu", 0x3076 }, { "be", 0x3079 }, { "bo", 0x307C },
            { "pa", 0x3071 }, { "pi", 0x3074 }, { "pu", 0x3077 }, { "pe", 0x307A }, { "po", 0x307D },
            { "ma", 0x307E }, { "mi", 0x307F }, { "mu", 0x3080 }, { "me", 0x3081 }, { "mo", 0x3082 },
            { "ya", 0x3084 }, { "yu", 0x3086 }, { "yo", 0x3088 },
            { "ra", 0x3089 }, { "ri", 0x308A }, { "ru", 0x308B }, { "re", 0x308C }, { "ro", 0x308D },
            { "wa", 0x308F }, { "wo", 0x3092 }, { "n", 0x3093 }, { "vu", 0x3094 },
        };
        for (const R& r : rows) { std::string s; putUtf8(s, r.cp); m[r.r] = s; }
        // the small-ya combinations: ki + small ya = kya, ...
        struct Y { const char* r; const char* base; };
        static const Y ys[] = { { "ky", "ki" }, { "gy", "gi" }, { "sh", "shi" }, { "j", "ji" }, { "ch", "chi" }, { "ny", "ni" }, { "hy", "hi" }, { "by", "bi" }, { "py", "pi" }, { "my", "mi" }, { "ry", "ri" } };
        static const uint32_t small[3] = { 0x3083, 0x3085, 0x3087 };
        static const char* const sv[3] = { "a", "u", "o" };
        for (const Y& y : ys) for (int k = 0; k < 3; ++k) { std::string s = m[y.base]; putUtf8(s, small[k]); m[std::string(y.r) + sv[k]] = s; }
        return m;
    }();
    return T;
}
inline std::string romajiToHiragana(const std::string& r)
{
    const auto& T = romajiTable();
    const auto it = T.find(r);
    return it == T.end() ? std::string() : it->second;
}

// SING's phonemes as a romaji syllable: the onset consonant (the last one before the vowel) and the vowel
inline std::string syllableRomaji(const Syllable& s, char& vowelOut)
{
    vowelOut = 0;
    if (s.nu < 0) return {};
    const std::string v = kPhones[s.nu].name;
    char vw = 'a';
    if (v == "IY" || v == "IH") vw = 'i';
    else if (v == "EH" || v == "AE" || v == "EY") vw = 'e';
    else if (v == "AO" || v == "OW" || v == "OY") vw = 'o';
    else if (v == "UH" || v == "UW" || v == "ER") vw = 'u';
    vowelOut = vw;
    std::string c;
    if (!s.on.empty())
    {
        const std::string p = kPhones[s.on.back()].name;
        static const std::map<std::string, std::string> cm = { { "K", "k" }, { "G", "g" }, { "S", "s" }, { "Z", "z" }, { "SH", "sh" }, { "ZH", "j" }, { "T", "t" },
            { "D", "d" }, { "CH", "ch" }, { "JH", "j" }, { "N", "n" }, { "NG", "n" }, { "HH", "h" }, { "F", "f" }, { "M", "m" }, { "Y", "y" }, { "R", "r" },
            { "L", "r" }, { "W", "w" }, { "P", "p" }, { "B", "b" }, { "V", "b" }, { "TH", "s" }, { "DH", "z" } };
        const auto it = cm.find(p);
        if (it != cm.end()) c = it->second;
    }
    std::string r = c + vw;
    // the Japanese spellings
    static const std::map<std::string, std::string> fix = { { "si", "shi" }, { "zi", "ji" }, { "ti", "chi" }, { "tu", "tsu" }, { "di", "ji" }, { "du", "zu" },
        { "hu", "fu" }, { "fa", "ha" }, { "fi", "hi" }, { "fe", "he" }, { "fo", "ho" }, { "wi", "i" }, { "wu", "u" }, { "we", "e" }, { "yi", "i" }, { "ye", "e" },
        { "sha", "sha" }, { "shu", "shu" }, { "sho", "sho" }, { "she", "se" }, { "je", "ze" }, { "che", "te" } };
    const auto f = fix.find(r);
    if (f != fix.end()) r = f->second;
    return r;
}

// ---- the bank ------------------------------------------------------------------------------------------------------
struct Sample {
    std::vector<float> x;
    double sr = 44100.0;
    std::vector<int> marks;          // pitch marks (sample positions)
    std::vector<float> period;       // the period at each mark (samples); unvoiced: 5 ms
    std::vector<char> voiced;
};
struct Entry {
    std::string alias;
    int sample = -1;
    double offset = 0, consonant = 0, end = 0, preutter = 0, overlap = 0;   // samples in the file
};

class Bank {
public:
    std::string name, error;
    std::vector<Sample> samples;
    std::vector<Entry> entries;
    std::map<std::string, int> byAlias;

    bool ok() const { return !entries.empty(); }
    const Entry* find(const std::string& a) const { const auto it = byAlias.find(a); return it == byAlias.end() ? nullptr : &entries[(size_t)it->second]; }
    // the entry to sing a syllable with (prev: the last vowel sung, 0 at a phrase start), or nullptr
    const Entry* lookup(const Syllable& s, char prev, std::string* tried = nullptr) const
    {
        char vw = 0;
        const std::string r = syllableRomaji(s, vw);
        std::vector<std::string> cand;
        std::string word = s.text; for (char& c : word) c = (char)std::tolower((unsigned char)c);
        if (!word.empty())
        {
            // the word as typed (romaji or kana, if the lyrics are written for the bank), and its VCV forms
            if (prev) cand.push_back(std::string(1, prev) + " " + word);
            cand.push_back("- " + word);
            cand.push_back(word);
        }
        if (!r.empty())
        {
            if (prev) cand.push_back(std::string(1, prev) + " " + r);
            cand.push_back("- " + r);
            cand.push_back(r);
        }
        if (vw) { if (prev) cand.push_back(std::string(1, prev) + " " + std::string(1, vw)); cand.push_back("- " + std::string(1, vw)); cand.push_back(std::string(1, vw)); }
        for (const std::string& c : cand)
        {
            if (const Entry* e = find(c)) { if (tried) *tried = c; return e; }
            // the same in hiragana (and "- " / "a " in front kept)
            const size_t sp = c.find(' ');
            const std::string head = sp == std::string::npos ? "" : c.substr(0, sp + 1), tail = sp == std::string::npos ? c : c.substr(sp + 1);
            const std::string hk = romajiToHiragana(tail);
            if (hk.empty()) continue;
            std::string hh = head;
            if (head.size() == 2 && head[0] != '-') hh = romajiToHiragana(std::string(1, head[0])) + " ";
            if (const Entry* e = find(hh + hk)) { if (tried) *tried = hh + hk; return e; }
        }
        return nullptr;
    }

    // a folder (or its oto.ini): every oto.ini in it and its subfolders, two levels down
    static std::shared_ptr<Bank> load(std::string path)
    {
        auto b = std::make_shared<Bank>();
        struct stat st {};
        if (stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) { const size_t sl = path.find_last_of("/\\"); path = sl == std::string::npos ? "." : path.substr(0, sl); }
        b->name = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
        // character.txt: name=
        {
            std::ifstream f(path + "/character.txt", std::ios::binary);
            std::string line;
            while (std::getline(f, line)) { if (line.rfind("name=", 0) == 0) { std::string n = line.substr(5); while (!n.empty() && (n.back() == '\r' || n.back() == '\n')) n.pop_back(); b->name = validUtf8(n) ? n : sjisToUtf8(n); } }
        }
        std::map<std::string, int> files;
        b->scan(path, 0, files);
        if (b->entries.empty() && b->error.empty()) b->error = "no oto.ini entries found";
        return b;
    }

private:
    void scan(const std::string& dir, int depth, std::map<std::string, int>& files)
    {
        readOto(dir, files);
        if (depth >= 2) return;
        if (DIR* d = opendir(dir.c_str()))
        {
            std::vector<std::string> subs;
            while (dirent* e = readdir(d))
            {
                if (e->d_name[0] == '.') continue;
                const std::string p = dir + "/" + e->d_name;
                struct stat st {};
                if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) subs.push_back(p);
            }
            closedir(d);
            std::sort(subs.begin(), subs.end());
            for (const auto& s : subs) scan(s, depth + 1, files);
        }
    }
    void readOto(const std::string& dir, std::map<std::string, int>& files)
    {
        std::ifstream f(dir + "/oto.ini", std::ios::binary);
        if (!f) return;
        std::stringstream ss; ss << f.rdbuf();
        std::string text = ss.str();
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text = text.substr(3);
        if (!validUtf8(text)) text = sjisToUtf8(text);
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string file = line.substr(0, eq);
            std::vector<std::string> v; std::string cur;
            for (char c : line.substr(eq + 1)) { if (c == ',') { v.push_back(cur); cur.clear(); } else cur += c; }
            v.push_back(cur);
            while (v.size() < 6) v.push_back("0");
            // the sample
            const std::string fp = dir + "/" + file;
            int si;
            const auto it = files.find(fp);
            if (it != files.end()) si = it->second;
            else
            {
                const WavData w = readWav(fp, (size_t)(44100.0 * 20.0));
                if (!w.ok()) { si = -1; files[fp] = -1; }
                else
                {
                    Sample s; s.sr = w.sampleRate;
                    const size_t n = w.channels[0].size();
                    s.x.assign(n, 0.0f);
                    for (const auto& c : w.channels) for (size_t i = 0; i < n; ++i) s.x[i] += c[i] / (float)w.channels.size();
                    analyse(s);
                    si = (int)samples.size();
                    samples.push_back(std::move(s));
                    files[fp] = si;
                }
            }
            if (si < 0) continue;
            const Sample& S = samples[(size_t)si];
            const double ms = S.sr / 1000.0;
            Entry e;
            e.alias = toHiragana(v[0].empty() ? file.substr(0, file.find_last_of('.')) : v[0]);
            e.sample = si;
            e.offset = std::max(0.0, std::atof(v[1].c_str()) * ms);
            e.consonant = std::max(0.0, std::atof(v[2].c_str()) * ms);
            const double cut = std::atof(v[3].c_str()) * ms;
            e.end = cut < 0.0 ? e.offset - cut : (double)S.x.size() - cut;
            e.end = clampv(e.end, e.offset + e.consonant + 0.03 * S.sr, (double)S.x.size());
            e.preutter = std::max(0.0, std::atof(v[4].c_str()) * ms);
            e.overlap = std::max(0.0, std::atof(v[5].c_str()) * ms);
            if (byAlias.count(e.alias)) continue;   // the first of an alias wins (a bank's main pitch is usually first)
            byAlias[e.alias] = (int)entries.size();
            entries.push_back(e);
        }
    }
    // pitch marks: the period every 10 ms by autocorrelation (60 - 1000 Hz), then a mark at the peak of each period
    static void analyse(Sample& s)
    {
        // level: about -18 dBFS RMS where it sounds
        double e = 0.0; size_t cnt = 0;
        for (float v : s.x) if (std::fabs(v) > 1e-3f) { e += (double)v * v; ++cnt; }
        if (cnt) { const float g = (float)std::min(20.0, 0.125 / std::sqrt(e / cnt)); for (float& v : s.x) v *= g; }
        const int n = (int)s.x.size(), hop = std::max(1, (int)(0.01 * s.sr)), win = (int)(0.03 * s.sr);
        const int lo = (int)(s.sr / 1000.0), hi = (int)(s.sr / 60.0);
        std::vector<double> per, vo;
        for (int c = 0; c + win + hi < n; c += hop)
        {
            double best = 0.0, e0 = 1e-12; int bl = 0;
            for (int i = 0; i < win; ++i) e0 += (double)s.x[(size_t)(c + i)] * s.x[(size_t)(c + i)];
            for (int L = lo; L <= hi; ++L)
            {
                double acc = 0.0, e1 = 1e-12;
                for (int i = 0; i < win; i += 2) { acc += (double)s.x[(size_t)(c + i)] * s.x[(size_t)(c + i + L)]; e1 += (double)s.x[(size_t)(c + i + L)] * s.x[(size_t)(c + i + L)]; }
                const double r = acc / std::sqrt(e0 * 0.5 * e1);
                if (r > best) { best = r; bl = L; }
            }
            per.push_back(bl); vo.push_back(best > 0.6 && e0 / win > 1e-6 ? 1.0 : 0.0);
        }
        int p = 0;
        while (p < n)
        {
            const int f = clampv(p / hop, 0, std::max(0, (int)per.size() - 1));
            const bool v = !per.empty() && vo[(size_t)f] > 0.5;
            const int T = v ? (int)per[(size_t)f] : (int)(0.005 * s.sr);
            // voiced: the mark at the largest sample in the period ahead
            int m = p;
            if (v) { float pk = -1.0f; for (int i = p; i < std::min(n, p + T); ++i) if (std::fabs(s.x[(size_t)i]) > pk) { pk = std::fabs(s.x[(size_t)i]); m = i; } }
            if (!s.marks.empty() && m <= s.marks.back()) m = s.marks.back() + std::max(1, T / 2);
            if (m >= n) break;
            s.marks.push_back(m); s.period.push_back((float)std::max(8, T)); s.voiced.push_back(v ? 1 : 0);
            p = m + std::max(8, T) / 2 + 1;
            if (v) p = m + (int)(0.6 * T);
        }
    }
};

// ---- a voice playing an entry -----------------------------------------------------------------------------------
class BankVoice {
public:
    void prepare(double sampleRate) { sr = sampleRate; reset(); }
    void reset() { active = false; held = false; e = nullptr; olaN = 0; olaR = 0.0; nextMark = 0.0; env = 0.0; fade = 1.0; }
    bool active = false, held = false;
    int note = -1;
    uint64_t age = 0;
    double detune = 0.0, gl = 1.0, gr = 1.0, gain = 1.0;
    std::string alias;

    void on(const Bank* b, const Entry* en, double hz)
    {
        bank = b; e = en; target = hz; active = true; held = true; env = 0.0; fade = 1.0;
        const Sample& S = b->samples[(size_t)e->sample];
        ts = e->offset; dir = 1.0; outT = 0.0; nextMark = 0.0; olaR = 0.0; olaBase = 0; std::fill(ola, ola + kOla, 0.0f);
        rel = false; dying = false; cleared = 0;
        loopA = e->offset + e->consonant + 0.15 * (e->end - e->offset - e->consonant);
        loopB = e->end - 0.25 * (e->end - e->offset - e->consonant);
        if (loopB - loopA < 0.02 * S.sr) { loopA = std::max(e->offset + e->consonant, e->end - 0.04 * S.sr); loopB = e->end - 0.005 * S.sr; }
    }
    void setPitch(double hz) { target = hz; }
    void off() { held = false; rel = true; }
    void kill() { fade = std::min(fade, 0.999); dying = true; }

    // n samples added in
    void render(float* L, float* R, int n, double bendSemis, double vibSemis, double gender, double attackS, double releaseS)
    {
        if (!active || !e) return;
        const Sample& S = bank->samples[(size_t)e->sample];
        const double step = S.sr / sr;                     // file samples per output sample
        const double hz = target * std::exp2((detune * 0.01 + bendSemis + vibSemis) / 12.0);
        const double Tout = clampv(S.sr / hz, 8.0, 0.05 * S.sr);   // the period we lay grains at (file samples)
        const double fs = std::exp2(clampv(gender, -1.0, 1.0) * 0.5);   // grain resampling: formants up (>1) or down
        const double aI = 1.0 / std::max(1.0, attackS * sr), rI = 1.0 / std::max(1.0, releaseS * sr);
        for (int i = 0; i < n; ++i)
        {
            // lay down grains until the buffer is filled ahead of where we read
            while (nextMark < olaR + kAhead) grain(S, Tout, fs);
            const int ir = (int)olaR - olaBase; const double fr = olaR - std::floor(olaR);
            const double y = ola[ir & (kOla - 1)] * (1.0 - fr) + ola[(ir + 1) & (kOla - 1)] * fr;
            // clear what has been read
            const int clearTo = (int)std::floor(olaR) - 1;
            while (cleared < clearTo) { ola[(cleared - olaBase) & (kOla - 1)] = 0.0f; ++cleared; }
            olaR += step;
            const double tgt = rel ? 0.0 : 1.0;
            env = env < tgt ? std::min(tgt, env + aI) : std::max(tgt, env - (rel ? rI : 0.0));
            if (dying) { fade -= 1.0 / (0.03 * sr); if (fade <= 0.0) { active = false; return; } }
            const double o = y * env * fade * gain;
            L[i] += (float)(o * gl); R[i] += (float)(o * gr);
            if (rel && (env <= 1e-4 || (ts >= e->end && olaR >= nextMark))) { active = false; return; }
        }
    }
    double level() const { return env * fade; }

private:
    static constexpr int kOla = 1 << 14, kAhead = 1024;
    // one grain: the analysis mark nearest the source time, windowed over two of its periods, at the next output mark
    void grain(const Sample& S, double Tout, double fs)
    {
        const auto& M = S.marks;
        if (M.empty() || ts >= e->end) { nextMark += Tout; return; }   // past its end: silence
        // the source time: through the consonant at its own speed, then back and forth over the vowel's middle
        size_t k = (size_t)(std::lower_bound(M.begin(), M.end(), (int)ts) - M.begin());
        if (k >= M.size()) k = M.size() - 1;
        if (k > 0 && std::fabs(M[k - 1] - ts) < std::fabs(M[k] - ts)) --k;
        const bool v = S.voiced[k] != 0;
        const double Pa = S.period[k];
        const double T = v ? Tout : Pa;                     // unvoiced: keep its own spacing
        const int half = (int)std::min(Pa * 1.0 / std::max(0.5, fs), 0.02 * S.sr);
        const double c = M[k];
        // a voiced grain is weighted by T / Pa, so the level doesn't change with the pitch (closer grains overlap more)
        const double wt = v ? clampv(T / std::max(1.0, Pa), 0.25, 4.0) : 1.0;
        // overlap-add, resampled by fs (formant shift): the grain's sample j reads the source at c + j * fs
        for (int j = -half; j <= half; ++j)
        {
            const double w = 0.5 + 0.5 * std::cos(kPi * j / (half + 1));
            const double sp = c + j * fs;
            const int i0 = (int)std::floor(sp); const double fr = sp - i0;
            if (i0 < 0 || i0 + 1 >= (int)S.x.size()) continue;
            const double xv = S.x[(size_t)i0] * (1.0 - fr) + S.x[(size_t)i0 + 1] * fr;
            const int at = (int)std::lround(nextMark) + j;
            if (at < cleared) continue;
            ola[(at - olaBase) & (kOla - 1)] += (float)(xv * w * wt);
        }
        // on: output time by T, source time by the same (1:1) through the consonant, then held over the loop
        nextMark += T;
        if (!rel && ts >= loopA)
        {
            ts += dir * T;
            if (ts > loopB) { ts = loopB; dir = -1.0; }
            if (ts < loopA) { ts = loopA; dir = 1.0; }
        }
        else ts += T;
    }

    const Bank* bank = nullptr;
    const Entry* e = nullptr;
    double sr = 48000.0, target = 440.0, ts = 0.0, dir = 1.0, outT = 0.0, loopA = 0.0, loopB = 0.0, env = 0.0, fade = 1.0;
    double nextMark = 0.0, olaR = 0.0;
    int olaN = 0, olaBase = 0, cleared = 0;
    bool rel = false, dying = false;
    float ola[kOla] = {};
};

}} // namespace pump::sing
