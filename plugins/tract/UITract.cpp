// pump TRACT: interface. TRACT: the newest voice's vocal tract from the glottis (left, glowing as the folds open) to the
// lips (right), as wide as each of its 44 sections, the tongue's narrowing moving with the vowel. VOWEL: A, E, I, O, U
// and where the vowel is now (click or drag to set it). WAVE: the sound. Below: the voice source (tenseness, breath,
// vibrato, jitter), the tract (vowel, motion, size, brightness), the ensemble (singers, detune, their sizes and vowels,
// stereo), the envelope, the voice, and a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginTract.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::tract;

class UITract : public PumpUI {
public:
    UITract() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::tract(), "TRACT", 1000, 738, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small, bi); if (tip) d->tip = tip; return d; };
        tenseD = dial(kTense, "TENSE", false, "Breathy, open folds (0) to a pressed, bright voice (100)");
        breathD = dial(kBreath, "BREATH", false, "Air through the folds: aspiration noise");
        vibD = dial(kVibrato, "VIBRATO"); vrateD = dial(kVibRate, "RATE");
        jitD = dial(kJitter, "JITTER", false, "Each period a little longer or shorter, louder or softer: a living voice");
        vowelD = dial(kVowel, "VOWEL", false, "A (0), E (25), I (50), O (75), U (100)");
        motionD = dial(kMotion, "MOTION", false, "The vowel sways (each singer on its own)"); mrateD = dial(kMotionRate, "RATE");
        sizeD = dial(kSize, "SIZE", false, "The tract's length: a child's (60) to a giant's (150); bigger sounds lower and darker");
        brightD = dial(kBright, "BRIGHT");
        singersD = dial(kSingers, "SINGERS", false, "Voices per note"); detuneD = dial(kDetune, "DETUNE", false, "Cents between the outermost singers");
        sizesD = dial(kSizeSpread, "SIZES", false, "How differently sized the singers are"); vowelsD = dial(kVowelSpread, "VOWELS", false, "How differently voweled the singers are");
        spreadD = dial(kSpread, "STEREO");
        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) envD[k] = dial(kAttack + k, adsr[k]);
        velD = dial(kVelocity, "VELOCITY"); voicesD = dial(kVoices, "VOICES"); glideD = dial(kGlide, "GLIDE");
        octD = dial(kOctave, "OCTAVE", true); volD = dial(kVolume, "VOLUME", true);
        vmodeSeg = addSegmented(kVoiceMode, { "POLY", "MONO", "LEGATO" });
        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginTract*>(pluginPtr())) plug = p;
    }
    ~UITract() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kGlide && v < 0.5f) return "OFF";
        if (i == kVowel)
        {
            const float u = v / 25.0f; const int k = (int)std::lround(u);
            if (std::fabs(u - k) < 0.12f) return kVowelNames[pump::clampv(k, 0, 4)];
            std::snprintf(b, sizeof b, "%s-%s", kVowelNames[pump::clampv((int)u, 0, 3)], kVowelNames[pump::clampv((int)u + 1, 1, 4)]); return b;
        }
        if (i == kDetune) { std::snprintf(b, sizeof b, "%.0f ct", v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, hs = Dial::heightFor(Dial::Small);
        tubeR = Rect(14, top, 620, 220);
        vowelR = Rect(646, top, 314, 104);
        waveR = Rect(646, top + 116, 314, 104);
        meterL = Rect(966, top, 8, 208); meterR = Rect(977, top, 8, 208);
        srcPanel = Rect(14, 290, 472, 116);
        Dial* sd[5] = { tenseD, breathD, vibD, vrateD, jitD };
        for (int k = 0; k < 5; ++k) sd[k]->r = Rect(20 + k * 93, 324, 93, hs);
        trPanel = Rect(498, 290, 488, 116);
        Dial* td[5] = { vowelD, motionD, mrateD, sizeD, brightD };
        for (int k = 0; k < 5; ++k) td[k]->r = Rect(504 + k * 96, 324, 96, hs);
        ensPanel = Rect(14, 416, 472, 116);
        Dial* ed[5] = { singersD, detuneD, sizesD, vowelsD, spreadD };
        for (int k = 0; k < 5; ++k) ed[k]->r = Rect(20 + k * 93, 450, 93, hs);
        envPanel = Rect(498, 416, 488, 116);
        for (int k = 0; k < 4; ++k) envD[k]->r = Rect(504 + k * 120, 450, 120, hs);
        voicePanel = Rect(14, 542, 972, 96);
        Dial* vd[5] = { velD, voicesD, glideD, octD, volD };
        for (int k = 0; k < 5; ++k) vd[k]->r = Rect(22 + k * 120, 564, 120, hs);
        vmodeSeg->r = Rect(700, 584, 276, 22);
        keyboard.r = Rect(14, 650, 966, 56);
    }

    void tick(float dt) override
    {
        voicesD->enabled = (int)value(kVoiceMode) == 0;
        mrateD->enabled = value(kMotion) > 0.5f;
        detuneD->enabled = sizesD->enabled = vowelsD->enabled = spreadD->enabled = value(kSingers) > 1.5f;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; } else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        for (int k = 0; k < kSections; ++k) dia[k] = plug->engine.diameter(k);
        vowelNow = plug->engine.vowelNow();
        glot = std::max(plug->engine.glottisNow(), glot * std::exp(-dt * 12.0f));
        playing = plug->engine.voicesNow() > 0;
        plug->engine.outRing.readLatest(wave, kWave);
    }

    void drawTube()
    {
        const Rect& r = tubeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "TRACT");
        const float size = value(kSize) * 0.01f;
        char b[64]; std::snprintf(b, sizeof b, "%.0f CM  ·  %s", 17.0f * size, formatParam(kVowel, (playing ? vowelNow : value(kVowel) * 0.01f) * 100.0f).c_str());
        textAt(r.r() - 10, r.y + 14, b, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        const float x0 = r.x + 60, x1 = r.r() - 50, cy = r.y + r.h * 0.56f, sc = r.h * 0.22f;
        const Color hot = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        // the walls: upper (palate) and lower (tongue, jaw), the airway between them filled
        auto X = [&](int i) { return x0 + (x1 - x0) * i / (kSections - 1.0f); };
        beginPath();
        for (int i = 0; i < kSections; ++i) { const float y = cy - dia[i] * sc * 0.5f; if (!i) moveTo(X(i), y); else lineTo(X(i), y); }
        for (int i = kSections - 1; i >= 0; --i) lineTo(X(i), cy + dia[i] * sc * 0.5f);
        closePath(); fillColor(withAlpha2(hot, theme.dark ? 0.22f : 0.18f)); fill();
        strokeColor(hot); strokeWidth(2.0f); stroke();
        // the tongue: shade the lower wall where it pushes in
        for (int i = 1; i < kSections; ++i)
        {
            const float rest = i < 7 ? 0.6f + 0.5f * i / 7.0f : (i < 12 ? 1.1f + 0.4f * (i - 7) / 5.0f : 1.5f);
            const float push = pump::clampv((rest - dia[i]) / rest, 0.0f, 1.0f);
            if (push > 0.02f) line(X(i), cy + dia[i] * sc * 0.5f + 2, X(i), cy + rest * sc * 0.5f + 10, withAlpha2(theme.ink, 0.2f + 0.5f * push), 3.0f);
        }
        // the glottis (glowing with the folds' flow) and the lips
        const float g = playing ? pump::clampv(glot, 0.0f, 1.0f) : 0.0f;
        beginPath(); circle(x0 - 22, cy, 12.0f); fillColor(withAlpha2(theme.neon, 0.15f + 0.85f * g)); fill(); strokeColor(theme.ink); strokeWidth(1.5f); stroke();
        textAt(x0 - 22, cy + 30, "GLOTTIS", kLabel, 8.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        textAt(x1 + 22, cy + 30, "LIPS", kLabel, 8.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        line(x1 + 6, cy - dia[kSections - 1] * sc * 0.5f - 6, x1 + 6, cy + dia[kSections - 1] * sc * 0.5f + 6, theme.ink, 2.0f);
    }
    void drawVowel()
    {
        const Rect& r = vowelR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "VOWEL");
        const Rect p(r.x + 24, r.y + 34, r.w - 48, r.h - 50);
        line(p.x, p.cy(), p.r(), p.cy(), theme.dim, 2.0f);
        for (int k = 0; k < 5; ++k)
        {
            const float x = p.x + p.w * k / 4.0f;
            beginPath(); circle(x, p.cy(), 11.0f); fillColor(theme.panel2); fill(); strokeColor(theme.ink); strokeWidth(1.2f); stroke();
            textAt(x, p.cy(), kVowelNames[k], kLabel, 11.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.0f);
        }
        const float v = playing ? vowelNow : value(kVowel) * 0.01f;
        const float x = p.x + p.w * v;
        beginPath(); circle(x, p.cy(), 7.0f); fillColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.4f)); fill();
        const float m = value(kMotion) * 0.01f * 0.5f;
        if (m > 0.01f) { line(p.x + p.w * std::max(0.0f, value(kVowel) * 0.01f - m), p.cy() + 18, p.x + p.w * std::min(1.0f, value(kVowel) * 0.01f + m), p.cy() + 18, theme.ink2, 2.0f); }
    }
    void drawWave()
    {
        const Rect& r = waveR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "WAVE");
        const Rect p(r.x + 8, r.y + 24, r.w - 16, r.h - 30);
        line(p.x, p.cy(), p.r(), p.cy(), theme.grid, 1.0f);
        int st = 0; for (int i = 1; i < kWave / 2; ++i) if (wave[i - 1] <= 0.0f && wave[i] > 0.0f) { st = i; break; }
        float mx = 1e-3f; for (int i = 0; i < kWave; ++i) mx = std::max(mx, std::fabs(wave[i]));
        beginPath();
        const int n = kWave / 2;
        for (int i = 0; i < n; ++i) { const float x = p.x + p.w * i / (n - 1.0f), y = p.cy() - wave[st + i] / mx * p.h * 0.45f; if (!i) moveTo(x, y); else lineTo(x, y); }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.4f); stroke();
    }

    void drawBody() override
    {
        drawTube(); drawVowel(); drawWave();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 13, c); };
        panel(srcPanel, "VOICE SOURCE"); panel(trPanel, "TRACT"); panel(ensPanel, "ENSEMBLE"); panel(envPanel, "ENVELOPE"); panel(voicePanel, "VOICE");
        keyboard.draw(*this);
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        if (vowelR.contains(x, y)) { dragging = true; beginEdit(kVowel); setVowelFrom(x); return true; }
        return false;
    }
    void setVowelFrom(float x)
    {
        const Rect p(vowelR.x + 24, vowelR.y + 34, vowelR.w - 48, vowelR.h - 50);
        setParam(kVowel, pump::clampv((x - p.x) / p.w, 0.0f, 1.0f) * 100.0f);
    }
    void bodyMouseDrag(float x, float y, uint) override
    {
        if (kbDown) { keyboard.mouseDrag(x, y); return; }
        if (dragging) setVowelFrom(x);
    }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; }
        if (dragging) { endEdit(kVowel); dragging = false; }
    }
    void bodyHover(float x, float y) override
    {
        if (tubeR.contains(x, y)) setStatus("The vocal tract, glottis to lips: the tongue's narrowing makes the vowel");
        else if (vowelR.contains(x, y)) setStatus("Click or drag: the vowel");
        else if (keyboard.r.contains(x, y)) setStatus("Play TRACT");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "VOWEL", "click A E I O U, or drag between them" }, { "SIZE", "the tract's length: child to giant" },
                 { "SINGERS", "up to four per note, each a little different" }, { "TENSE / BREATH", "a pressed voice to a whisper" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%d SINGERS   %d VOICES", (int)value(kSingers), plug ? plug->engine.voicesNow() : 0);
        return b;
    }

private:
    static constexpr int kWave = 1024;
    PluginTract* plug = nullptr;
    Rect tubeR, vowelR, waveR, meterL, meterR, srcPanel, trPanel, ensPanel, envPanel, voicePanel;
    Segmented* vmodeSeg;
    Dial *tenseD, *breathD, *vibD, *vrateD, *jitD, *vowelD, *motionD, *mrateD, *sizeD, *brightD, *singersD, *detuneD, *sizesD, *vowelsD, *spreadD;
    Dial *envD[4], *velD, *voicesD, *glideD, *octD, *volD;
    KeyboardUI keyboard;
    bool kbDown = false, dragging = false, playing = false;
    float dia[kSections] = {}, vowelNow = 0.0f, glot = 0.0f, wave[kWave] = {};
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UITract)
};

UI* createUI() { return new UITract(); }

END_NAMESPACE_DISTRHO
