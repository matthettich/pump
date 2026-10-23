# …pump

Mastering plugins by **wake**, in the look of Thunder Plus's *Simple* skins: flat and square, ink outlines with an
offset block, Share Tech Mono, one light-neon colour per plugin, light or dark. VST3 and CLAP on Windows and macOS
(plus Audio Units on macOS), built for Ableton Live and any other host.

| Plugin | Colour | Inspired by | What it does |
|---|---|---|---|
| **pump EQ** | aqua | FabFilter Pro-Q | 24 bands; analog-matched curves with no cramping; cuts to 96 dB/oct; dynamic bands; stereo/L/R/mid/side per band; zero latency or linear phase |
| **pump COMP** | pink | FabFilter Pro-C | Clean, Glue, Opto, Punch, Vari-Mu and Pump styles; lookahead; external sidechain with sidechain EQ; stereo link and mid/side |
| **pump LIMIT** | lime | FabFilter Pro-L, Ozone Maximizer | true-peak limiter with seven styles, up to 8x oversampling, dither, loudness metering and targets |
| **pump CALM** | violet | soothe | dynamic resonance suppressor with a sensitivity curve, mid/side and delta listening |
| **pump TONE** | mint | Gullfoss | automatic balance: Recover, Tame, Bias, Brighten, Body |
| **pump BAND** | orange | Pro-MB, Ozone Dynamics | multiband compression, expansion and upward compression; analog or linear-phase crossovers |
| **pump WIDE** | blue | Ozone Imager | multiband width and stereoize, mono bass, vectorscope and correlation |
| **pump HEAT** | coral | Saturn, Ozone Exciter | multiband warm/tape/tube/triode/retro/clip saturation, oversampled |
| **pump METER** | yellow | Youlean, Ozone | LUFS (momentary, short-term, integrated), LRA, true peak, PLR/PSR, history |

## Download

The newest builds are always on the [latest release](https://github.com/matthettich/pump/releases/tag/latest):
`pump-windows.zip`, `pump-macos.zip` (Apple Silicon + Intel). Each zip has an `INSTALL.txt`.

## Using them

Every plugin follows FabFilter's and iZotope's conventions. Press **?** in a plugin for its full list.

- drag a control (Shift = fine), mouse wheel steps it, Ctrl/Cmd-click resets it
- double-click or right-click to type a value: `1k`, `A4`, `C#3+13`, `2x` (= +6 dB), `50%` (halfway); Tab = next field
- Ctrl/Cmd+Z undo, Shift+Ctrl/Cmd+Z or Ctrl+Y redo; A/B compare with COPY; factory presets with ◀ ▶
- hold **Alt** on a band to solo it while held (EQ nodes, multiband regions); on empty spectrum space, Alt-hold listens
  to the range under the mouse (wheel = width)
- DARK switches the look and is remembered for new instances

## Building

```
git clone --recursive https://github.com/matthettich/pump
make            # all plugins into bin/  (Windows: cross-compile with MinGW, see .github/workflows/build.yml)
make tests      # offline DSP tests
```

See [docs/DEVELOPING.md](docs/DEVELOPING.md) for the framework, the shared DSP library and how to add a plugin. The DSP
in `common/dsp` and each plugin's `*Engine.hpp` has no framework code, so it can also be compiled to WebAssembly for
Thunder Plus.

## Licences

© wake. Third-party components (DPF, pffft, NanoVG, pugl, fonts) are permissively licensed; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
