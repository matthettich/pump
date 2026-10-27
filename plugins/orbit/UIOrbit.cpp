// pump ORBIT: interface. ORBIT: the newest voice's path through its system, in 3D, turning slowly (the newest part
// brightest), with the direction you hear it from (AXIS for the left channel, WIDTH further round for the right).
// WAVE: what comes out, and how chaotic it is now. Below: the system and how it is heard (system, chaos, sync,
// axis, width, drive), the drift, the filter and its envelope, the amp envelope and the voice, and a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginOrbit.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::orbit;

class UIOrbit : public PumpUI {
public:
    UIOrbit() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::orbit(), "ORBIT", 1000, 780, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small, bi); if (tip) d->tip = tip; return d; };
        sysSeg = addSegmented(kSystem, { kSystemKeys, kSystemKeys + kNumSystems });
        sysSeg->tip = "The chaotic system each voice runs";
        chaosD = dial(kChaos, "CHAOS", false, "From a clean periodic orbit (0) through period doubling into chaos (100)");
        syncD = dial(kSync, "SYNC", false, "Pulls the orbit back every period of the note: 0 free and drifting, 100 locked to the pitch");
        axisD = dial(kAxis, "AXIS", false, "The direction it is heard from: x (0), y (50), z (100)");
        widthD = dial(kWidth, "WIDTH", false, "The right channel hears it from further round");
        driveD = dial(kDrive, "DRIVE");
        dRateD = dial(kDriftRate, "RATE", false, "How fast the drift wanders (a slow Lorenz system per voice)");
        dPitchD = dial(kDriftPitch, "PITCH"); dCutD = dial(kDriftCut, "CUTOFF"); dChaosD = dial(kDriftChaos, "CHAOS");
        cutD = dial(kCutoff, "CUTOFF"); resD = dial(kReso, "RESO"); keyD = dial(kKeytrack, "KEYTRACK");
        envCutD = dial(kEnvCut, "ENV>CUT", true); envChaosD = dial(kEnvChaos, "ENV>CHAOS", true, "The mod envelope pushes the chaos up (+) or down (-)");
        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) { modD[k] = dial(kModA + k, adsr[k]); ampD[k] = dial(kAttack + k, adsr[k]); }
        velD = dial(kVelocity, "VELOCITY"); voicesD = dial(kVoices, "VOICES"); glideD = dial(kGlide, "GLIDE");
        octD = dial(kOctave, "OCTAVE", true); fineD = dial(kFine, "FINE", true); volD = dial(kVolume, "VOLUME", true);
        vmodeSeg = addSegmented(kVoiceMode, { "POLY", "MONO", "LEGATO" });
        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginOrbit*>(pluginPtr())) plug = p;
    }
    ~UIOrbit() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kFine) { std::snprintf(b, sizeof b, "%+.0f ct", v); return b; }
        if (i == kGlide && v < 0.5f) return "OFF";
        if (i == kSync) return v < 0.5f ? "FREE" : (v > 99.5f ? "LOCKED" : PumpUI::formatParam(i, v));
        if (i == kAxis) { const char* n[3] = { "X", "Y", "Z" }; const float a = v * 0.02f; const int k = std::min(2, (int)std::lround(a)); if (std::fabs(a - k) < 0.08f) return n[k]; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, hs = Dial::heightFor(Dial::Small);
        orbitR = Rect(14, top, 620, 240);
        waveR = Rect(646, top, 314, 240);
        meterL = Rect(966, top, 8, 228); meterR = Rect(977, top, 8, 228);
        sysPanel = Rect(14, 310, 600, 116);
        sysSeg->r = Rect(24, 332, 580, 22);
        Dial* sd[5] = { chaosD, syncD, axisD, widthD, driveD };
        for (int k = 0; k < 5; ++k) sd[k]->r = Rect(22 + k * 117, 356, 117, hs);
        driftPanel = Rect(626, 310, 360, 116);
        Dial* dd[4] = { dRateD, dPitchD, dCutD, dChaosD };
        for (int k = 0; k < 4; ++k) dd[k]->r = Rect(632 + k * 88, 344, 88, hs);
        filtPanel = Rect(14, 436, 520, 116);
        Dial* fd[5] = { cutD, resD, keyD, envCutD, envChaosD };
        for (int k = 0; k < 5; ++k) fd[k]->r = Rect(22 + k * 102, 470, 102, hs);
        modPanel = Rect(546, 436, 440, 116);
        for (int k = 0; k < 4; ++k) modD[k]->r = Rect(556 + k * 106, 470, 106, hs);
        ampPanel = Rect(14, 562, 380, 116);
        for (int k = 0; k < 4; ++k) ampD[k]->r = Rect(22 + k * 92, 596, 92, hs);
        voicePanel = Rect(406, 562, 580, 116);
        vmodeSeg->r = Rect(416, 584, 560, 22);
        Dial* vd[6] = { velD, voicesD, glideD, octD, fineD, volD };
        for (int k = 0; k < 6; ++k) vd[k]->r = Rect(414 + k * 94, 608, 94, hs);
        keyboard.r = Rect(14, 688, 966, 56);
    }

    void tick(float dt) override
    {
        voicesD->enabled = (int)value(kVoiceMode) == 0;
        dRateD->enabled = value(kDriftPitch) > 0.05f || value(kDriftCut) > 0.05f || value(kDriftChaos) > 0.05f;
        anim += dt;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; } else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        nTrail = plug->engine.trail(trailPts, kShow);
        playing = plug->engine.voicesNow() > 0;
        fade = playing ? std::min(1.0f, fade + dt * 6.0f) : std::max(0.0f, fade - dt * 1.2f);
        plug->engine.outRing.readLatest(wave, kWave);
    }

    // a point of the path on screen: turned about the vertical (yaw), tipped towards you (tilt)
    void project(const float* p, float cx, float cy, float sc, float& x, float& y, float& depth) const
    {
        const int sys = (int)value(kSystem);
        // stand each system up the way it is usually drawn (Lorenz and the scroll: their wings left and right)
        float a = p[0], b = p[1], c = p[2];
        if (sys == kScroll) { a = p[0]; b = p[2]; c = p[1]; }
        const float yaw = anim * 0.22f, tilt = 0.38f;
        const float X = a * std::cos(yaw) - b * std::sin(yaw), D = a * std::sin(yaw) + b * std::cos(yaw);
        x = cx + sc * X;
        y = cy - sc * (c * std::cos(tilt) - D * std::sin(tilt));
        depth = D;
    }
    void drawOrbit()
    {
        const Rect& r = orbitR;
        screenBox(r);
        const int sys = pump::clampv((int)value(kSystem), 0, kNumSystems - 1);
        caption(r.x + 10, r.y + 14, "ORBIT");
        std::string nm = kSystemNames[sys]; for (char& c : nm) c = (char)std::toupper((unsigned char)c);
        char b[96]; std::snprintf(b, sizeof b, "%s  ·  CHAOS %.0f%%", nm.c_str(), (plug ? plug->engine.chaosNow() : value(kChaos) * 0.01f) * 100.0f);
        textAt(r.r() - 10, r.y + 14, b, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.55f, sc = r.h * 0.21f;
        // the axes, faint
        const float ax[3][3] = { { 1.6f, 0, 0 }, { 0, 1.6f, 0 }, { 0, 0, 1.6f } };
        const char* an[3] = { "X", "Y", "Z" };
        for (int k = 0; k < 3; ++k)
        {
            const float o[3] = { 0, 0, 0 }; float x0, y0, x1, y1, d;
            project(o, cx, cy, sc, x0, y0, d); project(ax[k], cx, cy, sc, x1, y1, d);
            line(x0, y0, x1, y1, theme.grid, 1.0f);
            textAt(x1, y1, an[k], kMono, 8.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // the listening directions
        auto dirOf = [&](double phi, Color c) {
            double w[3]; direction(phi, w);
            float p3[3] = { (float)w[0] * 2.0f, (float)w[1] * 2.0f, (float)w[2] * 2.0f }, o[3] = { 0, 0, 0 };
            float x0, y0, x1, y1, d; project(o, cx, cy, sc, x0, y0, d); project(p3, cx, cy, sc, x1, y1, d);
            line(x0, y0, x1, y1, c, 2.0f);
            beginPath(); circle(x1, y1, 3.5f); fillColor(c); fill();
        };
        const Color hot = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        dirOf(2.0 * value(kAxis) * 0.01, theme.ink);
        if (value(kWidth) > 0.5f) dirOf(2.0 * value(kAxis) * 0.01 + 1.5 * value(kWidth) * 0.01, theme.ink2);
        // the path: in pieces, older fainter
        if (nTrail > 2 && fade > 0.0f)
        {
            const int pieces = 32, per = std::max(2, nTrail / pieces);
            for (int s0 = 0; s0 < nTrail - 1; s0 += per)
            {
                const int s1 = std::min(nTrail - 1, s0 + per);
                const float age = (float)s1 / nTrail;
                beginPath();
                for (int i = s0; i <= s1; ++i)
                {
                    float p[3] = { pump::clampv(trailPts[i][0], -3.0f, 3.0f), pump::clampv(trailPts[i][1], -3.0f, 3.0f), pump::clampv(trailPts[i][2], -3.0f, 3.0f) };
                    float x, y, d; project(p, cx, cy, sc, x, y, d);
                    if (i == s0) moveTo(x, y); else lineTo(x, y);
                }
                strokeColor(withAlpha2(hot, fade * (0.08f + 0.92f * age * age))); strokeWidth(0.8f + 1.4f * age); stroke();
            }
            float p[3] = { trailPts[nTrail - 1][0], trailPts[nTrail - 1][1], trailPts[nTrail - 1][2] }, x, y, d;
            project(p, cx, cy, sc, x, y, d);
            beginPath(); circle(x, y, 4.0f); fillColor(withAlpha2(theme.ink, fade)); fill();
        }
        else textAt(cx, cy, "PLAY A NOTE", kLabel, 10.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
    }
    void drawWave()
    {
        const Rect& r = waveR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "WAVE");
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 36);
        line(p.x, p.cy(), p.r(), p.cy(), theme.grid, 1.0f);
        // start where it rises through zero, so a locked orbit stands still
        int st = 0;
        for (int i = 1; i < kWave / 2; ++i) if (wave[i - 1] <= 0.0f && wave[i] > 0.0f) { st = i; break; }
        float mx = 1e-3f; for (int i = 0; i < kWave; ++i) mx = std::max(mx, std::fabs(wave[i]));
        const int n = kWave / 2;
        beginPath();
        for (int i = 0; i < n; ++i)
        {
            const float x = p.x + p.w * i / (n - 1.0f), y = p.cy() - wave[st + i] / mx * p.h * 0.45f;
            if (!i) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.6f); stroke();
        const char* lock = value(kSync) > 99.5f ? "LOCKED" : (value(kSync) < 0.5f ? "FREE" : "SYNC");
        textAt(r.r() - 10, r.y + 14, lock, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
    }

    void drawBody() override
    {
        drawOrbit();
        drawWave();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 13, c); };
        panel(sysPanel, "SYSTEM"); panel(driftPanel, "DRIFT"); panel(filtPanel, "FILTER"); panel(modPanel, "MOD ENVELOPE");
        panel(ampPanel, "AMP ENVELOPE"); panel(voicePanel, "VOICE");
        keyboard.draw(*this);
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override { if (kbDown) keyboard.mouseDrag(x, y); }
    void bodyMouseUp(float, float) override { if (kbDown) { keyboard.mouseUp(); kbDown = false; } }
    void bodyHover(float x, float y) override
    {
        if (orbitR.contains(x, y)) setStatus("The newest voice's path through its system; the lines are the directions you hear it from (left, right)");
        else if (waveR.contains(x, y)) setStatus("What comes out: with SYNC high it repeats every period");
        else if (keyboard.r.contains(x, y)) setStatus("Play ORBIT");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "CHAOS", "periodic (0), period doubling, chaos (100)" }, { "SYNC", "0: free, drifting · 100: locked to the note" },
                 { "AXIS / WIDTH", "where you hear the orbit from, left and right" }, { "DRIFT", "a slow chaotic system wandering pitch, cutoff, chaos" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   %d VOICES", kSystemKeys[pump::clampv((int)value(kSystem), 0, kNumSystems - 1)], plug ? plug->engine.voicesNow() : 0);
        return b;
    }

private:
    static constexpr int kShow = 1600, kWave = 1024;
    PluginOrbit* plug = nullptr;
    Rect orbitR, waveR, meterL, meterR, sysPanel, driftPanel, filtPanel, modPanel, ampPanel, voicePanel;
    Segmented *sysSeg, *vmodeSeg;
    Dial *chaosD, *syncD, *axisD, *widthD, *driveD, *dRateD, *dPitchD, *dCutD, *dChaosD, *cutD, *resD, *keyD, *envCutD, *envChaosD;
    Dial *modD[4], *ampD[4], *velD, *voicesD, *glideD, *octD, *fineD, *volD;
    KeyboardUI keyboard;
    bool kbDown = false, playing = false;
    float trailPts[kShow][3] = {}, wave[kWave] = {};
    int nTrail = 0;
    float anim = 0.0f, fade = 0.0f;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIOrbit)
};

UI* createUI() { return new UIOrbit(); }

END_NAMESPACE_DISTRHO
