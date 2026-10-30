# pump: handover

The pick-up doc: where things stand, what is verified and what is not, and what is open. Conventions and the layout
are in [DEVELOPING.md](DEVELOPING.md) and the plan is in [ROADMAP.md](ROADMAP.md); this file does not repeat them.
Last updated 2026-10-30 (pump ROOM PHONE MIC; before it: ACID, X0X, GLOSS; pump ROOM, GUI pass, COIL modulation, LIFE sound, AU crash fix).

## State

- 33 plugins (effects, mastering, instruments, and ROOM room correction), VST3 + CLAP everywhere, AU on macOS. All build on macOS with
  `make -C plugins/<name> MACOS=true` (serial per plugin, see "Build gotchas").
- CI (`.github/workflows/build.yml`) builds Windows x64 (MinGW cross-compile), macOS universal, Linux, runs the DSP
  tests and the licence-boundary check, and refreshes the `latest` pre-release on every push to `main`.
- Windows and macOS builds for testing come from that `latest` release, not from this machine.

## Licences (do not blur these)

- pump's own code is (c) wake, all rights reserved ([LICENSE](../LICENSE)). `PumpPlugin::getLicense()` returns
  "All rights reserved (c) wake".
- **MACHINE is GPL-3.0-only**, together with the pump code compiled into it, because it is built on GPL drum machine
  firmware ([plugins/machine/NOTICE.md](../plugins/machine/NOTICE.md)). It overrides `getLicense()`. The grant covers
  MACHINE only.
- `tools/check-licence-boundary.sh` (CI step and `make licence-check`, also run by `make tests`) fails if anything in
  `common/` or another plugin includes, links or builds MACHINE code, or if another plugin overrides `getLicense()`.
  It ignores comments that merely mention MACHINE. Negative-tested against six injected violations.
- DPF prints `Unknown license string 'All rights reserved (c) wake'` while generating the AU plist. Harmless: DPF only
  knows SPDX names, "Proprietary" hit the same warning, and the string is not written into the plist.

## pump ROOM PHONE MIC (2026-10-30)

A phone (Android Chrome, USB measurement mic) as the mic while the computer plays the sweeps: [ROOM.md](ROOM.md)
stage 5 has how it works. Built as a separate mode on the owner's request: `room/remote.js` + `remote.css`, two script
slots in `app.html` and the USE A PHONE AS THE MIC button; app.js is untouched, and the runs are ordinary runs.

| What | Where | Verified by |
|---|---|---|
| Pairing relay (rooms per code, ICE servers from `ICE_SERVERS`) | `relay/core.mjs`, `worker.mjs` + `wrangler.toml` (hosted), `local.mjs` (tests) | the phone test pairs through `local.mjs` |
| The link (WebRTC data channel, blobs with backpressure), the computer's dialog and playback, the phone page and its recorder worklet, `analysePhoneRun` (two pings, resampling, cuts), test mode | `room/remote.js` | `tools/room_phone_e2e.mjs`: clocks to the ppm at +150 / -400, L/R timing to 0.001 ms, one speaker, calibration from the phone, MEASURE unchanged, correction alignment, reconnect |
| QR codes | `room/third_party/qrcode.js` (MIT, unmodified; notice in THIRD_PARTY_NOTICES.md) | the test checks the drawing |

**To make it live:** deploy `relay/` (the owner's account: log in with the host's command-line tool, deploy from that
folder) and put its `wss://` address in `RELAY_URL` (remote.js). Until then the dialog says the relay isn't set up.
`tools/room_e2e.mjs` was stale (the export items moved into the EXPORT menu; the Pi export has 14 files): fixed, and
it passes on this build. Neither page test runs in CI.

## NEXT: pump ROOM, the owner's first real measurement (2026-10-29 or later)

Room correction ([ROOM.md](ROOM.md) has the plan, how every part works and the file formats). Everything below was
built on 2026-10-28 and has **only ever run against simulated rooms**: the owner measures real speakers for the first
time and sends the results. The next session's job is to look at that data, fix what the real world breaks, and tune
the correction rules on it. Later the same day the owner asked for the **handheld mode** and **phase correction**
early: both are built too (rows below), equally untried on real hardware.

### What exists

| Part | Where | Ships as (CI, `latest` release) | Verified |
|---|---|---|---|
| Measurement DSP: log sweeps, deconvolution, arrival between samples, windowed spectra, smoothing, mic calibration files | `common/dsp/Room.hpp` | inside everything below | `tests/test_room.cpp` (known rooms within 0.1 dB, arrivals to 0.01 sample, 14 cal-file formats; ASan) |
| Correction design: power average per speaker, target (tilt, bass, treble), cuts on detail / boosts only on the 1/3-octave deficit, no boost below the roll-off, limits, band, amount, time alignment, preamp; min/linear-phase FIR design; `.pumproom` profiles | `common/dsp/RoomCorrect.hpp` | inside everything below | `tests/test_room.cpp` (rules against known rooms, FIRs within 0.07 dB of the design, profile round trip) |
| The app: measure (devices, level check, cal file, L/R sweeps), RESPONSE / IMPULSE / CORRECTION views, session save/open, SAVE PROFILE, EXPORT FOR WINDOWS, EXPORT FOR PI | `room/` (`app.html`, `app.css`, `app.js`, `worklet.js`), `wasm/room_wasm.cpp`, `room/build.sh` (`make room-page`) | `pump-room.html` (Chrome / Edge) | `tools/room_e2e.mjs` (headless Chromium on the page's `#sim` room: timing, cal, correction, every export, save/reopen) |
| Desktop app (Electron around the page) | `room/electron/` | `pump-room-macos.zip`, `pump-room-windows-setup.exe`, `pump-room-windows.zip` | CI packages both and runs each once with `PUMP_ROOM_SMOKE=1` (measures the sim room, checks the timing) |
| pump ROOM plugin: loads a profile (kept in the session), amount, minimum / linear phase, time align, output | `plugins/room` (engine: `RoomEngine.hpp`) | in `pump-macos.pkg` / `pump-macos.zip` / `pump-windows.zip` / linux | engine in `tests/test_room.cpp` (impulse = design, alignment, amount, linear latency; TSan); builds on all three in CI. **The window has never been opened** (no GL in the session that made it: syntax-checked against stub GL headers only) |
| Room Correction in System EQ (Mac): ROOM's AU after the EQ, menu toggle, its own window, state per output | `system/main.mm`, `system/build.sh` | `pump-macos.pkg`, `system-eq-macos.zip` | compiles in CI. **Never run** |
| Windows export: stereo min-phase filters at 44.1-192 kHz + a config (`If: sampleRate == ...` / `Convolution:`) for the free system-wide equalizer the owner uses | `exportWindows()` in `room/app.js` | (made by the app) | zip contents checked; the config syntax is from that equalizer's documentation, **never tried in it** |
| Phase correction: each speaker's group delay (frequency-dependent window, octave smoothing), lined up 60 Hz..3 kHz with linear-phase filters; profile 5th column; ARRIVAL BY FREQUENCY graph | `groupDelayMs`, `designPhaseCorrection`, `designFir(..., groupDelayMs)` in `RoomCorrect.hpp`; ALIGN PHASE / NO LATENCY-LINEAR PHASE in the app; the plugin in linear phase | everything above | `tests/test_room.cpp` (a 50 Hz 4th-order roll-off + crossover allpass: 2.5 ms spread -> 0.4 ms; with reflections 4.4 dB down within 0.35 ms 300 Hz..3 kHz), e2e |
| Handheld measurement: FIND SPEAKERS (latency + spacing with the mic at the left speaker), SET CENTER, tracked by pings, auto-capture on each dot, map + close-up with direction | HANDHELD view, `H` / `HAND` in `room/app.js`; `simMic` in `worklet.js` | the app | e2e moves the simulated mic through 5 points (positions within 6 cm, guidance shown). **Never held by a person**: body reflections, handling noise and the tolerances (`HAND`) are the open questions |
| Pi export: mono filters per speaker and rate, a CamillaDSP v4 config, `install.sh` (filters to /etc/pump-room, CamillaDSP from GitHub if missing, a boot service) | `exportPi()` in `room/app.js` | (made by the app) | YAML parses, script parses, its device-keeping re-install merge works; **never run on a Pi or in CamillaDSP** |

### The owner's setup and what they'll send

- Mic: a MEMS measurement mic from a commercial room-correction kit; they hoped to get its calibration file, else
  they'll buy a USB measurement mic. Main computer Windows (Renoise, VST3, plug-in sandboxing on, 150 %+ display
  scaling); also Macs (Ableton Live; System EQ). They want this for their studio and for several rooms in the house
  (turntable, streamer, phone; one cheap Pi box per room: Pi Zero 2 W + a 16-bit USB line in/out interface named
  "CODEC" under ALSA, which is what the Pi export's defaults assume).
- Expect: one or more **session files** (`pump-room-*.json`: every speaker's impulse response from 20 ms before the
  direct sound to 1 s after, arrivals, levels, the calibration file's text, the correction settings), maybe a
  `.pumproom`, screenshots, and notes on what they heard. Ask which app (page / desktop), browser, OS and audio
  interface, the speakers, and roughly where they sat if it isn't in the notes.

### How to look at a session

```
make room-page                     # needs WASI_SDK (wasi-sdk 25; download from its GitHub releases)
node tools/room_session.mjs path/to/session.json build/room-session    # Playwright
```

`tools/room_session.mjs` opens the session in the built page with the app's own code and writes `summary.txt` (per
run and speaker: arrival, SNR, sweep level at the mic, peak, polarity; L/R timing and level; the correction with the
session's own settings: preamp, delays, low-end limit, largest cut and boost), `responses.csv` (every speaker,
1/12 octave), `correction.csv` (averaged measurement, target, correction, predicted result per speaker) and screenshots
of the three views. Plot the CSVs (matplotlib) and send the owner images when discussing curves.

Where Playwright lives: in the session that built this it was `/opt/npm-tools/node_modules` (link it as
`node_modules` in the repo root for the run, then remove the link). npm's registry was blocked there.

### What to check first, and the likely failures

1. **Levels.** SNR under ~30 dB or peaks over -1 dBFS make everything else noise: tell the owner to change the volume /
   sweep level, not the code. Sweep at the mic around -30..-12 dBFS is the aim.
2. **Was the direct sound found?** Arrival = system latency + distance (a few ms to a few tens of ms). The search
   window is the first 1 s after the sweep starts (`SEARCH_S` in `app.js`): Bluetooth or a big buffer could exceed it.
   L/R difference should match the geometry (343 m/s: 1 ms = 34.3 cm). Polarity should agree between speakers.
3. **Calibration sign.** Files list the mic's own deviation; the code subtracts it. If corrected responses get worse
   (e.g. a rising top end gets steeper), the file may be the other convention. Check a known-flat region. MEMS mic
   files often come as 0° and 90° versions: ask which way the mic pointed.
4. **Browser audio.** Echo cancellation / noise suppression / AGC are requested off; if the response has odd
   pumping or a missing low end, suspect the browser or OS still processing the mic (Windows "enhancements", a
   communications device). Different input and output devices can drift (sweep lengths are short enough that this
   should only smear the top octave).
5. **Handheld on a real mic** (if they tried it): did FIND SPEAKERS get a plausible spacing (tape-measure it)? Does
   the tracked position jitter more than 4 cm when still (then nothing is ever captured: raise `HAND.still`)? Are
   points rejected as "moved" (the measurement's own arrivals disagree with the pings)? Compare the handheld runs'
   responses with a tripod measurement at the center.
6. **Phase correction on real speakers**: the measured group delay (CORRECTION view, ARRIVAL BY FREQUENCY) should
   rise smoothly toward the bass and step at the crossover; ragged curves mean the room dominates (fewer cycles,
   more smoothing, a narrower band). It's only applied with linear-phase filters.
7. **The correction rules on a real room** (`designCorrection` in `RoomCorrect.hpp`): the low-end guard (first point
   under 150 Hz that stays 6 dB under the target) can be fooled by a room null; the null rule (boost only the 1/3-oct
   deficit) and the 1/12..1/3-oct smoothing were chosen on simulations. Tune against the owner's ears, keep the tests'
   intent, add a test from the real data (anonymised responses are fine to commit as test data).
8. **Plugin window and System EQ first run.** The owner will be the first to open both. Renoise's crash log is
   `~/Library/Logs/Renoise.log` on Mac; look for the AU idle-timer patch class of problem
   (`patches/dpf/0009-...`). The profile is a ~28 KB state string with newlines: check hosts keep it (save, reopen).
   In System EQ: the menu items appear only when `pump-room.component` is inside the app.

### Owner's preferences that apply here

- No third-party product or company names anywhere in the repo, git history included (call the Windows equalizer
  "the free system-wide equalizer", the room-correction kit "a commercial room-correction kit"). Chat is fine.
- Commit and push after each finished piece so they can test from the `latest` release; say what to test.
- Wants it simple like the commercial tools (hold the mic, get a correction); **handheld measurement is the
  long-term goal** (ROOM.md stage 4) and comes after the real data has shaped the rules.
- Lowest-cost hardware for the per-room Pi boxes; existing open-source DSP on the Pi is fine (no need to write our own).

### Session gotchas (from the 2026-10-28 session)

- CI logs: the log download host may be blocked; the failing steps report errors as annotations instead
  (`gh api repos/matthettich/pump/check-runs/<job id>/annotations`). `gh release view` (GraphQL) is blocked; use
  `gh api repos/matthettich/pump/releases/tags/latest`.
- A push cancels the running CI (the `concurrency` group): don't push while waiting on a result you need.
- `make room` builds the plugin; the page is `make room-page`.

## What changed in the 2026-10-29 session: ACID, X0X, GLOSS

The owner asked for an acid bass synth with a pattern sequencer and a random pattern generator (gray, colourful
display and sequencer), an x0x-style drum machine with kits of the classic acid drum machines, and a hyperpop synth
built on the voices of a classic machine-based mono synth (pink). The voicings are named generically in the repo
(the owner's rule); in chat: ACID's models recall the bass line with the diode ladder, a saw/pulse/sub mono, a pocket
three-oscillator bass, a two-oscillator bass station and a screaming two-pole; X0X's EIGHT / NINE / SIX / TEN / SEVEN are
the classic analog boxes and the first eight-bit sample box; GLOSS's machines recall the machine-based synth's FM,
chip, vocal, ensemble saw, wave and physical voices.

| Plugin | Where | Verified by |
|---|---|---|
| **ACID**: five models (Acid: band-limited saw / transistor square into a coupled four-stage diode ladder whose stage coefficient comes from a table built from the ladder's own response, so the resonance sits at the cutoff; decaying filter envelope, slow amp decay, an accent capacitor that charges over consecutive accents; Solo, Pocket, Station, Scream), 2x oversampled; a 16-step sequencer (note -12..+24, gate note / tie / rest, accent, slide; length, rate 1/32..1/8 with triplets, swing as GATE's, gate length, transpose; OFF / SONG / KEYS / FREE, and PLAY from the window); MIDI legato slides, velocity >= ACC VEL accents; RANDOM / MUTATE over chosen lanes with acid-line rules (`randomizeSteps` in AcidParams.hpp) | `plugins/acid` | `tests/test_acid.cpp` (pitch per model, slides, the diode table's phase at the cutoff, resonance near the cutoff, the grid timing, swing, tie, slide without a new attack, KEYS transposition, SONG stopping with the host, loudness of every model's line, 3600 random patterns against the rules, MUTATE, random settings); screenshots light and dark |
| **X0X**: eleven drums (BD SD CP CH OH LT MT HT RS CB CY on C-4 up the white keys, GM below), each on any of five machine circuits (tables at the top of X0xEngine.hpp), tune / decay / tone / a fourth control / level / pan; accent row; step masks as one integer parameter per row (`bd_steps` .. `ac_steps`, bit k = step k + 1, not automatable); OFF / SONG / FREE, PLAY; a PATTERNS menu in the window (`patternLibrary()`) that writes the steps only | `plugins/x0x` | `tests/test_x0x.cpp` (55 instrument x machine hits sound, end and stay finite; notes and GM map; bass drum tuning; grid timing, swing, accent, choke; SONG / FREE; loudness; random kits); screenshots |
| **GLOSS**: seven machines with four controls each (`kMachineControls`), pitch envelope, glide, poly 8 / mono / legato, SVF with its own envelope, ADSR, drive soft / hard / fold at 2x, crush, width, LFO; a blob display drawn from the output | `plugins/gloss` | `tests/test_gloss.cpp` (every machine in tune at A2 and C4, the string to the cent; loudness; pitch envelope; poly / mono / legato; release; chip sync; bubbles chirp; random settings; CPU per machine); screenshots |

| **BREAK** (asked for later the same day): a break chopper. Built-in breaks are scores in `BreakParams.hpp` (`kBreaks`: sixteenths for kick, snare with ghosts, hats, ride, toms; swing, room, tape, brightness, tightness) rendered by `BreakSounds.hpp` (an acoustic-style kit, a small Schroeder room, tape; three bars rendered with the same feel, the second kept and its first 3 ms crossfaded from the third's start, so it loops seamlessly; about 1.5 s for all fourteen, on a worker thread like PRISM's built-ins, retained / released by the plugins). Files: state "file" (stereo, 30 s, FILE LENGTH bars sets their tempo). 16 slices; per step slice / pitch / bend / level / decay / reverse; eight momentary effects (`fxN_hold` parameters are momentary, kPNoUndo; the window's pads and keys 1-8 go through an atomic; MIDI white keys from C-2); slices on MIDI white keys from C-4 | `plugins/break` | `tests/test_break.cpp` (lengths, seamless loops, levels; the straight chop reproduces the break; pitch, bend, decay, reverse, FIT; every effect; MIDI; SONG stop; random settings); screenshots |

All four are at about -20 dBFS RMS at VOLUME 0 dB (X0X's default kit and pattern about -18), have parameter ids
recorded in tests/data/param_ids.txt, run in `make tests-asan` (added to SAN_TESTS), and build as VST3 / CLAP here.
Not tried in a DAW yet: the sequencers' song lock in Live and Renoise (the same `transport()` as GATE and LIFE), and the
CLAP / VST3 / AU builds on Mac and Windows (CI builds them).

**pump ROOM, phones and Bluetooth (same session):** LINK WIRED / BLUETOOTH in the setup panel (`S.bt`, stored as
`link`; each run keeps `bt`, saved in sessions). Bluetooth widens the capture (lead-in 1.5 s, search 2.4 s, tail 2.6 s,
a -80 dBFS keep-alive noise in the silence) and any used Bluetooth run makes the correction EQ only (no delay, no phase,
UP TO capped at 16 kHz; `btRuns()` / `showBtLocks()` in app.js); the verdict says timing isn't measured and HANDHELD is
off. Phones: a screen wake lock while audio runs (`keepAwake`), a note when the output can't be picked. tools/room_e2e.mjs
measures once over the Bluetooth link too. Not tried on a real phone or Bluetooth receiver yet: on Android Chrome the
mic must be a USB one (the phone's own mic is tuned for voice and isn't flat).

**GL headers for screenshots in a session without them:** the package registries were blocked, so the GL headers were
generated from the Khronos XML registry (git clone of KhronosGroup/OpenGL-Registry and EGL-Registry works): a small
script wrote `GL/gl.h` (1.0-1.3 prototypes and enums) and `GL/glx.h` (GLX 1.0-1.4) from gl.xml / glx.xml, next to the
registry's glext.h, glxext.h and KHR/khrplatform.h, in /home/claude/sysroot/include, with gl.pc / x11.pc in
/home/claude/sysroot/pkgconfig pointing at the system's libGL.so.1 (a symlink to libGL.so). The DPF submodule's own pugl
submodule must be initialised (`git -C dpf submodule update --init`). Then `make -C plugins/<x> jack` and Xvfb render
the real windows.

## Also open: LIFE's sound against its reference (in progress, 2026-10-27)

LIFE is modelled on a groovebox the owner knows well (the reference; it is named in the owner's own notes, not here:
this repo names no third-party products). The goal is to sound closer to it. The owner will send a **recording of the
reference** (ideally: its default set for about 10 s, the same with its FX mix at zero, and a few voices alone).

Done so far:
- **The noise** (the clear difference): every voice was FM'd by its neighbour's *already modulated* sound and the
  eight formed a ring, i.e. feedback FM, chaotic at the kit's FM amounts. Measured on the default set by spectral
  flatness: 0.133, and 0 with FM off (ring, drive, FX and the kick sweep add none). Now each voice is modulated (and
  ring-modulated) by its neighbour's own clean oscillator: 0.0012, loudness unchanged. The old sound is the
  **FM LOOP** switch (appended parameter `fm_loop`, off). Commit "LIFE: clean FM by default".
- **What the reference has that LIFE lacked** (from a published description of it): a send into the FX per voice
  (now the FX bar row, `t1_fx`..`t8_fx`, 100 % by default), and feedback that builds into a saturated wash at the top
  (now: FEEDBACK as before up to 80 %, loop gain rising past unity to 1.3 at 100 %, held by the soft clip). The same
  description: parabolic-wave tones with pitch, kick (a short pitch envelope), FM, ring, decay and amplitude; global
  FM, drive and decay; an LFO with depth, rate and phase; FX = pitch shifter, grain and delay as one unit, a bipolar
  filter, feedback, and an amplitude/decay envelope into the FX. LIFE has all of these except the LFO phase control
  and an FX-envelope amplitude (the per-voice sends stand in for it).

When the recording arrives, compare against LIFE's default set rendered offline and match, in this order:
1. each voice's **pitch envelope** (KICK: depth, and its 12 ms time constant in `LifeEngine::control`, `kickK`),
2. the **decay curve** (LIFE: exponential to -60 dB over DECAY) and the attack (1 ms linear),
3. **brightness**: the parabolic shape, FM index scaling (`fmAmt = fm * fmAll * 1.5`), DRIVE (`tanh`, gain 1 + 7 x),
4. the **FX**: delay -> two-grain pitch shifter (sin^2 windows, linear interpolation) -> filter -> soft clip, fed back,
5. levels and panning (`width` spreads the voices alternately left and right).

Analysis harness: render the engine directly (see `tests/test_life.cpp`'s setup, or build Settings from the
parameter defaults as `PluginLife::sync` does) and measure spectral flatness, energy above 6 kHz, envelopes per voice.
Send the owner before/after WAVs (old for a few seconds, a gap, then new) to judge by ear.

## What changed in the 2026-10-27 session

| Change | Where | Verified by |
|---|---|---|
| **AU crash on removing/swapping a plugin** (Renoise: SIGSEGV in a run-loop timer after "Uninitialize"; seen with FOLD, SING, WOB, ORBIT): DPF's AU view kept its idle timer until deallocation | `patches/dpf/0009-au-stop-idle-timer-when-view-leaves-window.patch` | the owner's Renoise log (`~/Library/Logs/Renoise.log`, "CrashLog" lines); compiles on macOS in CI; **not yet confirmed in Renoise by the owner** |
| Windows remember their size (corner grip) per plugin, and open about 600 points tall until a size is chosen | `common/ui/PumpUI.hpp` (`readZoom`, `rememberZoom`, `defaultZoom`); the settings file keeps the theme on line 1 so older builds still read it | headless: saved size reopens; bad values and the old one-line file fall back |
| GUI tightening: 15 plugins shorter (COIL, TWANG, BOING, STRIKE, MACHINE, SING, PHASE, ORBIT, TRACT, FREEZE, PRISM, CHOP, KIT, STUTTER, AMP); STUTTER and VECTOR got pages; TUNE tighter | each `UI<Name>.cpp`, `DistrhoPluginInfo.h` | screenshots light and dark (`tools/shot.sh`) |
| COIL / TWANG / BOING springs drawn as SPRING's tank: one spring per sounding voice, a colour each, turns and look per tank model (digital: chips, stepped coils) | `plugins/coil/CoilView.hpp` | screenshots per model |
| COIL VOICES (1-16, default 8) and **modulation**: 3 LFOs (the third added later: its source number and settings are appended), velocity, note, wheel, aftertouch, random per note; 32 connection slots (appended params); drag a source's grip onto a dial; MOD page lists connections | `common/dsp/Mod.hpp`, `plugins/coil/CoilDsp.hpp`, `UICoil.cpp` | `tests/test_coil_mod.cpp` (no connection = identical output, removal leaves no trace, sync, CPU), `parity_coil` (plugin = web build), UI driven with `tools/xdo.cpp` |
| COIL **drawn LFO shapes** (2026-10-28): any LFO's DRAW plays a drawn curve (up to 32 points, each line bent, a loop between two points, LOOP off = once through). Saved as the `shapes` state (text, ':' between numbers since the parser reads ',' as a decimal point), handed to the audio thread by a sequence-locked copy; with RETRIG each voice runs its own copy | `common/dsp/ModShape.hpp`, `CoilDsp.hpp`, `UICoil.cpp` (`drawShapeEditor`, `shapeMouse*`) | `test_coil_mod` "drawn shapes"; UI driven with `tools/xdo.cpp` |
| COIL **BOWED** (2026-10-28): instead of the kick, a bow (the bowed-string friction junction: stick and slip) on each spring's loop, every sample. The bow feels the loop's wave low-passed at 3 x the note with its drift taken out (otherwise the dispersive springs' corners pull the stick-slip off the note, or it just pushes a DC offset round), and its push has its drift taken out too; a soft kick (0.35) starts each note so low notes speak at once; PRESSURE = friction slope 5.5 .. 2.5, SPEED = bow velocity, velocity scales it; output x 0.5. Pitch: tanks within about 25 cents to C5 (small per-range correction in `design`, `bowTune`); above C5 and on the lo-fi model's low notes it can jump to a neighbouring mode at high pressure (heard as a crack into another pitch, as real bows do) | `CoilEngine.hpp` (`bowAt`, `bowForce`), `UICoil.cpp` (VOICE panel: STRUCK/BOWED, PRESSURE, SPEED; the springs view is 40 px narrower) | `test_coil` "== bowed" (in tune within 40 cents C2..C5 on four models, sustains while held, silent after, loudness near struck, extremes finite); listen: the analysis harness renders melodies per model |
| Drum synths' pads on the white keys up from C4 (kick C4, snare D4, clap E4, hat F4...), note shown on each pad; old GM notes still play (STRIKE's old 61 and 63 too) | BOING, KIT, STRIKE, MACHINE | `== notes` sections in their tests |
| Pads moved down an octave (2026-10-28) to Renoise's C-4 = MIDI 48 (Renoise numbers octaves from 0 at MIDI 0; the owner plays them from Renoise's keyboard, where C-4 sent 48 and hit the old GM tom). `kPadBaseNote`, `whiteKeyNote`, `padNoteName` (labels "C-4" + the number). GM notes only below 48 now; black keys between the pads silent; STRIKE's legacy 61/63 dropped. LIFE's voices and CHOP's default pad notes on the same keys | `common/dsp/Util.hpp`, BOING, KIT, STRIKE, MACHINE, LIFE, CHOP | `== notes` in their tests; test_life "E-4 (52) plays tone 3" |
| SING text entry in Renoise: the Windows hook no longer lets keys through when `GetFocus()` says the editor has them (it answers for this thread's queue only, so after `takeFocus` it said yes while the keys went to Renoise); every key goes through the hook while the box is open. Fallbacks that need no keyboard: PASTE (clipboard) and OPEN TXT (a text file, state `lyrics_file`, re-read when it is saved again while the UI is open) | `common/ui/WinKeyGrab.cpp`, `plugins/sing/UISing.cpp`, `PluginSing.hpp` | **not yet confirmed in Renoise by the owner** (no Windows here); macOS path unchanged |
| SING / preset names on macOS: a real NSTextField in a non-activating NSPanel (a child window over the drawn box) takes the typing. Why: a sandboxed plugin's process can't activate itself on macOS 14+ (`[NSApp activate]` is cooperative, Renoise doesn't yield), so the old grab never got keys there. The panel class is made at run time per bundle (`PumpTextPanel_<addr>`); Enter = commit, Esc = cancel (local monitor), text mirrored into `fEdit`. Control value entry still uses the drawn box + grab | `common/ui/MacKeyGrab.mm` (`textFieldOpen/Close`), `PumpUI::takeKeyboardFocus`, `textFromField` | compiled in macOS CI only; **not yet tried in Renoise** |
| Waveform overlays (TRACT, PRISM, MODAL, FREEZE): one 1.25 px line in the plugin's complementary colour (`theme.second`), no glow | `PumpUI::drawWaveOverlay` | syntax-checked; no GL here for screenshots |
| STRIKE pads: each engine drawn like ORBIT's attractor, a 3-D path turning slowly with a comet trail and a head, faint path when quiet | `UIStrike.cpp` `drawMotion` | syntax-checked; no screenshot (no GL in that session) |
| CHOP: the bar coming in drawn live over the captured region (pink outline, green write head) | `ChopEngine.hpp` (`liveOverview`), `UIChop.cpp` | screenshots |
| CI: a newer push cancels a running build; tests get 60 min (seven concurrent builds had timed the tests out) | `.github/workflows/build.yml` | |

Ideas the owner liked but not built: the modulation system for TWANG and BOING (and later other plugins: `Mod.hpp` is
shared; COIL's `CoilDsp` + `UICoil` modulation code is the template). More source types are planned (drawn
multistage envelopes, macros with an XY pad, per-note envelopes).

## What changed in the 2026-10-25 session

| Change | Where | Verified by |
|---|---|---|
| Licence default, root LICENSE, README section, boundary guard | `common/PumpPlugin.hpp`, `LICENSE`, `tools/check-licence-boundary.sh` | guard run on the clean tree and 6 injected violations |
| WAV reader rejects more than 2 channels and rates outside 1 kHz to 768 kHz; accurate errors for directories and unreadable files | `common/dsp/Wav.hpp` | `tests/test_wav.cpp` (40+ cases incl. hostile headers) under ASan+UBSan; TABLE's user-wavetable test |
| Preset files: bounded read (256 KB, 512-char lines, 4096 values); saving writes a `.tmp` and renames over the old file | `common/ui/PumpUI.hpp` | compiles (all 28 plugins built); reader logic tested in a standalone harness under ASan+UBSan |
| Sanitizer targets | `Makefile`: `make tests-asan`, `make tests-tsan` | both clean on wav, biquad, dsp, presets, table, eq, band, wide, heat, gate |
| AMP model pre-check and crash marker | `plugins/amp/ModelGuard.hpp`, `AmpEngine.hpp`, `PluginAmp.hpp` | `tests/test_modelguard.cpp`; restore simulation (below) |
| NAM fuzz harness | `tests/fuzz_nam.cpp` | found the crash described below |

Not verified: the atomic preset save was not exercised end to end (it needs a running plugin UI); only `rename`'s
replace behaviour and the reader were tested. Rename-over-existing on Windows was not tested.

## FIXED (2026-10-26): NAM could corrupt memory on a damaged .nam model

The one real security/stability finding of the earlier session, in upstream NAM, not pump.

- **What it was:** a `.nam` file that is valid JSON but whose weights don't match its architecture made NAM read past
  the end of its weights array (heap-buffer-overflow in `set_weights_` of `Conv1D`, `Conv1x1`, `LSTMCell`) while
  `nam::get_dsp` built the network. A second class aborted on a missing JSON key (an `assert` in nlohmann's
  `operator[]`; in release builds an out-of-range read). A third: an architecture size of 2^32 wrapped to a negative
  matrix size. 11 of 300 fuzz cases crashed.
- **The fix, in three parts:**
  1. `patches/nam/0001-bounds-checked-weights-iterator.patch` (applied by `make`, see `patches/nam/README.md`): the
     weights cursor knows its end and throws on a read past it, so a short weights list is "Weight mismatch", not an
     overflow. A long one was already caught (and the LSTM's check, an assert, now throws too).
  2. `plugins/amp/NamJson.hpp`: AMP's copy of the JSON library turns its asserts into exceptions.
  3. `ModelGuard.hpp`: numbers in the architecture (not the weights or the metadata) must be under 2^24.
- **Verified:** `tests/fuzz_nam.cpp` under ASan+UBSan, one seed per process, seeds 1-600: 600 pass (257 rejected
  cleanly, 343 loaded and ran), 0 sanitizer reports. `test_amp` now checks four example models with 9 or 1 weights
  missing and 3 extra: all refused with a message.
- **Still there:** the crash marker (below) stays as a backstop for anything the fuzzer hasn't found. Report the
  overflow upstream (https://github.com/sdatkinson/NeuralAmpModelerCore) with the patch.

### What pump also does (the backstop)

- `ModelGuard.hpp` rejects files that are plainly not a model (not JSON, wrong types, missing keys, absurd size).
- The **crash marker**: `loadModel` writes a marker (temp folder, keyed on path + file size + mtime) before calling NAM
  and removes it after. If the host dies inside NAM the marker stays, and later loads of that unchanged file are
  refused with a message instead of crashing again. CLEAR removes the marker.

### Fuzzing it

```
# one-time: submodules, then build NAM and the harness with ASan+UBSan (macOS: see "Build gotchas" for SDKROOT)
git submodule update --init --recursive
make tests-asan                                  # does not cover amp; amp needs the NAM objects:
NAM=third_party/NeuralAmpModelerCore
c++ -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -std=c++17 -DNAM_SAMPLE_FLOAT -Iplugins/amp \
    -I$NAM -I$NAM/Dependencies/eigen -I$NAM/Dependencies/nlohmann -c plugins/amp/nam_unity.cpp -o build/asan/nam.o
c++ -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -std=c++17 -DNAM_SAMPLE_FLOAT -Icommon \
    -I$NAM -I$NAM/Dependencies/eigen -I$NAM/Dependencies/nlohmann -Iplugins/amp \
    tests/fuzz_nam.cpp build/asan/pffft.o build/asan/nam.o -lpthread -o build/asan/fuzz_nam
./build/asan/fuzz_nam 150 1       # cases, seed; stops at the first crash and keeps it in build/fuzz/crash.nam
```

`fuzz_nam` stops at the first crash. To count distinct crashes, run one case per process (a seed per process) and
group the sanitizer reports; that is how the 11-in-300 figure was obtained. Note `test_amp` itself passes under
ASan+UBSan: the crash only appears with damaged input.

## Build gotchas

- **`make -j` across plugins can fail** with `usage: mkdir ...` and a missing `bin/pump-<x>.vst3/Contents/Info.plist`.
  It is a race in DPF's bundle steps under `-j6`, not a code problem: the same plugin built alone succeeds. Build
  plugins one at a time (`make -C plugins/<name> -j4` inside is fine). CI has not shown this; do not assume it never will.
- **macOS SDK:** on the machine used for this session the default SDK (MacOSX27.0) could not link anything (a `.tbd`
  lists an architecture the installed linker does not know). Setting
  `SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk` per command fixed it. No system config was changed.
- `pkg-config: command not found` appears in the macOS build log and is harmless.
- zsh does not word-split unquoted variables: use arrays for flag lists in scripts run under zsh.
- Submodules (`dpf`, `third_party/NeuralAmpModelerCore` with eigen) must be initialised; a fresh clone has them empty.

## Findings from the review that turned out to be wrong (so nobody redoes them)

- "The WAV reader can allocate gigabytes": false. It was memory-safe; the real defects were silent acceptance of bad headers.
- "The preset code swallows errors": mostly false. Every failure path already calls `setStatus` with a specific message.
  The real issues were the unbounded read and truncate-before-write.
- "AMP's IR response curve is expensive": false. Worst case (16384 taps x 160 points) measured 15 to 22 ms, once per IR load.
- "No NaN or threading protection": false. Engines have per-block `isfinite` guards with state reset, lock-free atomic
  feeds, and the EQ worker thread is clean under ThreadSanitizer.
- `fonts.cpp` per plugin is an 85-byte stub including the shared file, not duplication.

## Still open (lower priority, no bug behind them)

- `common/ui/PumpUI.hpp` is 1,848 lines holding widgets, preset I/O, A/B, theme and config. Worth splitting; large; needs a
  screenshot regression check (`tools/shot.sh`).
- The per-block `chk` / `isfinite` / `clearState()` guard is hand-copied in about 20 engines. A shared helper would
  remove the duplication; each engine's tests already cover its behaviour.
- `common/ui/PumpUI.hpp` `writeGlobalTheme` ignores write failures (low stakes).
- No `.clang-format`; adding one project-wide would wreck `git blame`, so only apply it to new files.
- AMP and MACHINE are not in `SAN_TESTS` (they need NAM objects and the C firmware); extend the sanitizer targets if wanted.
  The MACHINE firmware (C, from the machine's repo) has had no sanitizer pass and is the likeliest place for another
  memory-safety bug.
- TSan was run on 10 tests, not the whole suite.
