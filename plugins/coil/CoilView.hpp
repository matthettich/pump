// pump COIL / TWANG: the SPRINGS screen, shared by both windows. Every voice is drawn the way SPRING draws its tank: a
// helix between two hooks, seen from the side, whose coils bunch and spread with the sound really running along it
// (the voice's own output, sent down the spring and reflected back), its sleeve glowing as it rings; back strands thin
// and faint, front strands in ink. A lower note is a longer, looser spring with more coils. A kick shakes the screen.
// Resting voices are drawn as still, dim springs.
#pragma once
#include "ui/PumpUI.hpp"
#include "CoilEngine.hpp"

namespace pump { namespace coil {

class SpringsView {
public:
    static constexpr int kWaveN = 512;

    // UI tick: the newest wave and level of every voice
    void update(const Engine* e, float dt)
    {
        if (!e) return;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            e->visV[(size_t)i].readSmooth(wave[i], kWaveN);
            const float lv = e->voiceLevel(i);
            energy[i] = std::max(std::min(1.0f, std::sqrt(std::max(0.0f, lv)) * 3.0f), energy[i] * std::exp(-dt / 0.25f));
            note[i] = e->voiceNote(i);
            if (note[i] >= 0) lastNote[i] = note[i];
        }
        visRate = e->visRate();
    }
    // the same for any instrument whose voices are springs (BOING's pads): one row's wave, level and note
    void updateRow(int i, const ScopeRing& ring, float level, int noteNum, double rate, float dt)
    {
        if (i < 0 || i >= kMaxVoices) return;
        ring.readSmooth(wave[i], kWaveN);
        energy[i] = std::max(std::min(1.0f, std::sqrt(std::max(0.0f, level)) * 3.0f), energy[i] * std::exp(-dt / 0.25f));
        note[i] = noteNum; if (noteNum >= 0) lastNote[i] = noteNum;
        visRate = rate;
    }
    const char* const* labels = nullptr;   // a name per row (else the note it plays)

    void draw(DISTRHO::PumpUI& ui, const Rect& r, float kickAgeS, const char* title, const char* right)
    {
        using P = DISTRHO::PumpUI;
        const Theme& th = ui.theme;
        ui.screenBox(r);
        ui.caption(r.x + 10, r.y + 14, title);
        if (right) ui.textAt(r.r() - 10, r.y + 14, right, P::kLabel, 10.0f, th.ink2, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, 0.8f);
        // a kick shakes the drawing for a moment
        float sx = 0.0f, sy = 0.0f;
        if (kickAgeS < 1.2f)
        {
            const float e = std::exp(-kickAgeS / 0.18f) * 6.0f;
            sx = e * std::sin(kickAgeS * 71.0f);
            sy = e * 0.6f * std::sin(kickAgeS * 53.0f + 1.0f);
        }
        const Rect p(r.x + 12, r.y + 26, r.w - 24, r.h - 32);
        ui.save();
        ui.scissor(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
        ui.translate(sx, sy);
        const float rowH = p.h / kMaxVoices;
        static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        int shown = 0;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const float yc = p.y + rowH * (i + 0.5f);
            const bool on = note[i] >= 0;
            if (on) ++shown;
            const int nv = lastNote[i] >= 0 ? (lastNote[i] >= 1000 ? lastNote[i] - 1000 : lastNote[i]) : 48 + 5 * i;
            // the note, then the hooks
            if (labels) ui.textAt(p.x, yc, labels[i], P::kLabel, 8.5f, on ? th.ink : th.ink2, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, 0.4f);
            else if (on)
            {
                char b[12]; std::snprintf(b, sizeof b, "%s%d", nn[((nv % 12) + 12) % 12], nv / 12 - 1);
                ui.textAt(p.x, yc, b, P::kMono, 9.5f, th.ink, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
            }
            const float hookX0 = p.x + (labels ? 52 : 34), hookX1 = p.r() - 4;
            // lower notes: longer springs (the drawing spans the screen; length shows as the number of coils)
            const double f0 = 440.0 * std::exp2((nv - 69) / 12.0);
            const double travel = 0.5 / std::max(20.0, f0);   // one way along the spring: half the period
            const int coils = clampv((int)std::lround(78.0 - 0.7 * (nv - 24)), 26, 72);   // a lower note: more turns, a longer spring
            const float tS = (float)std::min((double)kWaveN / 2 - 4, travel * visRate);
            const float R = std::min(rowH * 0.36f, 10.0f);
            const float x0 = hookX0 + 10, x1 = hookX1 - 10;
            ui.line(hookX0, yc, x0, yc, th.ink2, 1.1f);
            ui.line(x1, yc, hookX1, yc, th.ink2, 1.1f);
            ui.fillRect(Rect(hookX0 - 3, yc - R - 2, 3, 2 * R + 4), on ? th.ink2 : th.dim);
            ui.fillRect(Rect(hookX1, yc - R - 2, 3, 2 * R + 4), on ? th.ink2 : th.dim);
            drawCoil(ui, i, x0, x1, yc, R, coils, tS, on);
        }
        ui.restore();
        if (!shown) ui.textAt(r.cx(), r.b() - 14, "PLAY A NOTE (OR KICK)", P::kLabel, 9.5f, th.dim, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, 1.0f);
    }

private:
    // the displacement shown for a sample value: log-compressed, so a quiet tail still visibly moves
    static float shownV(float v)
    {
        const float a = std::log10(1.0f + 1000.0f * std::fabs(v)) / 3.0f;
        return std::copysign(std::min(1.0f, a), v);
    }
    void drawCoil(DISTRHO::PumpUI& ui, int l, float x0, float x1, float yc, float R, int coils, float tS, bool on)
    {
        using P = DISTRHO::PumpUI;
        const Theme& th = ui.theme;
        const float len = x1 - x0;
        const float e = on ? clampv(energy[l], 0.0f, 1.0f) : 0.0f;
        // the sleeve lights up as it rings
        ui.beginPath();
        ui.roundedRect(x0 - 4.0f, yc - R - 3.0f, len + 8.0f, 2.0f * R + 6.0f, R + 3.0f);
        ui.fillColor(P::withAlpha2(th.neon, (th.dark ? 0.04f : 0.08f) + (th.dark ? 0.38f : 0.62f) * e));
        ui.fill();
        // the wave along the spring: the voice's sound leaving one end, and its reflection coming back from the other
        auto at = [&](float pos) {
            const int i = clampv((int)pos, 0, kWaveN - 2);
            const float f = pos - (float)i;
            return wave[l][i] + (wave[l][i + 1] - wave[l][i]) * f;
        };
        auto waveAt = [&](float u) {
            if (!on) return 0.0f;
            const float pf = (float)(kWaveN - 1) - u * tS, pb = (float)(kWaveN - 1) - (2.0f - u) * tS;
            return shownV(at(pf) + 0.7f * at(pb));
        };
        static thread_local float px[600], py[600];
        static thread_local bool front[600];
        const int pts = std::min(std::max(48, coils * 8), 599);
        for (int i = 0; i <= pts; ++i)
        {
            const float u = (float)i / (float)pts;
            const float w = (i == 0 || i == pts) ? 0.0f : waveAt(u);
            const float a = (float)kTwoPi * (float)coils * u;
            const float taper = std::min(1.0f, std::min(u, 1.0f - u) * 30.0f);
            px[i] = x0 + (u + 0.016f * w * taper) * len + R * 0.45f * std::cos(a) * taper;
            py[i] = yc + R * (1.0f + 0.25f * std::fabs(w)) * std::sin(a) * taper + 3.5f * w * taper;
            front[i] = std::cos(a) >= 0.0f;
        }
        const Color ink = on ? th.ink : P::withAlpha2(th.ink, 0.45f);
        for (int pass = 0; pass < 2; ++pass)
        {
            ui.beginPath();
            bool open = false;
            for (int i = 1; i <= pts; ++i)
            {
                if (front[i - 1] != (pass == 1)) { open = false; continue; }
                if (!open) { ui.moveTo(px[i - 1], py[i - 1]); open = true; }
                ui.lineTo(px[i], py[i]);
            }
            ui.strokeColor(pass ? ink : P::withAlpha2(th.ink, on ? 0.32f : 0.16f));
            ui.strokeWidth(pass ? 1.6f : 1.0f);
            ui.lineJoin(NanoVG::ROUND);
            ui.stroke();
        }
    }

    float wave[kMaxVoices][kWaveN] = {};
    float energy[kMaxVoices] = {};
    int note[kMaxVoices] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    int lastNote[kMaxVoices] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    double visRate = 12000.0;
};

}} // namespace pump::coil
