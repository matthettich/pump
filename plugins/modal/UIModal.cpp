// pump MODAL: interface. OBJECT: the object itself, moving with its modes as they ring (a bar, a string or a tube
// bends along its length; a drum, a bell or a glass flexes round its rim; a plate round its edge), with the mallet
// where it strikes: drag it to move the strike position. MODES: every mode of the newest note, at its ratio to the
// first (log), as tall as it rings now. Below: the object (which, how many modes, material, decay, damping,
// brightness, stretch, position), the exciter (mallet, pluck, bow, breath; hardness, noise, pressure, vibrato) and
// the voice, and a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginModal.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::modal;

class UIModal : public PumpUI {
public:
    UIModal() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::modal(), "MODAL", 1000, 660, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small, bi); if (tip) d->tip = tip; return d; };
        modelSeg = addSegmented(kModel, { kModelKeys, kModelKeys + kNumModels });
        modelSeg->tip = "The object: its modes' ratios (a bar, a tuned marimba bar, a string, a drum skin, a plate, a bell, a tube, a glass)";
        modesD = dial(kModes, "MODES", false, "How many modes ring (more: richer and heavier on the CPU)");
        matD = dial(kMaterial, "MATERIAL", false, "Wood (0: the upper modes die fast) to metal (100: everything rings)");
        decayD = dial(kDecay, "DECAY", false, "How long the first mode rings while the key is held");
        dampD = dial(kDamp, "DAMP", false, "How fast it dies once the key is up (a hand on the bar)");
        brightD = dial(kBright, "BRIGHT", true, "Tilts the modes: brighter (+) or darker (-)");
        stretchD = dial(kStretch, "STRETCH", true, "Squeezes (-) or spreads (+) the modes: retunes the object");
        posD = dial(kPosition, "POSITION", false, "Where it is struck: 0 at the end, 100 in the middle (no even modes on a string)");
        exSeg = addSegmented(kExciter, { "MALLET", "PLUCK", "BOW", "BREATH" });
        exSeg->tip = "How it's played: struck, plucked, bowed or blown (bow and breath sound while the key is held)";
        hardD = dial(kHardness, "HARDNESS", false, "Mallet: soft felt (0) to a hard stick (100) · pluck: finger to pick · bow: rosin");
        noiseD = dial(kNoise, "NOISE", false, "The exciter's own noise: the knock of the mallet, the bow's hair, the breath");
        pressD = dial(kPressure, "PRESSURE", false, "Bow / breath: how hard");
        vibD = dial(kVibrato, "VIBRATO");
        rateD = dial(kVibRate, "RATE");
        velD = dial(kVelocity, "VELOCITY"); spreadD = dial(kSpread, "SPREAD", false, "Spreads the modes across the stereo field");
        voicesD = dial(kVoices, "VOICES"); glideD = dial(kGlide, "GLIDE");
        octD = dial(kOctave, "OCTAVE", true); volD = dial(kVolume, "VOLUME", true);
        vmodeSeg = addSegmented(kVoiceMode, { "POLY", "MONO", "LEGATO" });
        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginModal*>(pluginPtr())) plug = p;
    }
    ~UIModal() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kGlide && v < 0.5f) return "OFF";
        if (i == kVibrato && v < 0.05f) return "OFF";
        if (i == kMaterial) return v < 20 ? "WOOD" : (v < 45 ? "HARD WOOD" : (v < 70 ? "GLASS" : (v < 90 ? "STEEL" : "BRASS")));
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, hs = Dial::heightFor(Dial::Small);
        objR = Rect(14, top, 560, 214);
        modesR = Rect(586, top, 374, 214);
        meterL = Rect(966, top, 8, 202); meterR = Rect(977, top, 8, 202);
        objPanel = Rect(14, 284, 972, 126);
        modelSeg->r = Rect(24, 306, 952, 22);
        Dial* od[7] = { modesD, matD, decayD, dampD, brightD, stretchD, posD };
        for (int k = 0; k < 7; ++k) od[k]->r = Rect(26 + k * 136, 332, 136, hs);
        exPanel = Rect(14, 420, 470, 126);
        exSeg->r = Rect(24, 442, 450, 22);
        Dial* ed[5] = { hardD, noiseD, pressD, vibD, rateD };
        for (int k = 0; k < 5; ++k) ed[k]->r = Rect(22 + k * 91, 468, 91, hs);
        voicePanel = Rect(496, 420, 490, 126);
        vmodeSeg->r = Rect(506, 442, 470, 22);
        Dial* vd[6] = { velD, spreadD, voicesD, glideD, octD, volD };
        for (int k = 0; k < 6; ++k) vd[k]->r = Rect(502 + k * 80, 468, 80, hs);
        keyboard.r = Rect(14, 556, 966, 56);
    }

    void tick(float dt) override
    {
        const int ex = (int)value(kExciter);
        voicesD->enabled = (int)value(kVoiceMode) == 0;
        pressD->enabled = ex == kBow || ex == kBlow;
        rateD->enabled = value(kVibrato) > 0.05f;
        glideD->enabled = (int)value(kVoiceMode) != 0 || value(kGlide) > 0.5f;
        anim += dt;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; } else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        float mx = 0.0f;
        for (int m = 0; m < kMaxModes; ++m)
        {
            lev[m] = plug->engine.modeLevel(m); rat[m] = plug->engine.modeRatio(m);
            mx = std::max(mx, lev[m]);
        }
        ref = std::max(mx, ref * std::exp(-dt * 0.25f));
        ref = std::max(ref, 1e-4f);
        hit = std::max(std::min(1.0f, plug->engine.exciteNow() * 40.0f), hit * std::exp(-dt * 10.0f));
    }

    // a mode's size on screen (log, 0..1)
    float shown(int m) const
    {
        if (lev[m] <= 0.0f) return 0.0f;
        const float db = 20.0f * std::log10(lev[m] / ref + 1e-9f);
        return pump::clampv((db + 54.0f) / 54.0f, 0.0f, 1.0f);
    }
    // each mode wobbles at a slow, visible rate that goes up with its ratio
    float wob(int m) const { return std::cos(anim * (5.0f + 6.0f * std::sqrt(std::max(0.2f, rat[m]))) + m * 1.7f); }

    void drawObject()
    {
        const Rect& r = objR;
        screenBox(r);
        const int model = pump::clampv((int)value(kModel), 0, kNumModels - 1);
        caption(r.x + 10, r.y + 14, "OBJECT");
        std::string nm = kModelNames[model]; nm += " · "; nm += kExciterNames[pump::clampv((int)value(kExciter), 0, kNumExciters - 1)];
        for (char& c : nm) c = (char)std::toupper((unsigned char)c);
        textAt(r.r() - 10, r.y + 14, nm, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        const Color ink = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        const int nm2 = std::min(kMaxModes, (int)value(kModes));
        const float pos = value(kPosition) * 0.01f * 0.5f;
        const bool line1 = model == kBar || model == kMarimba || model == kString || model == kTube;
        if (line1)
        {
            // along the length: the sum of the mode shapes (sin(pi m u) on a string; the same family for the others)
            const float x0 = r.x + 40, x1 = r.r() - 40, cy = r.y + r.h * 0.55f, amp = r.h * 0.26f;
            const int N = 180;
            float ys[N + 1];
            for (int i = 0; i <= N; ++i)
            {
                const float u = (float)i / N;
                float d = 0.0f;
                for (int m = 0; m < nm2; ++m)
                {
                    const float s = shown(m);
                    if (s <= 0.0f) continue;
                    const float k = model == kTube ? (2 * m + 1) * 0.5f : (float)(m + 1);
                    float shape = std::sin((float)pump::kPi * k * u);
                    if (model == kBar || model == kMarimba) shape = std::cos((float)pump::kPi * (m + 1.5f) * u) * 0.8f;   // free ends move too
                    d += s * s * shape * wob(m) / (1.0f + 0.35f * m);
                }
                ys[i] = cy - amp * pump::clampv(d, -1.6f, 1.6f) * 0.6f;
            }
            const float thick = model == kString ? 2.0f : (model == kTube ? 3.0f : 10.0f);
            if (model == kBar || model == kMarimba)
            {
                // a bar: a band with some depth, on two cords at its nodes
                beginPath();
                for (int i = 0; i <= N; ++i) { const float x = x0 + (x1 - x0) * i / N; if (!i) moveTo(x, ys[i] - thick); else lineTo(x, ys[i] - thick); }
                for (int i = N; i >= 0; --i) { const float x = x0 + (x1 - x0) * i / N; lineTo(x, ys[i] + thick); }
                closePath(); fillColor(withAlpha2(ink, theme.dark ? 0.35f : 0.25f)); fill();
                strokeColor(ink); strokeWidth(1.6f); stroke();
                for (float u : { 0.224f, 0.776f }) line(x0 + (x1 - x0) * u, r.b() - 14, x0 + (x1 - x0) * u, cy + thick + 4, theme.dim, 1.5f);
            }
            else
            {
                if (model == kTube)
                {
                    // the tube's walls, and the air column moving inside
                    line(x0, cy - 22, x1, cy - 22, theme.ink2, 2.0f); line(x0, cy + 22, x1, cy + 22, theme.ink2, 2.0f);
                    line(x0, cy - 22, x0, cy + 22, theme.ink2, 2.0f);
                }
                else { fillRect(Rect(x0 - 8, cy - 18, 6, 36), theme.ink2); fillRect(Rect(x1 + 2, cy - 18, 6, 36), theme.ink2); }
                beginPath();
                for (int i = 0; i <= N; ++i) { const float x = x0 + (x1 - x0) * i / N; const float y = model == kTube ? cy + (ys[i] - cy) * 0.35f : ys[i]; if (!i) moveTo(x, y); else lineTo(x, y); }
                strokeColor(ink); strokeWidth(thick); stroke();
            }
            // the strike point
            const float sx = x0 + (x1 - x0) * pos;
            drawStriker(sx, cy - (model == kBar || model == kMarimba ? thick + 14.0f : 22.0f), true);
        }
        else
        {
            // round the rim (a drum, a bell, a glass) or the edge of a plate
            const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.55f, R = r.h * 0.34f;
            const int N = 160;
            for (int ring = 0; ring < 3; ++ring)
            {
                const float rr = R * (1.0f - ring * 0.3f);
                beginPath();
                for (int i = 0; i <= N; ++i)
                {
                    const float th = (float)pump::kTwoPi * i / N;
                    float d = 0.0f;
                    for (int m = 0; m < nm2; ++m)
                    {
                        const float s = shown(m);
                        if (s <= 0.0f) continue;
                        const int around = model == kMembrane ? m % 6 : (m + 2) % 9;   // the mode's number of waves round
                        const float radial = std::sin((float)pump::kPi * (1.0f - ring * 0.3f) * (1 + m / 6));
                        d += s * s * std::cos(around * th + m * 0.9f) * wob(m) * radial / (1.0f + 0.3f * m);
                    }
                    float x, y;
                    if (model == kPlate)
                    {
                        // a square: the edge point at this angle
                        const float c = std::cos(th), sn = std::sin(th), k = rr / std::max(std::fabs(c), std::fabs(sn));
                        x = cx + c * k * (1.0f + 0.10f * d); y = cy + sn * k * 0.85f * (1.0f + 0.10f * d);
                    }
                    else if (model == kBell || model == kGlass)
                    {
                        // seen from above, a little in perspective
                        x = cx + std::cos(th) * rr * 1.3f * (1.0f + 0.12f * d); y = cy + std::sin(th) * rr * 0.62f * (1.0f + 0.12f * d);
                    }
                    else { x = cx + std::cos(th) * rr * 1.25f * (1.0f + 0.10f * d); y = cy + std::sin(th) * rr * 0.75f * (1.0f + 0.10f * d); }
                    if (!i) moveTo(x, y); else lineTo(x, y);
                }
                closePath();
                if (ring == 0) { fillColor(withAlpha2(ink, theme.dark ? 0.18f : 0.12f)); fill(); }
                strokeColor(ring == 0 ? ink : withAlpha2(ink, 0.55f)); strokeWidth(ring == 0 ? 2.2f : 1.3f); stroke();
            }
            // where it is struck: from the rim (0) to the centre (100)
            const float sx = cx + (1.0f - pos * 2.0f) * R * 1.2f;
            drawStriker(sx, cy - 4, false);
        }
    }
    void drawStriker(float x, float y, bool above)
    {
        const int ex = (int)value(kExciter);
        const Color c = theme.dark ? theme.ink : theme.ink;
        const float h = hit;
        if (ex == kBow)
        {
            // a bow across it
            line(x - 60, y + 10 - 4 * h, x + 60, y - 4 - 4 * h, c, 2.0f);
            line(x - 60, y + 14 - 4 * h, x + 60, y - 0 - 4 * h, withAlpha2(c, 0.5f), 1.0f);
        }
        else if (ex == kBlow)
        {
            // breath: a few lines blowing in
            for (int k = 0; k < 3; ++k) { const float yy = y - 10 + k * 8; line(x - 44 - 6 * h, yy, x - 14, yy + 4, withAlpha2(c, 0.4f + 0.5f * h), 1.5f); }
        }
        else if (ex == kPluck)
        {
            beginPath(); moveTo(x - 7, y - 18 + 6 * h); lineTo(x + 7, y - 18 + 6 * h); lineTo(x, y - 4 + 6 * h); closePath(); fillColor(c); fill();
        }
        else
        {
            // a mallet: a head on a stick, dropping in on each strike
            const float yy = y - 16 + 10 * h;
            line(x, yy - 6, x + (above ? 34 : 30), yy - 44, c, 2.5f);
            beginPath(); circle(x, yy, 7.0f); fillColor(c); fill();
        }
        // the strike position marker
        line(x, objR.b() - 18, x, objR.b() - 8, theme.ink2, 2.0f);
    }

    void drawModes()
    {
        const Rect& r = modesR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "MODES");
        const Rect p(r.x + 12, r.y + 26, r.w - 24, r.h - 46);
        float hi = 2.0f;
        for (int m = 0; m < kMaxModes; ++m) if (rat[m] > 0.0f) hi = std::max(hi, rat[m]);
        const float lo = 0.45f;
        auto X = [&](float ratio) { return p.x + p.w * std::log(std::max(lo, ratio) / lo) / std::log(hi * 1.1f / lo); };
        for (float g : { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f })
        {
            if (g > hi * 1.1f) break;
            const float x = X(g);
            line(x, p.y, x, p.b(), theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, "%g", g);
            textAt(x, p.b() + 10, b, kMono, 8.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        const Color c = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.4f);
        int lit = 0;
        for (int m = 0; m < kMaxModes; ++m)
        {
            if (rat[m] <= 0.0f) continue;
            const float s = shown(m), x = X(rat[m]);
            if (s > 0.0f) ++lit;
            line(x, p.b(), x, p.b() - std::max(1.0f, s * p.h), s > 0.0f ? c : theme.dim, m == 0 ? 3.0f : 2.0f);
        }
        char b[48]; std::snprintf(b, sizeof b, "%d RINGING · RATIO TO THE FIRST", lit);
        textAt(r.r() - 10, r.y + 14, b, kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);
    }

    void drawBody() override
    {
        drawObject();
        drawModes();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 13, c); };
        panel(objPanel, "OBJECT"); panel(exPanel, "EXCITER"); panel(voicePanel, "VOICE");
        keyboard.draw(*this);
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        if (objR.contains(x, y)) { dragging = true; beginEdit(kPosition); setParam(kPosition, posFromX(x)); return true; }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override
    {
        if (kbDown) { keyboard.mouseDrag(x, y); return; }
        if (dragging) setParam(kPosition, posFromX(x));
    }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; }
        if (dragging) { endEdit(kPosition); dragging = false; }
    }
    // POSITION from a click: along the length (0 at an end, 100 in the middle) or across the rim
    float posFromX(float x) const
    {
        const int model = (int)value(kModel);
        const bool line1 = model == kBar || model == kMarimba || model == kString || model == kTube;
        if (line1)
        {
            const float x0 = objR.x + 40, x1 = objR.r() - 40, u = pump::clampv((x - x0) / (x1 - x0), 0.0f, 1.0f);
            return std::min(u, 1.0f - u) * 200.0f;
        }
        const float cx = objR.x + objR.w * 0.5f, R = objR.h * 0.34f * 1.2f;
        const float u = pump::clampv(std::fabs(x - cx) / R, 0.0f, 1.0f);
        return (1.0f - u) * 100.0f;
    }
    void bodyHover(float x, float y) override
    {
        if (objR.contains(x, y)) setStatus("The object, moving with its modes. Drag: where it is struck (POSITION)");
        else if (modesR.contains(x, y)) setStatus("The newest note's modes: at their ratio to the first, as loud as they ring now");
        else if (keyboard.r.contains(x, y)) setStatus("Play MODAL");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "OBJECT", "the modes' ratios: bar, marimba, string, drum, plate, bell, tube, glass" }, { "MATERIAL", "wood: the upper modes die fast · metal: everything rings" },
                 { "POSITION", "drag the object: where it is struck" }, { "BOW / BREATH", "sound while the key is held, at PRESSURE" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   %d VOICES", kModelKeys[pump::clampv((int)value(kModel), 0, kNumModels - 1)], plug ? plug->engine.voicesNow() : 0);
        return b;
    }

private:
    PluginModal* plug = nullptr;
    Rect objR, modesR, meterL, meterR, objPanel, exPanel, voicePanel;
    Segmented *modelSeg, *exSeg, *vmodeSeg;
    Dial *modesD, *matD, *decayD, *dampD, *brightD, *stretchD, *posD, *hardD, *noiseD, *pressD, *vibD, *rateD;
    Dial *velD, *spreadD, *voicesD, *glideD, *octD, *volD;
    KeyboardUI keyboard;
    bool kbDown = false, dragging = false;
    float lev[kMaxModes] = {}, rat[kMaxModes] = {}, ref = 1e-3f, hit = 0.0f, anim = 0.0f;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIModal)
};

UI* createUI() { return new UIModal(); }

END_NAMESPACE_DISTRHO
