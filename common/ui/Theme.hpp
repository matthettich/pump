// pump UI: colours. Each plugin has one light-neon accent; the palette is built the way Thunder Plus's Simple
// skins are (a tinted background, one deep ink colour for text/outlines/fills, a dim tone for faint lines and the
// offset blocks, a soft highlight), in a light and a dark variant.
#pragma once

#include "Color.hpp"
#include <cmath>

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

// A wheel of 16 colours (the suite's accents, around the hue circle) for things that must be told apart: COIL's
// springs, SING's words. deep = the colour's dark shade (text and strands in light mode); wheelInk picks per theme.
inline Color wheelColour(int i, bool deep)
{
    static const unsigned neon[16] = { 0xFF7466, 0xFFD04A, 0xC8FF4D, 0x5CFFB4, 0x4DF3FF, 0x73B4FF, 0xB9A0FF, 0xFF6FB5,
                                       0xFFB24F, 0xDCEB5A, 0x8CFF5A, 0x3DF5D0, 0x5CCBFF, 0x949FFF, 0xDD77FF, 0xFF92C2 };
    static const unsigned ink[16]  = { 0x8C2318, 0x6B4700, 0x3B5C00, 0x006E44, 0x00636D, 0x1443AC, 0x4528B5, 0x8C1257,
                                       0x934000, 0x4E5700, 0x235C00, 0x006652, 0x00527A, 0x262C99, 0x650C8A, 0x8A1450 };
    const int k = ((i % 16) + 16) % 16;
    return hexColor(deep ? ink[k] : neon[k]);
}
inline Color wheelInk(int i, bool dark) { return dark ? mixColor(wheelColour(i, false), hexColor(0xFFFFFF), 0.18f) : wheelColour(i, true); }

// OKLCH (lightness 0..1, chroma, hue in degrees) to sRGB, clipped: colours of equal lightness read at the same
// strength whatever their hue (Thunder Plus's Simple rainbow skin is built on it).
inline Color oklchColor(float L, float C, float hDeg, float alpha = 1.0f)
{
    const double h = hDeg * 3.14159265358979323846 / 180.0;
    const double a = C * std::cos(h), b = C * std::sin(h);
    const double l_ = L + 0.3963377774 * a + 0.2158037573 * b;
    const double m_ = L - 0.1055613458 * a - 0.0638541728 * b;
    const double s_ = L - 0.0894841775 * a - 1.2914855480 * b;
    const double l = l_ * l_ * l_, m = m_ * m_ * m_, s3 = s_ * s_ * s_;
    double rgb[3] = { 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s3,
                      -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s3,
                      -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s3 };
    for (double& c : rgb)
    {
        c = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
        c = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    }
    return Color((float)rgb[0], (float)rgb[1], (float)rgb[2], alpha);
}

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
    inline Accent modal()  { return { "modal",  hexColor(0xFFC15E), hexColor(0x744400), hexColor(0x3D7BFF) }; }   // honey
    inline Accent orbit()  { return { "orbit",  hexColor(0xBE8CFF), hexColor(0x47148C), hexColor(0x2FD3A0) }; }   // nebula
    inline Accent fold()   { return { "fold",   hexColor(0xFFA3E6), hexColor(0x8A1470), hexColor(0x2FC4B0) }; }   // blush
    inline Accent vector() { return { "vector", hexColor(0x6BFFE1), hexColor(0x00604F), hexColor(0xFF6BB0) }; }   // aqua
    inline Accent strike() { return { "strike", hexColor(0xFFD98C), hexColor(0x6E4A00), hexColor(0x4D7BFF) }; }   // sand
    inline Accent freeze() { return { "freeze", hexColor(0xC9E8FF), hexColor(0x1E4A73), hexColor(0xFF9A5C) }; }   // frost
    inline Accent tract()  { return { "tract",  hexColor(0xFF8FA3), hexColor(0x8A1A2E), hexColor(0x3FC9B0) }; }   // flush
    inline Accent acid()   { return { "acid",   hexColor(0xC9CED6), hexColor(0x2E333B), hexColor(0xFF6A2B) }; }   // gray (the notes in colour)
    inline Accent x0x()    { return { "x0x",    hexColor(0xF3E3BC), hexColor(0x5B4326), hexColor(0xE8402F) }; }   // cream (the rows in colour)
    inline Accent gloss()  { return { "gloss",  hexColor(0xFF6EC7), hexColor(0x8A0F5C), hexColor(0x2FE0C8) }; }   // hot pink
    inline Accent brk()    { return { "break",  hexColor(0xB5D65C), hexColor(0x3D5200), hexColor(0xD9549B) }; }   // avocado
    inline Accent chop()   { return { "chop",   hexColor(0xC6FF6B), hexColor(0x3E6100), hexColor(0xD45CFF) }; }   // lime
    // room measurement and correction (the app in room/ uses it too, in app.js)
    inline Accent room()   { return { "room",   hexColor(0xFFC86B), hexColor(0x6E4300), hexColor(0x2F7BFF) }; }   // amber
    // auto mastering
    inline Accent master() { return { "master", hexColor(0xDCDCE3), hexColor(0x3B3B44), hexColor(0x9A63FF) }; }   // grey, the controls in rainbow
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

    // One control's own colour (Thunder Plus's Simple rainbow skin): its outline, text and "on" fill take the hue at
    // one OKLCH lightness, the rest of the theme stays. PumpUI applies it around a control whose `tint` is set.
    void tint(float hueDeg)
    {
        if (!dark)
        {
            ink = oklchColor(0.55f, 0.17f, hueDeg);
            ink2 = oklchColor(0.62f, 0.12f, hueDeg);
            dim = oklchColor(0.86f, 0.07f, hueDeg);
            neon = oklchColor(0.72f, 0.14f, hueDeg);
            neonInk = oklchColor(0.30f, 0.10f, hueDeg);
            on = hexColor(0xFFFFFF);
        }
        else
        {
            ink = oklchColor(0.80f, 0.14f, hueDeg);
            ink2 = oklchColor(0.68f, 0.11f, hueDeg);
            dim = oklchColor(0.40f, 0.08f, hueDeg);
            neon = oklchColor(0.72f, 0.15f, hueDeg);
            neonInk = oklchColor(0.18f, 0.04f, hueDeg);
            on = bg;
            glow = withAlpha(neon, 0.22f);
        }
    }
};

} // namespace pump
