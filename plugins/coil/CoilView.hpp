// pump COIL / TWANG: the SPRINGS screen, shared by both windows. Every sounding voice is a coil: tighter for a higher
// note, shaking with its level, its note at the left.
#pragma once
#include "ui/PumpUI.hpp"
#include "CoilEngine.hpp"

namespace pump { namespace coil {

inline void drawSprings(DISTRHO::PumpUI& ui, const Rect& r, const Engine* e, float t, const char* title, const char* right)
{
    using P = DISTRHO::PumpUI;
    const Theme& th = ui.theme;
    ui.screenBox(r);
    ui.caption(r.x + 10, r.y + 14, title);
    if (right) ui.textAt(r.r() - 10, r.y + 14, right, P::kLabel, 10.0f, th.ink2, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, 0.8f);
    const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 34);
    static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float rowH = p.h / kMaxVoices;
    int shown = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const int note = e ? e->voiceNote(i) : -1;
        const float lv = e ? e->voiceLevel(i) : 0.0f;
        const float y = p.y + rowH * (i + 0.5f);
        if (note < 0)
        {
            ui.line(p.x + 44, y, p.r(), y, P::withAlpha2(th.ink, 0.10f), 1.0f);
            continue;
        }
        ++shown;
        const int nnum = note >= 1000 ? note - 1000 : note;
        char b[12]; std::snprintf(b, sizeof b, "%s%d", nn[((nnum % 12) + 12) % 12], nnum / 12 - 1);
        ui.textAt(p.x, y, b, P::kMono, 10.0f, th.ink, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
        // turns: more for a higher note; the swing: its level
        const float turns = 6.0f + 0.35f * (float)clampv(nnum - 24, 0, 84);
        const float amp = std::min(rowH * 0.42f, 2.0f + std::sqrt(std::max(0.0f, lv)) * rowH * 1.6f);
        const float x0 = p.x + 44, x1 = p.r();
        ui.beginPath();
        const int steps = 220;
        for (int k = 0; k <= steps; ++k)
        {
            const float u = (float)k / steps;
            const float wobble = std::sin(u * 3.14159f) * std::sin(t * (9.0f + 0.13f * nnum) + u * 6.0f) * amp * 0.35f;
            const float yy = y + std::sin(u * turns * 6.28318f) * amp * 0.65f + wobble;
            const float xx = x0 + (x1 - x0) * u;
            if (k == 0) ui.moveTo(xx, yy); else ui.lineTo(xx, yy);
        }
        ui.strokeColor(mixColor(th.neon, th.ink, th.dark ? 0.0f : 0.35f));
        ui.strokeWidth(1.3f + std::min(1.5f, lv * 6.0f));
        ui.lineJoin(NanoVG::ROUND);
        ui.stroke();
    }
    if (!shown) ui.textAt(r.cx(), r.cy() + 6, "PLAY A NOTE (OR KICK)", P::kLabel, 10.0f, th.dim, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, 1.0f);
}

}} // namespace pump::coil
