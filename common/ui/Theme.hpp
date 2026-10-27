// pump UI: colours. Each plugin has one light-neon accent; the palette is built the way Thunder Plus's Simple
// skins are (a tinted background, one deep ink colour for text/outlines/fills, a dim tone for faint lines and the
// offset blocks, a soft highlight), in a light and a dark variant.
#pragma once

#include "Color.hpp"

namespace pump {

using DGL_NAMESPACE::Color;

struct Accent {
    const char* id;
    Color neon;     // the light-neon highlight (fills, curves)
    Color deep;     // the ink colour in light mode
    Color second;   // a complementary colour (second curves, side channel, selections)
};

inline Color hexColor(unsigned rgb, float a = 1.0f)
{
    return Color((int)((rgb >> 16) & 255), (int)((rgb >> 8) & 255), (int)(rgb & 255), a);
}
inline Color mixColor(const Color& a, const Color& b, float t)   // t = amount of b
{
    return Color(a.red + (b.red - a.red) * t, a.green + (b.green - a.green) * t, a.blue + (b.blue - a.blue) * t, a.alpha + (b.alpha - a.alpha) * t);
}
inline Color withAlpha(const Color& c, float a) { Color o = c; o.alpha = a; return o; }

// The suite's accents (one per plugin).
namespace accents {
    inline Accent eq()    { return { "eq",    hexColor(0x4DF3FF), hexColor(0x00636D), hexColor(0xFF4FA0) }; }   // aqua
    inline Accent comp()  { return { "comp",  hexColor(0xFF74DC), hexColor(0x8E0F68), hexColor(0x19B5A5) }; }   // pink
    inline Accent limit() { return { "limit", hexColor(0xC8FF4D), hexColor(0x3B5C00), hexColor(0x7A5CFF) }; }   // lime
    inline Accent calm()  { return { "calm",  hexColor(0xB9A0FF), hexColor(0x4528B5), hexColor(0xFFAE3B) }; }   // violet
    inline Accent tone()  { return { "tone",  hexColor(0x5CFFB4), hexColor(0x006E44), hexColor(0xFF6E4A) }; }   // mint
    inline Accent band()  { return { "band",  hexColor(0xFFB24F), hexColor(0x934000), hexColor(0x2F86FF) }; }   // orange
    inline Accent wide()  { return { "wide",  hexColor(0x73B4FF), hexColor(0x1443AC), hexColor(0xFF8A3D) }; }   // blue
    inline Accent heat()  { return { "heat",  hexColor(0xFF8676), hexColor(0xA21F12), hexColor(0x2A9BC9) }; }   // coral
    inline Accent meter() { return { "meter", hexColor(0xFFF04F), hexColor(0x625800), hexColor(0xB64DFF) }; }   // yellow
    // the creative effects
    inline Accent tube()   { return { "tube",   hexColor(0xFF5C7C), hexColor(0x8C0A2E), hexColor(0x1FB5A0) }; }   // cherry
    inline Accent snap()   { return { "snap",   hexColor(0x8CFF5A), hexColor(0x235C00), hexColor(0xE0459B) }; }   // grass
    inline Accent gate()   { return { "gate",   hexColor(0x5CCBFF), hexColor(0x00527A), hexColor(0xFF8F3D) }; }   // sky
    inline Accent echo()   { return { "echo",   hexColor(0xDD77FF), hexColor(0x650C8A), hexColor(0x4FB33A) }; }   // fuchsia
    inline Accent spring() { return { "spring", hexColor(0xFFD04A), hexColor(0x6B4700), hexColor(0x3D6BFF) }; }   // gold
    inline Accent verb()   { return { "verb",   hexColor(0x949FFF), hexColor(0x262C99), hexColor(0xFFA84D) }; }   // periwinkle
    inline Accent grain()  { return { "grain",  hexColor(0x3DF5D0), hexColor(0x006652), hexColor(0xFF6F91) }; }   // seafoam
    inline Accent vox()    { return { "vox",    hexColor(0xD38CFF), hexColor(0x50138C), hexColor(0x1FB89E) }; }   // lilac
    inline Accent tune()   { return { "tune",   hexColor(0xFFAA85), hexColor(0x8A3210), hexColor(0x2E86E0) }; }   // peach
    inline Accent crush()  { return { "crush",  hexColor(0xDCEB5A), hexColor(0x4E5700), hexColor(0xB24BE8) }; }   // olive
    inline Accent swirl()  { return { "swirl",  hexColor(0xFF92C2), hexColor(0x8A1450), hexColor(0x22A88E) }; }   // rose
    inline Accent stutter(){ return { "stutter",hexColor(0x8FE8FF), hexColor(0x064A63), hexColor(0xFF6A3D) }; }   // ice
    inline Accent amp()    { return { "amp",    hexColor(0xF5A672), hexColor(0x6E3410), hexColor(0x2C9FD0) }; }   // copper
    // the instruments
    inline Accent table()  { return { "table",  hexColor(0x7DF0A0), hexColor(0x0B5A2A), hexColor(0xE04FD0) }; }   // spring green
    inline Accent mono()   { return { "mono",   hexColor(0xFF8A3D), hexColor(0x7A2E00), hexColor(0x2F8FE0) }; }   // blood orange
    inline Accent poly()   { return { "poly",   hexColor(0xA9B8FF), hexColor(0x27307A), hexColor(0xE8A23C) }; }   // dusk blue
    inline Accent wob()    { return { "wob",    hexColor(0xA36BFF), hexColor(0x3A0F94), hexColor(0x3FD9A0) }; }   // ultraviolet
    inline Accent kit()    { return { "kit",    hexColor(0xFF5E57), hexColor(0x8C0F0A), hexColor(0x2FB8C9) }; }   // signal red
    inline Accent machine(){ return { "machine",hexColor(0xB8C4DD), hexColor(0x354163), hexColor(0xFF7A45) }; }   // silver
    inline Accent sing()   { return { "sing",   hexColor(0xF45CFF), hexColor(0x6A0A80), hexColor(0x2EE6C0) }; }   // electric orchid
    inline Accent coil()   { return { "coil",   hexColor(0xFF7466), hexColor(0x8C2318), hexColor(0x1FB8A8) }; }   // coral
    inline Accent twang()  { return { "twang",  hexColor(0x2BD9C2), hexColor(0x055A50), hexColor(0xFF8A4D) }; }   // lagoon
    inline Accent phase()  { return { "phase",  hexColor(0x6B85FF), hexColor(0x1A2A99), hexColor(0xFFB43D) }; }   // cobalt
    inline Accent life()   { return { "life",   hexColor(0xADFF2F), hexColor(0x3A6600), hexColor(0xC24DFF) }; }   // acid
    inline Accent boing()  { return { "boing",  hexColor(0xFF6FB5), hexColor(0x8C1257), hexColor(0x3DD6C4) }; }   // flamingo
    inline Accent prism()  { return { "prism",  hexColor(0x7FE8FF), hexColor(0x0A5A6E), hexColor(0xFF7AA8) }; }   // glacier
}

struct Theme {
    bool dark = false;
    Color bg, panel, panel2, ink, ink2, dim, neon, neonInk, on, second, red, grid, screen, glow;

    void build(const Accent& a, bool darkMode)
    {
        dark = darkMode;
        const Color white = hexColor(0xFFFFFF), black = hexColor(0x07070B);
        if (!dark)
        {
            bg     = mixColor(white, a.neon, 0.16f);
            panel  = mixColor(white, a.neon, 0.08f);
            panel2 = mixColor(white, a.neon, 0.30f);
            ink    = a.deep;
            ink2   = mixColor(a.deep, bg, 0.38f);
            dim    = mixColor(mixColor(a.neon, a.deep, 0.30f), white, 0.42f);
            neon   = a.neon;
            neonInk = a.deep;                       // text on a neon fill
            on     = mixColor(white, a.neon, 0.06f); // text on an ink fill
            second = mixColor(a.second, black, 0.08f);
            red    = hexColor(0xD0302A);
            grid   = withAlpha(a.deep, 0.11f);
            screen = mixColor(white, a.neon, 0.05f);
            glow   = withAlpha(a.neon, 0.0f);
        }
        else
        {
            bg     = mixColor(black, a.neon, 0.07f);
            panel  = mixColor(black, a.neon, 0.10f);
            panel2 = mixColor(black, a.neon, 0.19f);
            ink    = mixColor(a.neon, white, 0.18f);
            ink2   = mixColor(a.neon, bg, 0.38f);
            dim    = mixColor(a.neon, bg, 0.70f);
            neon   = a.neon;
            neonInk = mixColor(black, a.neon, 0.05f);
            on     = mixColor(black, a.neon, 0.05f);
            second = mixColor(a.second, white, 0.15f);
            red    = hexColor(0xFF5A4E);
            grid   = withAlpha(a.neon, 0.10f);
            screen = mixColor(black, a.neon, 0.04f);
            glow   = withAlpha(a.neon, 0.22f);
        }
    }
};

} // namespace pump
