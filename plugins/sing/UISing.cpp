// pump SING: interface. LYRICS (the words; a dot per syllable, the one being sung lit and the next one marked; click
// or EDIT to type new lyrics, RESTART goes back to the first syllable), VOWELS (the voice's first two formants on the
// vowel chart, live), SCOPE. Below: the voice, its tone and the choir; vibrato, the envelope and the output; a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginSing.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::sing;

class UISing : public PumpUI {
public:
    UISing() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::sing(), "SING", 1000, 694, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Medium, bi); if (tip) d->tip = tip; return d; };
        octD = dial(kOctave, "OCTAVE", true);
        sizeD = dial(kSize, "SIZE", true, "The singer's size: + smaller (younger, higher formants), - bigger (deeper)");
        robotD = dial(kRobot, "ROBOT", false, "The source: a smooth throat (0) to a buzzy machine (100)");
        voiceD = dial(kVoice, "VOICE", false, "Voiced (100) to a whisper (0)");
        breathD = dial(kBreath, "BREATH", false, "Air in the voice");
        roughD = dial(kRough, "ROUGH", false, "Rasp and growl: an unsteady, rough throat");
        brightD = dial(kBright, "BRIGHT", false, "The source's brightness");
        softD = dial(kSoft, "SOFT", false, "Wider, softer formants (100) or narrow, ringing ones (0)");
        clarD = dial(kClarity, "CLARITY", false, "Brings out the formants that tell the vowels apart");
        hissD = dial(kHiss, "CONSONANT", false, "How loud the consonants' noise is (S, SH, T, K...)");
        dictD = dial(kDiction, "DICTION", false, "Faster (more) or slower (less) consonants");
        driveD = dial(kDrive, "DRIVE");
        modeSeg = addSegmented(kMode, { "MONO", "POLY" });
        modeSeg->tip = "Mono: overlapping notes slide to the next syllable · Poly: chords sing one syllable together";
        choirD = dial(kChoir, "VOICES", false, "Singers per note: 1 solo, 2 a duet, more a choir");
        spreadD = dial(kSpread, "SPREAD", false, "How differently the choir's singers sing (pitch, size, vibrato)");
        widthD = dial(kWidth, "WIDTH", false, "The choir across the stereo field");
        vibD = dial(kVib, "VIBRATO");
        vibRateD = dial(kVibRate, "RATE");
        vibDelayD = dial(kVibDelay, "DELAY", false, "Vibrato comes in after this long");
        wheelD = dial(kWheel, "WHEEL", false, "The mod wheel adds this much vibrato");
        atkD = dial(kAttack, "ATTACK");
        relD = dial(kRelease, "RELEASE");
        glideD = dial(kGlide, "GLIDE", false, "How long a slide between legato notes takes");
        velD = dial(kVelocity, "VELOCITY", false, "How much velocity changes the level");
        volD = dial(kVolume, "VOLUME", true);
        engSeg = addSegmented(kEngine, { "FORMANT", "VOICEBANK" });
        engSeg->tip = "Formant: SING's own voice. Voicebank: a recorded singer (the oto.ini voicebank layout) sings what it has, the formant voice the rest";
        genderD = dial(kGender, "GENDER", true, "Voicebank: moves the singer's formants up (+) or down (-) without changing the pitch");
        loadBank = addButton("LOAD BANK", [this] { FileBrowserOptions op; op.title = "A voicebank: choose its oto.ini (or any file in its folder)"; openFileBrowser(op); });
        loadBank->tip = "Load a voicebank: a folder of recorded syllables with an oto.ini (the oto.ini voicebank layout)";
        editBtn = addButton("EDIT", [this]() { editLyrics(); });
        editBtn->small = true; editBtn->tip = "Type new lyrics (Enter keeps them, Esc cancels; Cmd/Ctrl+V pastes)";
        restartBtn = addButton("RESTART", [this]() { if (plug) plug->restartRequest.store(true); });
        restartBtn->small = true; restartBtn->tip = "Back to the first syllable (also when the host starts playing)";

        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginSing*>(pluginPtr())) plug = p;
        if (const char* t = pump::testEnv("PUMP_TEST_LYRICS")) setText(t); else setText(kDefaultLyrics);
    }
    ~UISing() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kChoir) { const int n = (int)std::lround(v); if (n <= 1) return "SOLO"; std::snprintf(b, sizeof b, "%d", n); return b; }
        if ((i == kGlide || i == kVibDelay) && v < 0.5f) return "OFF";
        return PumpUI::formatParam(i, v);
    }
    void onExtraState(const char* key, const char* value) override { if (std::strcmp(key, "lyrics") == 0) setText(value ? value : ""); }

    void setText(const std::string& t) { text = t; syl = Lyrics::parse(t); }
    void editLyrics()
    {
        openNameEntry(Rect(lyrR.x + 8, lyrR.b() - 34, lyrR.w - 16, 26), text, [this](const std::string& t) {
            setText(t);
            setState("lyrics", t.c_str());
            if (plug) plug->restartRequest.store(true);
        }, 4000);
    }

    void layout() override
    {
        const float top = 58, sh = 150, hm = Dial::heightFor(Dial::Medium);
        lyrR = Rect(14, top, 560, sh);
        editBtn->r = Rect(lyrR.r() - 150, top + 6, 66, 18); restartBtn->r = Rect(lyrR.r() - 78, top + 6, 70, 18);
        vowR = Rect(586, top, 212, sh);
        scopeR = Rect(810, top, 150, sh);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);

        // three strips of panels, one row of dials each, the dials spread evenly
        const float ph = 116;
        auto rowOf = [&](Rect& panel, float x, float y, float w, float dx, Dial** d, int n) {
            panel = Rect(x, y, w, ph);
            const float cw = (w - 20 - dx) / n;
            for (int k = 0; k < n; ++k) d[k]->r = Rect(x + 10 + dx + k * cw, y + 28, cw, hm);
        };
        const float y1 = top + sh + 12;
        Dial* va[6] = { octD, sizeD, robotD, voiceD, breathD, roughD };   rowOf(voicePanel, 14, y1, 480, 0, va, 6);
        Dial* ta[6] = { brightD, softD, clarD, hissD, dictD, driveD };    rowOf(tonePanel, 506, y1, 480, 0, ta, 6);
        const float y2 = y1 + ph + 12;
        Dial* vv[4] = { vibD, vibRateD, vibDelayD, wheelD };              rowOf(vibPanel, 14, y2, 480, 0, vv, 4);
        Dial* ee[4] = { atkD, relD, glideD, velD };                       rowOf(envPanel, 506, y2, 480, 0, ee, 4);
        // SINGERS: mono/poly beside its three dials; OUTPUT: engine, LOAD BANK and the bank beside volume and gender
        const float y3 = y2 + ph + 12;
        Dial* ca[3] = { choirD, spreadD, widthD };                         rowOf(choirPanel, 14, y3, 480, 156, ca, 3);
        modeSeg->r = Rect(26, y3 + 56, 144, 24);
        Dial* oo[2] = { volD, genderD };                                   rowOf(outPanel, 506, y3, 480, 236, oo, 2);
        engSeg->r = Rect(518, y3 + 32, 224, 24);
        loadBank->r = Rect(518, y3 + 64, 96, 20);
        keyboard.r = Rect(14, y3 + ph + 12, 966, 56);
    }

    void tick(float dt) override
    {
        spreadD->enabled = widthD->enabled = value(kChoir) > 1.5f;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        cur = plug->engine.syllableNow(); next = plug->engine.nextSyllable();
        const float lv = plug->engine.levelNow();
        level += (lv - level) * std::min(1.0f, dt * 20.0f);
        if (lv > 0.02f)
        {
            const float f1 = plug->engine.f1Now(), f2 = plug->engine.f2Now();
            trail[trailW % kTrail][0] = f1; trail[trailW % kTrail][1] = f2; ++trailW;
        }
    }

    void drawBody() override
    {
        drawLyrics();
        drawVowels();
        drawScope();
        drawOutMeter(outMeter, meterL, meterR);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(voicePanel, "VOICE"); panel(tonePanel, "TONE"); panel(choirPanel, "SINGERS");
        panel(vibPanel, "VIBRATO"); panel(envPanel, "ENVELOPE"); panel(outPanel, "OUTPUT");
        // what is being sung, in phonemes
        std::string now = "-";
        if (cur >= 0 && cur < (int)syl.size())
        {
            const Syllable& s = syl[(size_t)cur];
            now.clear();
            for (int p : s.on) { now += kPhones[p].name; now += ' '; }
            if (s.nu >= 0) { now += '['; now += kPhones[s.nu].name; now += "] "; }
            for (int p : s.co) { now += kPhones[p].name; now += ' '; }
        }
        // the voicebank: which, and the recording sung now
        const pump::sing::Bank* bk = plug ? plug->bankNow() : nullptr;
        {
            std::string info = plug ? plug->bankInfo() : std::string();
            if (info.empty()) info = "NO VOICEBANK";
            textAt(outPanel.x + 12, outPanel.y + 96, info, kLabel, 8.5f, bk ? theme.ink : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.3f);
            const int en = plug ? plug->engine.entryNow() : -1;
            if (bk && value(kEngine) >= 0.5f) now = en >= 0 && en < (int)bk->entries.size() ? "\"" + bk->entries[(size_t)en].alias + "\"" : now + "(FORMANT)";
        }
        // what is sung now, along the bottom of the LYRICS screen
        textAt(lyrR.x + 10, lyrR.b() - 14, "SINGING", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        textAt(lyrR.x + 76, lyrR.b() - 14, now, kBold, 12.0f, sungColour >= 0 ? pump::wheelInk(sungColour, theme.dark) : theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
        char b[64];
        std::snprintf(b, sizeof b, "SYLLABLE %d OF %d", cur >= 0 ? cur + 1 : 0, (int)syl.size());
        textAt(lyrR.r() - 10, lyrR.b() - 14, b, kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.4f);
        textAt(choirPanel.r() - 12, choirPanel.y + 16, "KEYS: EVERY NOTE SINGS THE NEXT SYLLABLE", kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);
        keyboard.draw(*this);
    }

    // the words in a flow, each in its own colour from the suite's wheel (as COIL's springs are); a dot per syllable
    // under each, the sung one lit, the next one ringed; the word being sung lights up and turns bold
    static constexpr float kLyricSize = 17.0f;
    void drawLyrics()
    {
        const Rect& r = lyrR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "LYRICS");
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 50);
        save(); scissor(p.x, p.y, p.w, p.h);
        float x = p.x, y = p.y + 16;
        const float lineH = 38.0f;
        int colour = 0;
        sungColour = -1;
        size_t i = 0;
        if (syl.empty()) textAt(p.x, y, "(no lyrics: EDIT to type some)", kMono, 11.0f, theme.dim, ALIGN_LEFT | ALIGN_MIDDLE);
        // keep the line with the next syllable in view
        float scrollY = 0.0f;
        {
            float sx = p.x, sy = 0.0f;
            for (size_t j = 0; j < syl.size(); )
            {
                size_t k = j; while (k < syl.size() && syl[k].word == syl[j].word && !syl[k].rest && !syl[k].extend) ++k; if (k == j) ++k;
                const float w = textWidth(syl[j].text, kMono, kLyricSize) + 16.0f;
                if (sx + w > p.r() && sx > p.x) { sx = p.x; sy += lineH; }
                if ((int)j <= next && next < (int)k) scrollY = std::max(0.0f, sy - lineH);
                sx += w; j = k;
            }
        }
        y -= scrollY;
        while (i < syl.size())
        {
            size_t k = i; while (k < syl.size() && syl[k].word == syl[i].word && !syl[k].rest && !syl[k].extend) ++k; if (k == i) ++k;
            const std::string& word = syl[i].text;
            const float w = textWidth(word, kMono, kLyricSize) + 16.0f;
            if (x + w > p.r() && x > p.x) { x = p.x; y += lineH; }
            const bool sung = cur >= (int)i && cur < (int)k, lit = sung && level > 0.02f;
            const bool plain = syl[i].rest || syl[i].extend;
            const int col = plain ? -1 : colour++;
            if (sung && col >= 0) sungColour = col;
            const Color neon = col >= 0 ? pump::wheelColour(col, false) : theme.dim;
            const Color ink = col >= 0 ? pump::wheelInk(col, theme.dark) : theme.dim;
            const bool done = !plain && (int)k <= cur;
            if (col >= 0)
            {
                const float base = theme.dark ? 0.12f : 0.22f, sungA = (theme.dark ? 0.28f : 0.35f) + (theme.dark ? 0.45f : 0.6f) * std::min(1.0f, level * 3.0f);
                fillRect(Rect(x - 3, y - 13, w - 6, 26), withAlpha2(neon, lit ? sungA : done ? base * 0.5f : base));
            }
            textAt(x + 3, y, word, lit ? kBold : kMono, kLyricSize, ink, ALIGN_LEFT | ALIGN_MIDDLE);
            const int n = (int)(k - i);
            for (int d = 0; d < n; ++d)
            {
                const int idx = (int)i + d;
                const float dx = x + 6 + d * 9.0f, dy = y + 18;
                beginPath(); circle(dx, dy, 2.6f);
                if (idx == cur) { fillColor(col >= 0 ? neon : theme.neon); fill(); strokeColor(ink); strokeWidth(0.8f); stroke(); }
                else { strokeColor(idx == next ? ink : theme.dim); strokeWidth(idx == next ? 1.4f : 0.8f); stroke(); }
            }
            x += w; i = k;
        }
        restore();
    }
    // F1 down, F2 to the left (the vowel chart: front vowels left, open vowels low)
    void drawVowels()
    {
        const Rect& r = vowR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "VOWELS");
        const Rect p(r.x + 12, r.y + 26, r.w - 24, r.h - 36);
        auto X = [&](double f2) { return (float)(p.r() - (std::log(pump::clampv(f2, 600.0, 2800.0) / 600.0) / std::log(2800.0 / 600.0)) * p.w); };
        auto Y = [&](double f1) { return (float)(p.y + (std::log(pump::clampv(f1, 220.0, 900.0) / 220.0) / std::log(900.0 / 220.0)) * p.h); };
        static const struct { const char* l; const char* ph; } marks[] = { { "ee", "IY" }, { "ih", "IH" }, { "eh", "EH" }, { "ae", "AE" }, { "ah", "AA" }, { "aw", "AO" }, { "oo", "UW" }, { "uh", "AH" }, { "er", "ER" }, { "u", "UH" } };
        for (const auto& m : marks)
        {
            const int i = phoneId(m.ph);
            if (i < 0) continue;
            textAt(X(kPhones[i].f[1]), Y(kPhones[i].f[0]), m.l, kMono, 10.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        const uint32_t n = std::min<uint32_t>(trailW, kTrail);
        if (n > 1)
        {
            beginPath();
            for (uint32_t j = 0; j < n; ++j)
            {
                const uint32_t idx = (trailW - n + j) % kTrail;
                const float x = X(trail[idx][1]), y = Y(trail[idx][0]);
                if (j == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(withAlpha2(sungNeon(), 0.7f)); strokeWidth(1.6f); lineJoin(ROUND); stroke();
            const uint32_t last = (trailW - 1) % kTrail;
            beginPath(); circle(X(trail[last][1]), Y(trail[last][0]), 3.0f + 3.0f * std::min(1.0f, level * 3.0f));
            fillColor(sungNeon()); fill();
        }
    }
    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        const int win = 1024;
        int start = kScopeN - win - 1;
        for (int i = start; i > start - 1024 && i > 1; --i) if (scope[i - 1] < 0.0f && scope[i] >= 0.0f) { start = i; break; }
        float mx = 1e-3f; for (int i = 0; i < win; ++i) mx = std::max(mx, std::fabs(scope[start + i]));
        const float want = 0.9f / mx;
        scale = want < scale ? want : scale + (want - scale) * 0.05f;
        save(); scissor(p.x, p.y, p.w, p.h);
        beginPath();
        for (int i = 0; i < win; i += 2)
        {
            const float x = p.x + p.w * i / (win - 1), y = p.cy() - scope[start + i] * scale * p.h * 0.48f;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.6f); lineJoin(ROUND); stroke();
        restore();
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        if (lyrR.contains(x, y)) { editLyrics(); return true; }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override { if (kbDown) keyboard.mouseDrag(x, y); }
    void bodyMouseUp(float, float) override { if (kbDown) { keyboard.mouseUp(); kbDown = false; } }
    void bodyHover(float x, float y) override
    {
        if (lyrR.contains(x, y)) setStatus("Click to type lyrics. A comma or full stop is a rest, ~ holds the last vowel, - splits a word (hel-lo), [HH AX L OW] spells sounds");
        else if (vowR.contains(x, y)) setStatus("The voice's first two formants on the vowel chart: front vowels left, open vowels low");
        else if (keyboard.r.contains(x, y)) setStatus("Play: every note sings the next syllable; drag across keys");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Lyrics", "click the box (or EDIT), type, Enter; Cmd/Ctrl+V pastes" },
                 { ", . ; ! ?", "a rest (that note is silent)" }, { "~", "hold the last vowel (a run over one word)" },
                 { "hel-lo", "split a word where you want the syllables" }, { "[HH AX L OW]", "spell the sounds directly" },
                 { "Mono", "overlapping notes slide to the next syllable" }, { "Poly", "a chord sings one syllable" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   SYLLABLE %d / %d", value(kMode) >= 0.5f ? "POLY" : "MONO", cur >= 0 ? cur + 1 : 0, (int)syl.size());
        return b;
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename) return;
        setState("voicebank", filename);
        setParamGesture(kEngine, (float)kEngineBank);
    }

private:
    static constexpr int kScopeN = 4096, kTrail = 48;
    PluginSing* plug = nullptr;
    Rect lyrR, vowR, scopeR, meterL, meterR, voicePanel, tonePanel, choirPanel, vibPanel, envPanel, outPanel;
    Dial *octD, *sizeD, *robotD, *voiceD, *breathD, *roughD, *brightD, *softD, *clarD, *hissD, *dictD, *driveD;
    Dial *choirD, *spreadD, *widthD, *vibD, *vibRateD, *vibDelayD, *wheelD, *atkD, *relD, *glideD, *velD, *volD, *genderD;
    Segmented* engSeg;
    Button* loadBank;
    Segmented* modeSeg;
    Button *editBtn, *restartBtn;
    KeyboardUI keyboard;
    bool kbDown = false;
    std::string text;
    int sungColour = -1;   // the wheel colour of the word being sung (-1: none): the vowel chart and SINGING follow it
    Color sungNeon() const { return sungColour >= 0 ? pump::wheelColour(sungColour, false) : theme.neon; }
    std::vector<Syllable> syl;
    int cur = -1, next = 0;
    float scope[kScopeN] = {}, scale = 1.0f, level = 0.0f, trail[kTrail][2] = {};
    uint32_t trailW = 0;
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UISing)
};

UI* createUI() { return new UISing(); }

END_NAMESPACE_DISTRHO
