// pump TABLE: interface. Two wavetable screens (the table's frames stacked back to front, the frame playing now
// drawn in neon; drag up/down to move the position, wheel steps the table), the filter (its response where the
// last note has it now) and the voice count. Below them: oscillator A, B, filter, voice; then the amp and mod
// envelopes, LFO 1 and 2, the mod matrix; a keyboard along the bottom to play.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginTable.hpp"
#include <complex>

START_NAMESPACE_DISTRHO

using namespace pump::table;
using pump::kNoteNames;
using pump::kNumNotes;

class UITable : public PumpUI {
public:
    UITable() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::table(), "TABLE", 1000, 740, -1)
    {
        for (int o = 0; o < 2; ++o)
        {
            const int b = o * kOscStride;
            Osc& c = osc[o];
            c.table = addMenu(kTableA + b); c.table->arrows = true;
            c.table->tip = "Wavetable (USER = the file loaded with LOAD)";
            c.load = addButton("LOAD", [this, o] { which = o; FileBrowserOptions op; op.title = "Load a wavetable (.wav, 2048-sample frames)"; openFileBrowser(op); });
            c.load->small = true;
            c.load->tip = "Load a .wav wavetable (frames of 2048 samples) into USER";
            c.pos = addDial(kPosA + b, "POS", Dial::Small);
            c.pos->tip = "Position: which frame of the table plays (or drag the table screen up / down)";
            c.level = addDial(kLevelA + b, "LEVEL", Dial::Small);
            c.pan = addDial(kPanA + b, "PAN", Dial::Small, true);
            c.uni = addDial(kVoicesA + b, "UNISON", Dial::Small);
            c.uni->tip = "Unison: copies of the oscillator, detuned and spread across the stereo field";
            c.det = addDial(kDetuneA + b, "DETUNE", Dial::Small);
            c.spr = addDial(kSpreadA + b, "SPREAD", Dial::Small);
            c.oct = addDial(kOctA + b, "OCT", Dial::Small, true);
            c.semi = addDial(kSemiA + b, "SEMI", Dial::Small, true);
            c.fine = addDial(kFineA + b, "FINE", Dial::Small, true);
            for (Dial* d : { c.pos, c.level, c.pan, c.uni, c.det, c.spr, c.oct, c.semi, c.fine }) d->tabGroup = 1 + o;
        }
        subDial = addDial(kSub, "SUB", Dial::Small);
        subDial->tip = "Sub: a sine an octave under oscillator A";
        ftype = addSegmented(kFilterType, { "LP24", "LP12", "BP", "HP" });
        cutDial = addDial(kCutoff, "CUTOFF", Dial::Small);
        resDial = addDial(kRes, "RES", Dial::Small);
        drvDial = addDial(kDrive, "DRIVE", Dial::Small);
        envDial = addDial(kFilterEnv, "ENV", Dial::Small, true);
        envDial->tip = "Mod envelope to cutoff (up to +-6 octaves)";
        keyDial = addDial(kKeyTrack, "KEY", Dial::Small);
        keyDial->tip = "Key tracking: the cutoff follows the note (100% = one octave per octave)";
        noiseDial = addDial(kNoise, "NOISE", Dial::Small);
        modeSeg = addSegmented(kVoiceMode, { "POLY", "MONO", "LEG" });
        modeSeg->tip = "Poly, mono, or legato (overlapping notes glide and don't restart the envelopes)";
        polyDial = addDial(kPolyphony, "VOICES", Dial::Small);
        glideDial = addDial(kGlide, "GLIDE", Dial::Small);
        bendDial = addDial(kBendRange, "BEND", Dial::Small);
        bendDial->tip = "Pitch bend range in semitones";
        volDial = addDial(kVolume, "VOLUME", Dial::Small, true);

        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) { ampD[k] = addDial(kAmpA + k, adsr[k], Dial::Small); modD[k] = addDial(kModA + k, adsr[k], Dial::Small); }
        velDial = addDial(kVelAmp, "VEL", Dial::Small);
        velDial->tip = "Velocity to level";
        lfo1Shape = addMenu(kLfo1Shape);
        lfo1Rate = addDial(kLfo1Rate, "RATE", Dial::Small);
        lfo1Rate->tip = "LFO 1: one per voice, restarts with each note";
        lfo2Shape = addMenu(kLfo2Shape);
        lfo2Rate = addDial(kLfo2Rate, "RATE", Dial::Small);
        lfo2Rate->tip = "LFO 2: shared by all voices";
        lfo2Sync = addToggle(kLfo2Sync, "SYNC");
        lfo2Sync->small = true;
        lfo2Div = addMenu(kLfo2Div);
        lfo2Div->arrows = true;
        for (int k = 0; k < 4; ++k)
        {
            src[k] = addMenu(kSrc1 + 3 * k);
            dst[k] = addMenu(kDst1 + 3 * k);
            amt[k] = addRow(kAmt1 + 3 * k, "", true);
            amt[k]->labelW = 0; amt[k]->valueW = 32;
            src[k]->tip = "Mod source"; dst[k]->tip = "Mod destination"; amt[k]->tip = "Mod amount (+-)";
        }

        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginTable*>(pluginPtr())) plug = p;
    }
    ~UITable() override { keyboard.mouseUp(); }

protected:
    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename || !*filename) return;
        setState(which == 0 ? "table_a" : "table_b", filename);
        setParamGesture(which == 0 ? kTableA : kTableB, (float)kUserTable);
    }

    std::string formatParam(int i, float v) const override
    {
        char b[32];
        const int base = i >= kTableB && i <= kPanB ? i - kOscStride : i;
        if (base == kOctA || base == kSemiA) { std::snprintf(b, sizeof b, "%+.0f", v); return std::fabs(v) < 0.5f ? std::string("0") : std::string(b); }
        if (base == kFineA) { std::snprintf(b, sizeof b, "%+.0f ct", v); return b; }
        if (base == kVoicesA) { std::snprintf(b, sizeof b, "%.0f", v); return b; }
        if (base == kTableA && (int)v == kUserTable)
        {
            const std::string n = plug ? plug->userInfo(i == kTableA ? 0 : 1) : "";
            return n.empty() ? "USER (EMPTY)" : "USER: " + n;
        }
        if (i == kPolyphony || i == kBendRange) { std::snprintf(b, sizeof b, "%.0f", v); return b; }
        if (i == kGlide && v < 0.5f) return "OFF";
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, sh = 166;
        screen[0] = Rect(14, top, 300, sh);
        screen[1] = Rect(326, top, 300, sh);
        filterR = Rect(638, top, 220, sh);
        voiceR = Rect(870, top, 90, sh);
        meterL = Rect(966, top, 8, sh - 12);
        meterR = Rect(977, top, 8, sh - 12);
        const float y1 = 238, ph = 196;
        for (int o = 0; o < 2; ++o)
        {
            Osc& c = osc[o];
            const float x0 = o == 0 ? 14 : 326;
            c.panel = Rect(x0, y1, 300, ph);
            c.table->r = Rect(x0 + 12, y1 + 28, 206, 22);
            c.load->r = Rect(x0 + 226, y1 + 28, 62, 22);
            const float dw = 56;
            Dial* row1[5] = { c.pos, c.level, c.pan, c.uni, c.det };
            Dial* row2[5] = { c.spr, c.oct, c.semi, c.fine, o == 0 ? subDial : nullptr };
            for (int k = 0; k < 5; ++k)
            {
                row1[k]->r = Rect(x0 + 10 + k * dw, y1 + 56, dw, Dial::heightFor(Dial::Small));
                if (row2[k]) row2[k]->r = Rect(x0 + 10 + k * dw, y1 + 124, dw, Dial::heightFor(Dial::Small));
            }
        }
        filterPanel = Rect(638, y1, 220, ph);
        ftype->r = Rect(650, y1 + 28, 196, 22);
        Dial* fr1[3] = { cutDial, resDial, drvDial };
        Dial* fr2[3] = { envDial, keyDial, noiseDial };
        for (int k = 0; k < 3; ++k)
        {
            fr1[k]->r = Rect(648 + k * 68, y1 + 56, 64, Dial::heightFor(Dial::Small));
            fr2[k]->r = Rect(648 + k * 68, y1 + 124, 64, Dial::heightFor(Dial::Small));
        }
        voicePanel = Rect(870, y1, 116, ph);
        modeSeg->r = Rect(878, y1 + 28, 100, 22);
        polyDial->r = Rect(872, y1 + 56, 56, Dial::heightFor(Dial::Small));
        glideDial->r = Rect(928, y1 + 56, 56, Dial::heightFor(Dial::Small));
        bendDial->r = Rect(872, y1 + 124, 56, Dial::heightFor(Dial::Small));
        volDial->r = Rect(928, y1 + 124, 56, Dial::heightFor(Dial::Small));

        const float y2 = 446, ph2 = 192;
        ampPanel = Rect(14, y2, 236, ph2);
        for (int k = 0; k < 4; ++k) ampD[k]->r = Rect(20 + k * 44, y2 + 112, 44, Dial::heightFor(Dial::Small));
        velDial->r = Rect(196, y2 + 112, 48, Dial::heightFor(Dial::Small));
        modPanel = Rect(262, y2, 190, ph2);
        for (int k = 0; k < 4; ++k) modD[k]->r = Rect(268 + k * 44, y2 + 112, 44, Dial::heightFor(Dial::Small));
        lfo1Panel = Rect(464, y2, 110, ph2);
        lfo1Shape->r = Rect(474, y2 + 28, 90, 22);
        lfo1Rate->r = Rect(487, y2 + 112, 64, Dial::heightFor(Dial::Small));
        lfo2Panel = Rect(586, y2, 130, ph2);
        lfo2Shape->r = Rect(596, y2 + 28, 110, 22);
        lfo2Sync->r = Rect(596, y2 + 56, 50, 22);
        lfo2Div->r = Rect(596, y2 + 84, 110, 22);
        lfo2Rate->r = Rect(619, y2 + 112, 64, Dial::heightFor(Dial::Small));
        matrixPanel = Rect(728, y2, 258, ph2);
        for (int k = 0; k < 4; ++k)
        {
            const float y = y2 + 30 + k * 38;
            src[k]->r = Rect(736, y, 92, 22);
            dst[k]->r = Rect(832, y, 84, 22);
            amt[k]->r = Rect(920, y, 60, 22);
        }
        keyboard.r = Rect(14, 650, 966, 56);
    }

    void tick(float dt) override
    {
        for (int o = 0; o < 2; ++o) { const int u = (int)value(kVoicesA + o * kOscStride); osc[o].det->enabled = osc[o].spr->enabled = u > 1; }
        lfo1Rate->enabled = true;
        lfo2Rate->enabled = value(kLfo2Sync) < 0.5f;
        lfo2Div->enabled = !lfo2Rate->enabled;
        polyDial->enabled = value(kVoiceMode) < 0.5f;
        drvDial->enabled = true;
        if (!plug) return;
        outMeter.tick(plug->outPeak, dt);
        const int vn = plug->engine.voicesNow();
        for (int o = 0; o < 2; ++o)
        {
            const float want = vn > 0 ? plug->engine.posNow(o) : value(kPosA + o * kOscStride) * 0.01f;
            posShow[o] += (want - posShow[o]) * std::min(1.0f, dt * 20.0f);
            // where the position was a moment ago, for the trail (sampled every 90 ms)
            trailT[o] += dt;
            if (trailT[o] >= 0.09f) { trailT[o] = 0.0f; for (int j = kTrail - 1; j > 0; --j) posTrail[o][j] = posTrail[o][j - 1]; posTrail[o][0] = posShow[o]; }
        }
        phaseShow += dt;
        const float c = vn > 0 ? plug->engine.cutoffNow() : value(kCutoff);
        cutShow += (std::log2(std::max(20.0f, c)) - cutShow) * std::min(1.0f, dt * 20.0f);
        voicesShow = vn;
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
    }

    const Table* tableShown(int o) const
    {
        const int t = (int)value(kTableA + o * kOscStride);
        if (t == kUserTable) { const Table* u = plug ? plug->userTable(o) : nullptr; return u; }
        return factory()[(size_t)pump::clampv(t, 0, kNumFactory - 1)].get();
    }

    void drawBody() override
    {
        for (int o = 0; o < 2; ++o) drawTable(o);
        drawFilter();
        drawVoices();
        drawOutMeter(outMeter, meterL, meterR);
        for (int o = 0; o < 2; ++o) { strokeRect(osc[o].panel, theme.dim, 1.0f); caption(osc[o].panel.x + 12, osc[o].panel.y + 16, o == 0 ? "OSC A" : "OSC B"); }
        for (auto pr : { std::make_pair(filterPanel, "FILTER"), std::make_pair(voicePanel, "VOICE"), std::make_pair(ampPanel, "AMP ENVELOPE"),
                         std::make_pair(modPanel, "MOD ENVELOPE"), std::make_pair(lfo1Panel, "LFO 1"), std::make_pair(lfo2Panel, "LFO 2"), std::make_pair(matrixPanel, "MOD MATRIX") })
        {
            strokeRect(pr.first, theme.dim, 1.0f);
            caption(pr.first.x + 12, pr.first.y + 16, pr.second);
        }
        drawEnv(Rect(ampPanel.x + 12, ampPanel.y + 28, ampPanel.w - 24, 76), kAmpA, plug ? plug->engine.ampEnvNow() : 0.0f);
        drawEnv(Rect(modPanel.x + 12, modPanel.y + 28, modPanel.w - 24, 76), kModA, plug ? plug->engine.modEnvNow() : 0.0f);
        drawLfo(Rect(lfo1Panel.x + 12, lfo1Panel.y + 56, lfo1Panel.w - 24, 48), (int)value(kLfo1Shape));
        keyboard.draw(*this);
    }

    void drawTable(int o)
    {
        const Rect& r = screen[o];
        screenBox(r);
        caption(r.x + 10, r.y + 14, o == 0 ? "TABLE A" : "TABLE B");
        const Table* t = tableShown(o);
        const int tsel = (int)value(kTableA + o * kOscStride);
        std::string name = tsel == kUserTable ? (t ? t->name : std::string("USER: LOAD A .WAV")) : std::string(kTableNames[pump::clampv(tsel, 0, kNumTables - 1)]);
        for (char& ch : name) ch = (char)std::toupper((unsigned char)ch);
        textAt(r.r() - 10, r.y + 14, name, kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        const bool off = value(kLevelA + o * kOscStride) <= 0.0f;
        if (!t || t->preview.empty()) return;
        // frames back to front: each one up and to the right of the one before
        const Rect p(r.x + 12, r.y + 26, r.w - 24, r.h - 34);
        const int F = (int)t->preview.size();
        const int show = std::min(F, 24);
        const float dx = 46.0f / std::max(1, show - 1), dy = 50.0f / std::max(1, show - 1);
        const float w = p.w - 46.0f, h = p.h - 50.0f;
        const float pos = pump::clampv(posShow[o], 0.0f, 1.0f);
        const float kp = pos * (show - 1);   // the position in the stack, between frames
        auto frameOf = [&](int k) { return show == 1 ? 0 : (int)std::lround((double)k * (F - 1) / (show - 1)); };
        const Color hot = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.35f);
        save();
        scissor(p.x, p.y, p.w, p.h);
        lineJoin(ROUND);
        // the stack, back to front; the frames near the position come forward
        for (int k = show - 1; k >= 0; --k)
        {
            const std::vector<float>& wv = t->preview[(size_t)frameOf(k)];
            const float ox = p.x + k * dx, oy = p.b() - h * 0.5f - k * dy;
            beginPath();
            for (int i = 0; i < (int)wv.size(); ++i)
            {
                const float x = ox + w * i / (wv.size() - 1), y = oy - wv[(size_t)i] * h * 0.45f;
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            const float near = off ? 0.0f : std::exp(-((k - kp) / 2.2f) * ((k - kp) / 2.2f));
            strokeColor(withAlpha2(theme.ink, 0.08f + 0.08f * (1.0f - (float)k / show) + 0.22f * near));
            strokeWidth(1.0f + 0.4f * near);
            stroke();
        }
        // the frame being played: blended between its neighbours, so it glides as the position moves
        const int fa = (int)std::floor(kp), fb = std::min(show - 1, fa + 1);
        const float m = kp - fa;
        const std::vector<float>& wa = t->preview[(size_t)frameOf(fa)];
        const std::vector<float>& wb = t->preview[(size_t)frameOf(fb)];
        const int n = (int)std::min(wa.size(), wb.size());
        auto curveAt = [&](float kk, const std::vector<float>& A, const std::vector<float>& B, float mm) {
            const float ox = p.x + kk * dx, oy = p.b() - h * 0.5f - kk * dy;
            beginPath();
            for (int i = 0; i < n; ++i)
            {
                const float v = A[(size_t)i] + (B[(size_t)i] - A[(size_t)i]) * mm;
                const float x = ox + w * i / (n - 1), y = oy - v * h * 0.45f;
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            return std::make_pair(ox, oy);
        };
        if (n > 1)
        {
            // a short trail behind it, only while the position moves (an LFO or envelope on POS, a drag)
            if (!off)
                for (int j = kTrail - 1; j >= 0; --j)
                {
                    const float pj = pump::clampv(posTrail[o][j], 0.0f, 1.0f);
                    if (std::fabs(pj - pos) < 0.004f) continue;
                    const float kj = pj * (show - 1);
                    const int ja = (int)std::floor(kj), jb = std::min(show - 1, ja + 1);
                    curveAt(kj, t->preview[(size_t)frameOf(ja)], t->preview[(size_t)frameOf(jb)], kj - ja);
                    strokeColor(withAlpha2(hot, 0.42f - 0.09f * j)); strokeWidth(1.6f); stroke();
                }
            const auto o0 = curveAt(kp, wa, wb, m);
            if (!off)
            {
                // a soft fill under it
                lineTo(o0.first + w, o0.second); lineTo(o0.first, o0.second); closePath();
                fillColor(withAlpha2(theme.neon, theme.dark ? 0.16f : 0.28f)); fill();
                curveAt(kp, wa, wb, m);
            }
            strokeColor(off ? theme.ink2 : hot); strokeWidth(2.6f); stroke();
            // a dot per voice playing, running along the wave: slowed far below the pitch, it shows the voices sound
            if (!off && voicesShow > 0)
            {
                const int dots = std::min(voicesShow, 4);
                for (int d = 0; d < dots; ++d)
                {
                    float u = phaseShow * 0.5f + d * 0.37f; u -= std::floor(u);
                    const int i = pump::clampv((int)std::lround(u * (n - 1)), 0, n - 1);
                    const float v = wa[(size_t)i] + (wb[(size_t)i] - wa[(size_t)i]) * m;
                    const float x = o0.first + w * i / (n - 1), y = o0.second - v * h * 0.45f;
                    beginPath(); circle(x, y, 7.0f); fillColor(withAlpha2(theme.neon, 0.35f)); fill();
                    beginPath(); circle(x, y, 3.5f); fillColor(theme.neon); fill(); strokeColor(theme.ink); strokeWidth(1.2f); stroke();
                }
            }
        }
        restore();
        char b[32]; std::snprintf(b, sizeof b, "POS %.0f%%   %d FRAMES", 100.0f * pos, t->frames);
        textAt(r.x + 10, r.b() - 8, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    // the filter's response at the shown cutoff (analog prototypes: four / two one-poles in a loop, or the SVF)
    void drawFilter()
    {
        const Rect& r = filterR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "FILTER");
        const int type = pump::clampv((int)value(kFilterType), 0, 3);
        textAt(r.r() - 10, r.y + 14, kFilterNames[type], kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        drawFreqGrid(p, 20.0, 20000.0, false);
        const double fc = std::exp2((double)cutShow), res = value(kRes) * 0.01;
        auto yOf = [&](double db) { return (float)(p.y + (18.0 - pump::clampv(db, -48.0, 18.0)) / 66.0 * p.h); };
        line(p.x, yOf(0), p.r(), yOf(0), withAlpha2(theme.ink, 0.25f), 1.0f);
        beginPath();
        for (int i = 0; i <= 120; ++i)
        {
            const double f = 20.0 * std::pow(1000.0, i / 120.0);
            const std::complex<double> s(0.0, f / fc);
            std::complex<double> H;
            if (type <= kLp12)
            {
                const std::complex<double> g = 1.0 / (1.0 + s);
                const double k = 4.2 * res;
                H = (type == kLp24 ? g * g * g * g : g * g) * (1.0 + 0.5 * k) / (1.0 + k * g * g * g * g);
            }
            else
            {
                const double q = 0.5 + 12.0 * res * res;
                const std::complex<double> den = s * s + s / q + 1.0;
                H = type == kBp ? (s / q) / den * q / q : (s * s) / den;
                if (type == kBp) H = (s / q) / den;
            }
            const float x = p.x + p.w * i / 120.0f, y = yOf(20.0 * std::log10(std::max(1e-9, std::abs(H))));
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(2.0f); lineJoin(ROUND); stroke();
        const float cx = freqToX(p, pump::clampv(fc, 20.0, 20000.0));
        line(cx, p.y, cx, p.b(), withAlpha2(theme.neon, 0.9f), 1.5f);
        char b[24];
        if (fc >= 1000) std::snprintf(b, sizeof b, "%.2f kHz", fc / 1000); else std::snprintf(b, sizeof b, "%.0f Hz", fc);
        textAt(r.x + 10, r.b() - 8, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
    }
    void drawVoices()
    {
        const Rect& r = voiceR;
        screenBox(r);
        caption(r.x + 8, r.y + 14, "VOICES");
        char b[16]; std::snprintf(b, sizeof b, "%d", voicesShow);
        textAt(r.cx(), r.y + 52, b, kBold, 28.0f, voicesShow > 0 ? theme.ink : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        const int maxV = value(kVoiceMode) < 0.5f ? (int)value(kPolyphony) : 1;
        std::snprintf(b, sizeof b, "OF %d", maxV);
        textAt(r.cx(), r.y + 76, b, kMono, 10.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        static const char* const mn[] = { "POLY", "MONO", "LEGATO" };
        textAt(r.cx(), r.y + 104, mn[pump::clampv((int)value(kVoiceMode), 0, 2)], kLabel, 10.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        const float bars = pump::clampv((float)voicesShow / std::max(1, maxV), 0.0f, 1.0f);
        const Rect vb(r.x + 10, r.y + 124, r.w - 20, 10);
        fillRect(vb, theme.panel2);
        fillRect(Rect(vb.x, vb.y, vb.w * bars, vb.h), theme.neon);
        strokeRect(vb, theme.ink, 1.0f);
    }
    // an ADSR drawn from its values (time axis: log-ish), with the level now as a bar
    void drawEnv(const Rect& r, int base, float now)
    {
        fillRect(r, theme.screen);
        strokeRect(r, theme.ink, 1.0f);
        auto tx = [](float ms) { return std::log10(1.0f + ms) / std::log10(1.0f + 10000.0f); };
        const float a = tx(value(base)), d = tx(value(base + 1)), s = value(base + 2) * 0.01f, rr = tx(value(base + 3));
        const float tot = a + d + 0.35f + rr;
        const Rect p = r.inset(4);
        auto X = [&](float t) { return p.x + p.w * t / tot; };
        auto Y = [&](float v) { return p.b() - v * p.h; };
        beginPath();
        moveTo(X(0), Y(0)); lineTo(X(a), Y(1));
        for (int i = 1; i <= 16; ++i) { const float t = (float)i / 16; lineTo(X(a + d * t), Y(s + (1 - s) * std::pow(1e-4f, t))); }
        lineTo(X(a + d + 0.35f), Y(s));
        for (int i = 1; i <= 16; ++i) { const float t = (float)i / 16; lineTo(X(a + d + 0.35f + rr * t), Y(s * std::pow(1e-4f, t))); }
        strokeColor(theme.ink); strokeWidth(1.8f); lineJoin(ROUND); stroke();
        fillRect(Rect(r.r() - 6, r.b() - r.h * pump::clampv(now, 0.0f, 1.0f), 4, r.h * pump::clampv(now, 0.0f, 1.0f)), theme.neon);
    }
    void drawLfo(const Rect& r, int shape)
    {
        fillRect(r, theme.screen);
        strokeRect(r, theme.ink, 1.0f);
        const Rect p = r.inset(4);
        beginPath();
        pump::fx::Rng rng(7);
        double held = rng.bi();
        for (int i = 0; i <= 64; ++i)
        {
            const double ph = 2.0 * i / 64.0;   // two cycles
            if (shape == kSampleHold && (i % 32) == 0) held = rng.bi();
            if (shape == kSmoothRandom) held = std::sin(ph * 2.3) * 0.7 + 0.3 * std::sin(ph * 5.1);
            const double v = lfoShape(shape, ph - std::floor(ph), held);
            const float x = p.x + p.w * i / 64.0f, y = p.cy() - (float)v * p.h * 0.45f;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(1.6f); stroke();
    }

    // ---- mouse: drag a table screen to move its position; the keyboard plays ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        for (int o = 0; o < 2; ++o)
            if (screen[o].contains(x, y))
            {
                const int pp = kPosA + o * kOscStride;
                if (dbl || cmdKey(mod)) { setParamGesture(pp, spec(pp).def); return false; }
                dragOsc = o; dragY = y; dragV = value(pp);
                beginEdit(pp);
                return true;
            }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (kbDown) { keyboard.mouseDrag(x, y); return; }
        if (dragOsc < 0) return;
        const int pp = kPosA + dragOsc * kOscStride;
        const float f = (mod & kModifierShift) ? 0.1f : 0.6f;
        setParam(pp, dragV + (dragY - y) * f);
        setStatus(paramStatus(pp));
    }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; return; }
        if (dragOsc >= 0) { endEdit(kPosA + dragOsc * kOscStride); dragOsc = -1; }
    }
    bool bodyScroll(float x, float y, float dy, uint) override
    {
        for (int o = 0; o < 2; ++o)
            if (screen[o].contains(x, y))
            {
                const int tp = kTableA + o * kOscStride;
                setParamGesture(tp, (float)((((int)value(tp) + (dy > 0 ? -1 : 1)) % kNumTables + kNumTables) % kNumTables));
                return true;
            }
        return false;
    }
    void bodyHover(float x, float y) override
    {
        for (int o = 0; o < 2; ++o)
            if (screen[o].contains(x, y)) { setStatus("Drag up / down: position (Shift = fine, double-click = reset); wheel: table"); return; }
        if (filterR.contains(x, y)) setStatus("The filter's response where the latest note has it now (envelope, key and mods included)");
        else if (keyboard.r.contains(x, y)) setStatus("Play: velocity follows where you strike the key (top soft, bottom hard); drag across keys");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag a table screen", "up / down = position; wheel = table" },
            { "LOAD", "a .wav wavetable (2048-sample frames) into USER" },
            { "Keyboard", "click to play, drag across; MIDI from the host plays too" },
        };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%d VOICES   %s", voicesShow, value(kVoiceMode) < 0.5f ? "POLY" : (value(kVoiceMode) < 1.5f ? "MONO" : "LEGATO"));
        return b;
    }

private:
    struct Osc {
        Rect panel;
        MenuButton* table;
        Button* load;
        Dial *pos, *level, *pan, *uni, *det, *spr, *oct, *semi, *fine;
    };
    PluginTable* plug = nullptr;
    Osc osc[2];
    Rect screen[2], filterR, voiceR, meterL, meterR, filterPanel, voicePanel, ampPanel, modPanel, lfo1Panel, lfo2Panel, matrixPanel;
    Dial *subDial, *cutDial, *resDial, *drvDial, *envDial, *keyDial, *noiseDial, *polyDial, *glideDial, *bendDial, *volDial, *velDial;
    Dial* ampD[4]; Dial* modD[4];
    Dial *lfo1Rate, *lfo2Rate;
    MenuButton *lfo1Shape, *lfo2Shape, *lfo2Div;
    Button* lfo2Sync;
    Segmented *ftype, *modeSeg;
    MenuButton* src[4]; MenuButton* dst[4]; SliderRow* amt[4];
    KeyboardUI keyboard;
    bool kbDown = false;
    int which = 0, dragOsc = -1, voicesShow = 0;
    static constexpr int kTrail = 4;
    float dragY = 0, dragV = 0, posShow[2] = { 0.4f, 0.3f }, cutShow = 12.5f;
    float posTrail[2][kTrail] = { { 0.4f, 0.4f, 0.4f, 0.4f }, { 0.3f, 0.3f, 0.3f, 0.3f } }, trailT[2] = {}, phaseShow = 0.0f;   // the trail; the dots' run
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UITable)
};

UI* createUI() { return new UITable(); }

END_NAMESPACE_DISTRHO
