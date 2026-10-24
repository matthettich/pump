# …pump

Mastering plugins by **wake**, in the look of Thunder Plus's *Simple* skins: flat and square, ink outlines with an
offset block, Share Tech Mono, one light-neon colour per plugin, light or dark. VST3 and CLAP on Windows and macOS
(plus Audio Units on macOS), built for Ableton Live and any other host.

| Plugin (name in your host) | Colour | Reference | What it does |
|---|---|---|---|
| **pump EQ** Equalizer | aqua | EQ | 24 bands; analog-matched curves with no cramping; cuts to 96 dB/oct; dynamic bands; stereo/L/R/mid/side per band; zero latency or linear phase |
| **pump COMP** Compressor | pink | compressor | Clean, Glue, Opto, Punch, Vari-Mu and Pump styles; lookahead; external sidechain with sidechain EQ; stereo link and mid/side |
| **pump LIMIT** Limiter | lime | limiter, maximizer | true-peak limiter with seven styles, up to 8x oversampling, dither, loudness metering and targets |
| **pump CALM** Resonance Suppressor | violet | resonance suppressor | dynamic resonance suppressor with a sensitivity curve, mid/side and delta listening |
| **pump TONE** Tonal Balance | mint | auto-balance | automatic balance: Recover, Tame, Bias, Brighten, Body |
| **pump BAND** Multiband Dynamics | orange | multiband, multiband dynamics | multiband compression, expansion and upward compression; analog or linear-phase crossovers |
| **pump WIDE** Stereo Imager | blue | imager | multiband width and stereoize, mono bass, vectorscope and correlation |
| **pump HEAT** Saturator | coral | saturator, exciter | multiband warm/tape/tube/triode/retro/clip saturation, oversampled |
| **pump METER** Loudness Meter | yellow | loudness meter, standard | LUFS (momentary, short-term, integrated), LRA, true peak, PLR/PSR, history |

## Download

The newest builds are always on the [latest release](https://github.com/matthettich/pump/releases/tag/latest):
`pump-macos.pkg` (installer, Apple Silicon + Intel), `pump-macos.zip`, `pump-windows.zip`. Each zip has an
`INSTALL.txt`.

## Using them

Every plugin follows the usual conventions. Press **?** in a plugin for its full list.

- drag a control (Shift = fine), mouse wheel steps it, Ctrl-click (Cmd-click on a Mac) resets it; on a Mac,
  Ctrl-click is a right-click, as everywhere else in macOS
- double-click or right-click to type a value: `1k`, `A4`, `C#3+13`, `2x` (= +6 dB), `50%` (halfway); Tab = next field
- Ctrl/Cmd+Z undo, Shift+Ctrl/Cmd+Z or Ctrl+Y redo; A/B compare with COPY; factory presets with ◀ ▶
- hold **Alt** on a band to solo it while held (EQ nodes, multiband regions); drag while soloed to move it (up = narrower,
  down = wider). On empty spectrum space, Alt-hold listens to the range under the mouse (drag to sweep; up/down or wheel = width)
- DARK switches the look and is remembered for new instances
- EQ dynamic bands (EQ style): select a band, turn on DYNAMIC, set RANGE and THRESH; Alt+double-click on the
  curve makes a dynamic band directly
- COMP has a stereo sidechain input: set SC to External and route a track to the plugin's sidechain in your host

## Presets

Every plugin's preset menu has three groups of factory presets, plus your own:

- **Factory**: general starting points.
- **Sources**: for single tracks and buses (Vocals, Drums, Kick, Snare, Bass, Guitars, Keys, Synths, Pads, Bus). They
  are set for sources peaking around -6 dBFS; move THRESH or GAIN by the same amount for quieter or hotter tracks.
- **Chains**: seven matching mastering chains (Pop, EDM, Hip-hop, Rock, Acoustic & jazz, Lo-fi, Streaming -14). Load
  the same name in each plugin, in this order: EQ, CALM, COMP, BAND, HEAT, TONE, WIDE, LIMIT, METER. On dense mixes,
  lower LIMIT's GAIN by 1-3 dB to land on the target loudness.
- **Your presets**: *Save preset…* at the end of the menu. They're stored as small text files (one folder per plugin)
  in `Documents/wake/pump/Presets/` on macOS and Windows, `~/.local/share/wake/pump/Presets/` on Linux, and show up in
  every session and every copy of the plugin.

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
