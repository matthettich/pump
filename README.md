# …pump

Mastering plugins and creative effects by **wake**, in the look of Thunder Plus's *Simple* skins: flat and square, ink outlines with an
offset block, Share Tech Mono, one light-neon colour per plugin, light or dark. VST3 and CLAP on Windows and macOS
(plus Audio Units on macOS), built for Ableton Live and any other host.

| Plugin (name in your host) | Colour | What it does |
|---|---|---|
| **pump EQ** Equalizer | aqua | 24 bands; analog-matched curves with no cramping; cuts to 96 dB/oct plus brickwall; dynamic bands; stereo/L/R/mid/side per band; zero latency or linear phase |
| **pump COMP** Compressor | pink | Clean, Glue, Opto, Punch, Vari-Mu and Pump styles; lookahead; external sidechain with sidechain EQ; stereo link and mid/side |
| **pump LIMIT** Limiter | lime | true-peak limiter with seven styles, up to 8x oversampling, dither, loudness metering and targets |
| **pump CALM** Resonance Suppressor | violet | dynamic resonance suppressor with a sensitivity curve, mid/side and delta listening |
| **pump TONE** Tonal Balance | mint | automatic balance: Recover, Tame, Bias, Brighten, Body |
| **pump BAND** Multiband Dynamics | orange | multiband compression, expansion and upward compression; analog or linear-phase crossovers |
| **pump WIDE** Stereo Imager | blue | multiband width and stereoize, mono bass, vectorscope and correlation |
| **pump HEAT** Saturator | coral | multiband warm/tape/tube/triode/retro/clip saturation, oversampled |
| **pump METER** Loudness Meter | yellow | LUFS (momentary, short-term, integrated), LRA, true peak, PLR/PSR, history |

### Creative effects

| Plugin (name in your host) | Colour | What it does |
|---|---|---|
| **pump TUBE** Tube Distortion | cherry | six circuits (glow, triode, pentode, push-pull, starve, fuzz) with dynamic grid bias, supply sag, voltage and up to three stages; focus, tone, oversampling, auto gain; live curve, scope and harmonics |
| **pump SNAP** Transient Shaper | grass | level-independent attack and sustain shaping; detector focus, band split, stereo link, lookahead, soft clip to a ceiling, delta listening |
| **pump GATE** Sequenced Gate | sky | tempo-locked step gate, up to 32 steps with levels; rate, swing, length, attack/release, smooth, depth; volume or resonant filter; stereo offset; pattern tools |
| **pump ECHO** Delay | fuchsia | digital, tape, bucket-brigade, pitched (once or climbing per repeat) and reverse modes; tempo sync, ping-pong and wide; age, drive, diffuse, filters in the loop, ducking, freeze, self-oscillation |
| **pump SPRING** Spring Reverb | gold | dispersive two-, three-, short and long spring tanks with dwell, drip, tension and wobble, plus cheap and lo-fi early-digital springs with grit; a KICK key crashes the tank |
| **pump VERB** Reverb | periwinkle | twelve voices: studio hall/room/chamber/plate, late-70s and 80s digital halls and plates, a 90s rack, gated, reverse and shimmer; RT60-accurate decay with bass multiplier and damping, early reflections, age colour, duck, freeze |
| **pump GRAIN** Granular | seafoam | real-time granular: position, size, density, spray, pitch with scale quantize, reverse, five envelopes, tempo-synced grains, feedback, freeze, filter and space; morph pad and motion |
| **pump VOX** Vocoder | lilac | four modes: classic band vocoder (8-40 bands), spectral (FFT envelope), talkbox (an LPC model of the mouth, with driver and tube) and convolve; formant shift, carrier flatten, emphasis, sibilance, unvoiced noise, gate, freeze, ensemble; a built-in carrier synth that plays MIDI or follows your voice in key with chords; sidechain, voice-on-sidechain or split L/R routing |
| **pump TUNE** Pitch Correction | peach | live correction to a key and scale or to MIDI notes (retune speed, humanize, flex, amount, added vibrato); a graph that captures the voice from the song as notes you drag, snap, straighten and re-vibrato; formant shift that keeps or follows the pitch; up to four harmonies by scale step, semitones or MIDI, with their own formant and humanize |
| **pump CRUSH** Lo-Fi Degrader | olive | seven sampler voicings (clean, gritty and hi-fi 12-bit, a dark resonant 12-bit, companded 8-bit, toy, phone) with drive, bits, rate and clock jitter; worn media before or after the sampler: wow, flutter, age, hiss, crackle and dropouts; converter pad, spectrum with the Nyquist marked, close-up wave |
| **pump SWIRL** Modulation | rose | six types with four variants each: stereo ensemble, bucket-brigade chorus (slow, fast, shimmer, vibrato), 2-6 voice rack chorus, tape flanger (classic, through-zero, negative, jet), 4-12 stage phaser and a lamp phaser; rate or host sync, depth, manual, feedback, spread, tone, warmth; live motion, response and stereo screens |
| **pump STUTTER** Beat Repeat | ice | eight moves (repeat, roll, reverse, tape stop, half speed, shuffle, gate, glitch) from a dice roll on the song grid, from pads (or keys 1-8) or from MIDI notes; grid, gate, pitch, decay and filter per repeat, variation; timeline, pads and a bar clock |
| **pump AMP** Amp Modeler | copper | loads neural amp models (.nam) and cabinet impulse responses (.wav), with previous / next file in the folder; input, gate, normalize to the model's loudness, mono or stereo, bass / mid / treble / presence, cab low and high cut; models run at their own rate (resampled when the host differs) |

### Instruments

| Plugin (name in your host) | Colour | What it does |
|---|---|---|
| **pump TABLE** Wavetable Synth | spring green | two wavetable oscillators (ten built-in tables plus your own .wav wavetables) with up to 7-voice unison, detune and stereo spread, sub and noise; ladder LP 24 / 12 and state-variable BP / HP filters with drive, envelope and key tracking; amp and mod envelopes, a per-voice and a shared (syncable) LFO, a 4-slot mod matrix; poly (16), mono or legato with glide; a keyboard to play |
| **pump MONO** Analog Mono Synth | blood orange | three oscillators (triangle, saw, square, pulse; 32' to 2') with hard sync and FM, noise, overdrive; four filter models: transistor ladder 24, state-variable 12 (low-pass to notch to high-pass), diode 18 and Sallen-Key 12, all self-oscillating but the SVF; filter and amp envelopes, LFO with wheel vibrato, glide, last / low / high note priority, legato, drift; runs 2x oversampled |

## Download

The newest builds are always on the [latest release](https://github.com/matthettich/pump/releases/tag/latest):
`pump-macos.pkg` (installer, Apple Silicon + Intel), `pump-macos.zip`, `pump-windows.zip`. Each zip has an
`INSTALL.txt`.

## System EQ (Mac)

**System EQ** puts pump EQ on everything your Mac plays (macOS 14.2 or later). It's a menu-bar app with the full EQ
window; `pump-macos.pkg` installs it in Applications, and `system-eq-macos.zip` has the app on its own.

- It follows the Mac's own output and sample rate: pick speakers or headphones as usual and it moves with you.
- **Separate EQ for each output** (menu, on by default): every output keeps its own settings; a new one starts from
  the latest settings. Turn it off to use one EQ everywhere.
- Only audio going to the current output is processed: an app playing to another device (a DAW on an audio
  interface) is left alone.
- During silence it skips the EQ, and with nothing playing it stops the audio device; it starts again the moment an
  app plays.
- **EQ On** in the menu turns it off and gives the Mac its untouched output back. *Open at Login* starts it with the Mac.
- The first start asks for permission to record system audio (that's how it hears other apps). If you hear nothing,
  *Audio Recording Permission…* in the menu opens the right page in System Settings. Unsigned builds may ask again
  after an update.
- Linear phase adds latency to everything, video included: zero latency suits everyday listening.

## Using them

Every plugin shares the same mouse and key conventions. Press **?** in a plugin for its full list.

- drag a control (Shift = fine), mouse wheel steps it, Ctrl-click (Cmd-click on a Mac) resets it; on a Mac,
  Ctrl-click is a right-click, as everywhere else in macOS
- double-click or right-click to type a value: `1k`, `A4`, `C#3+13`, `2x` (= +6 dB), `50%` (halfway); Tab = next field
- Ctrl/Cmd+Z undo, Shift+Ctrl/Cmd+Z or Ctrl+Y redo; A/B compare with COPY; factory presets with ◀ ▶
- hold **Alt** on a band to solo it while held (EQ nodes, multiband regions); drag while soloed to move it (up = narrower,
  down = wider). On empty spectrum space, Alt-hold listens to the range under the mouse (drag to sweep; up/down or wheel = width)
- DARK switches the look and is remembered for new instances
- EQ dynamic bands: select a band, turn on DYNAMIC, set RANGE and THRESH; Alt+double-click on the
  curve makes a dynamic band directly
- COMP has a stereo sidechain input: set SC to External and route a track to the plugin's sidechain in your host
- VOX goes on your vocal: its built-in synth sings along (FOLLOW) or plays the MIDI you send it (in Live: a MIDI track's
  *MIDI To* set to the vocal track, VOX). For your own synth as the carrier, set ROUTE to *Sidechain carrier* and send
  the synth to VOX's sidechain; *Split L/R* takes one stereo track with the carrier left and the voice right
- STUTTER: hold a pad (or keys 1-8) to play a move; MIDI notes C, C#, D, D#, E, F, F#, G of any octave do the same (in
  Live: a MIDI track's *MIDI To* set to the track with STUTTER). The small AUTO box on a pad lets the dice pick that move
- AMP: LOAD a `.nam` model (the free captures people share) and a cabinet `.wav`; `<` `>` step through the other files in
  the same folder. The paths are saved with the session
- The instruments take MIDI from the host (in Live, put them on a MIDI track); the keyboard at the bottom of their window
  plays too. TABLE: drag a table screen up / down to move its position, the wheel changes the table; LOAD reads a .wav
  wavetable (frames of 2048 samples) into USER
- TUNE goes on a solo voice. Live: set KEY and SCALE (or send it MIDI notes) and RETUNE. Graph: turn CAPTURE on and play
  the song once; the voice appears as notes on the host's timeline: drag them (Alt = free), double-click to snap,
  select several to STRAIGHTEN, change VIBRATO, FORMANT or GAIN. The edits are saved with the session

## Presets

Every plugin's preset menu has three groups of factory presets, plus your own:

- **Factory**: general starting points.
- **Sources**: for single tracks and buses (Vocals, Drums, Kick, Snare, Bass, Guitars, Keys, Synths, Pads, Bus). They
  are set for sources peaking around -6 dBFS; move THRESH or GAIN by the same amount for quieter or hotter tracks.
- **Chains** (mastering plugins): seven matching mastering chains (Pop, EDM, Hip-hop, Rock, Acoustic & jazz, Lo-fi, Streaming -14). Load
  the same name in each plugin, in this order: EQ, CALM, COMP, BAND, HEAT, TONE, WIDE, LIMIT, METER. On dense mixes,
  lower LIMIT's GAIN by 1-3 dB to land on the target loudness.
- **Your presets**: *Save preset…* at the end of the menu. They're stored as small text files (one folder per plugin)
  in `Documents/wake/pump/Presets/` on macOS and Windows, `~/.local/share/wake/pump/Presets/` on Linux, and show up in
  every session and every copy of the plugin.

## Building

```
git clone --recursive https://github.com/matthettich/pump   # (AMP needs the NAM core's Eigen submodule too)
make            # all plugins into bin/  (Windows: cross-compile with MinGW, see .github/workflows/build.yml)
make tests      # offline DSP tests
```

See [docs/DEVELOPING.md](docs/DEVELOPING.md) for the framework, the shared DSP library and how to add a plugin. The DSP
in `common/dsp` and each plugin's `*Engine.hpp` has no framework code, so it also compiles to WebAssembly: `make wasm`
builds `pump.wasm` for Thunder Plus, where the nine run as bus and master effects (`wasm/`).

## Licences

© wake. Third-party components (DPF, pffft, NanoVG, pugl, fonts) are permissively licensed; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
