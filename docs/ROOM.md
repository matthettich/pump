# pump ROOM: room measurement and correction

The plan, what's built, and how the measurement works. Stage 1 (measurement) is built; the rest is planned.

## Stages

1. **Measurement** — built (2026-10-28). `room/`: a single page (`bin/pump-room.html`, `make room-page`) that plays log
   sweeps out of each speaker, records any measurement mic with its calibration file, and shows the response, the
   impulse, and the timing and level differences between the speakers. The DSP is `common/dsp/Room.hpp` (tested in
   `tests/test_room.cpp`), compiled to wasm through `wasm/room_wasm.cpp`.
2. **Averaging and correction design** — built (2026-10-28). The app's CORRECTION view (`common/dsp/RoomCorrect.hpp`):
   the measurements ticked IN CORRECTION are power-averaged per speaker; the target (TILT about 1 kHz, BASS and
   TREBLE shelves) sits at the measured 200 Hz..2 kHz level, the same for both speakers; cuts follow the response
   smoothed 1/12 octave in the bass widening to 1/3 above 5 kHz, boosts only fill the 1/3-octave (to 1 octave) deficit
   (SKIP NULLS), none below the speaker's roll-off (PROTECT LOW END: the first point under 150 Hz where it stays more
   than 6 dB under the target, faded back in over half an octave), MAX BOOST / MAX CUT, FROM / UP TO (faded over a
   third of an octave), AMOUNT; TIME ALIGN delays the nearer speaker by the mean arrival difference; the preamp is
   the largest boost. SAVE PROFILE writes a `.pumproom` file (below); EXPORT FOR WINDOWS writes a zip with stereo
   minimum-phase filters at 44.1, 48, 88.2, 96 and 192 kHz (preamp and delays inside), a config file that picks the
   one for the device's rate (`If: sampleRate == ...` / `Convolution:`) and a read-me.
3. **Applying it** — built (2026-10-28).
   - **pump ROOM, the plugin** (`plugins/room`, VST3 / CLAP / AU): LOAD a `.pumproom`; the profile's text is kept in
     the session (state `profile`; `profile_file` remembers where it came from). AMOUNT (the preamp follows), PHASE
     (minimum: 256 samples of latency, the convolution block; linear: plus half the filter, 8192 samples at 48 kHz),
     TIME ALIGN, OUTPUT, bypass. The window shows the profile and, scaled by AMOUNT, measured / corrected / target and
     the correction. `RoomEngine.hpp` designs the filters on a worker thread at the host's rate and crossfades them in
     (tested in `tests/test_room.cpp`, also under TSan).
   - **System EQ** (Mac): bundles pump ROOM after the EQ. Menu: *Room Correction* (on / off) and *Show Room
     Correction…* (the plugin's window: load the profile there). Kept per output like the EQ (`room-<output>.plist`
     next to the EQ's files; a new output starts from the last one).
   - **Windows:** EXPORT FOR WINDOWS in the app (see stage 2) for the free system-wide equalizer; a native app maybe
     later.
   - **A Raspberry Pi in line** (2026-10-28, the owner's idea: a cheap box per room between the sources and the
     speakers): EXPORT FOR PI zips mono minimum-phase filters per speaker at 44.1-192 kHz (preamp and delays inside),
     a CamillaDSP config (`filename: /etc/pump-room/coeffs/pump-room-L-$samplerate$.wav`, pipeline `channels: [0]` /
     `[1]`; devices default to a USB line in/out interface named "CODEC", 16 bit, 48 kHz, chunksize 1024), an
     `install.sh` (filters and config to /etc/pump-room, CamillaDSP from its GitHub releases if missing, a systemd
     service; a re-install keeps the devices section) and a read-me. Checked: the zip, the YAML parses, the script
     parses and its device-keeping merge works. **Not run on a Pi yet**, and the CamillaDSP config was written from
     its documentation (v4.1), not tried in it.
3b. **Phase correction** — built (2026-10-28, asked for early by the owner). `groupDelayMs` in `RoomCorrect.hpp`
   reads each measurement's group delay against its direct sound with a frequency-dependent window (5 cycles, 2..300 ms)
   and smooths it over an octave (reflections ripple it; the octave averages them out); the positions are averaged per
   speaker; `designPhaseCorrection` makes the group delay to add (minus the excess over the 3..8 kHz average, in the
   PHASE band 60 Hz..3 kHz, faded over half an octave, limited to 20 ms). Linear-phase filters only (it advances the
   bass: that takes the latency): the app's ALIGN PHASE with LINEAR PHASE exports, the plugin in LINEAR phase. The
   profile carries it as a fifth column (group delay, ms; older readers ignore it). The CORRECTION view shows ARRIVAL
   BY FREQUENCY (measured faint, corrected bold) and the spread before and after. Tested: a speaker with a 4th-order
   roll-off at 50 Hz and a crossover allpass at 2 kHz (2.5 ms spread 100 Hz..2.5 kHz -> 0.4 ms), with reflections
   4.4 dB down (still within 0.35 ms of the speaker 300 Hz..3 kHz).
4. **Handheld measurement** — built (2026-10-28, the owner's goal, asked for early). The app's HANDHELD view
   (`app.js`, "handheld"): (1) FIND SPEAKERS with the mic against the left speaker's grille: a 0.25 s ping from each
   speaker gives the system latency (the left one is taken as 3 cm away) and the speakers' spacing; (2) SET CENTER
   at the listening spot: the two distances place the mic (speakers at x = -spacing/2 and +spacing/2, listener at
   y > 0); (3) START: 5, 9 or 13 points around the center (rings of 0.25 / 0.3 / 0.35 / 0.5 m); the mic is tracked with
   pings (about one a second: tail = latency + 60 ms), the close-up shows it with an arrow to the next dot and the
   text says how far and which way; on a dot (within 8 cm) and still (moved under 4 cm since the last ping) it
   measures both speakers (1 s sweeps), checks it didn't move (12 cm) and the levels, and adds the run ("Hand n",
   with its position, in the correction). Height isn't tracked (two speakers place the mic in a plane only).
   Tested: `tools/room_e2e.mjs` moves the page's simulated mic (`simMic` in the worklet): spacing within 1 cm,
   latency exact, center within 2 cm, all 5 points kept at the right positions, the guidance shown when off the dot.
   **Not tried with a real mic in a hand yet**: body reflections and handling noise are the open questions (short
   sweeps and averaging many points are the defence); the tolerances in `HAND` are first guesses.

## How the measurement works

- **One audio callback plays and records** (`room/worklet.js`), so the recording is locked to the playback sample
  for sample. A run plays 0.5 s of silence (the noise floor), then the left sweep and 2 s of tail, then the right
  sweep and its tail. Arrival times include the whole system latency, which is the same for both speakers in a run:
  their difference is real.
- **Sweep:** exponential, 10 Hz to 0.45 x the sample rate, a half-octave fade-in and a 5 ms fade-out, peak level as
  set. The response is trustworthy from 20 Hz to the sweep's end / 1.06 (shaded on the graph above that).
- **Deconvolution:** spectral division of the recording by the sweep, exact inside the sweep's band, regularised to
  zero outside it. Harmonic distortion lands at negative times, out of the response.
- **Arrival:** the first peak within 6 dB of the largest one in the first second (so a louder reflection doesn't win),
  located between samples on the band-limited waveform (good to about 0.01 sample in the tests).
- **Response:** the impulse windowed from 2 ms before the direct sound to WINDOW after it (last fifth faded),
  power spectrum, read on a 1/96-octave grid with fractional-octave power smoothing, minus the mic's calibration.
- **Levels:** the noise floor from the silence before the first sweep; each sweep's SNR, its peak at the mic
  (clipping above -1 dBFS) and dropouts are reported.

## Files

- **Profiles** (`.pumproom`, text): `pump-room-profile 1`, then `name`, `preamp <dB>`, and per speaker
  `channel L|R <delay ms>` followed by one line per point (`Hz correction measured target`, every 1/48 octave from
  20 Hz to 20 kHz) and `end`. Whoever applies it designs the filter from the correction curve at its own sample rate
  (`designFir`: minimum phase from the real cepstrum, or linear phase with a Tukey window; 16384 taps at 44.1/48 kHz,
  more at higher rates; the delay is built into the filter). Numbers are written and read with '.' whatever the locale.

- **Calibration files:** one point per line, frequency and dB (phase ignored), separated by spaces, tabs, commas or
  semicolons (with semicolons a decimal comma is fine). Headers, comments, a BOM and CRLF are skipped; a
  "Sens Factor" line is read but not applied (levels here are relative). The curve is subtracted, linear in
  log-frequency between points and held flat past the ends.
- **Sessions** (`pump-room-*.json`, `format: "pump-room-session"`, version 1): the calibration file's text and,
  per measurement and speaker, the impulse response from 20 ms before the direct sound to 1 s after it
  (float32, base64), its start in the run (`irStart`), the arrival, polarity and levels.
- **Exports:** TXT (frequency, dB, phase 0, every 1/48 octave, with the current smoothing, window and calibration:
  the usual text format other tools import) and WAV (the impulse response, 32-bit float).

## Studio layouts, weights and spread

HANDHELD's MIX layout is 17 points: the center at ear height, 15 cm above and below; a ring of 6 at 30 cm alternating
above and below; an ellipse of 8 (70 × 50 cm) at ear height. Weights 1, 0.75 and 0.5 (`run.weight`, saved in sessions).
+ COUCH adds 5 points around a second center set with SET COUCH, each weighted so the couch is a quarter of the
total. Height isn't tracked (two speakers place the mic in a plane); the app says where to hold it.

`room_corr_add` takes each measurement's weight: the average is a weighted power average, the group delay a
weighted mean. `positionSpread` is the weighted standard deviation of the positions' responses (each smoothed 1/6 to
1/3 octave first), drawn as shading around the average. SPREAD-AWARE (4 or more positions) scales the correction by
`spreadTrust`: all of it up to 2 dB of spread, falling to a quarter by 8 dB.

## Headphones

A curve rather than a measurement: `room_corr_add_curve` puts a frequency / dB list on the correction grid (no timing),
and `room_corr_target` sets a base target curve the tilt and shelves sit on (`TargetSpec::curveF / curveDb`). The
over-ear and in-ear targets are in app.js as a few filters each (fitted to the published curves: 0.1 and 0.05 dB RMS).
An EQ file is applied as it is: its curve, negated, is the "measured" response with a flat target, and `smooth = 0`
(design parameter 16) skips the smoothing that would soften its narrow filters. Profiles say `kind headphones`.

## Other software and devices (EXPORT)

Export only: each reads the current design and writes files; nothing else changes.
- *Graphic EQ* (Wavelet): `GraphicEQ:` line, 127 points at 20 Hz × 1.0563^k, the speakers averaged, preamp in it.
- *Parametric EQ*: `fitPeq` fits up to N bands (LS / PK / HS) to the correction on a 1/12-octave grid to the band's
  top, at 48 kHz, by Levenberg-Marquardt (bands added where the error is largest, then all refined), with a small
  cost on every gain so bands don't fight, boosts capped near the correction's own, cancelling pairs dropped. Limits:
  gain −15..+12 dB, Q 0.3..8 (shelves 0.5..1). Written as `Preamp:` + `Filter N: ON PK Fc … Hz Gain … dB Q …`
  (shelves LSC / HSC); a room gets both-speaker (averaged), L and R files. EQ files read in are evaluated at 48 kHz too.
- *Convolution*: a stereo float WAV per rate (44.1–192 kHz) in a folder, zipped, with a README per player.

## System EQ's profile library

System EQ keeps profiles in `~/Library/Application Support/wake/System EQ/Profiles`. Its menu lists them (room and
headphone apart, from each file's `kind`), ticking the one whose path is in pump ROOM's `profile_file` state; choosing
one puts the file's text into the `profile` state through the unit's ClassInfo (the other settings kept), for the
current output. `.pumproom` is an exported type the app owns, so opening a file adds it and switches to it.

The level hold: `heldLevelDb` (preferences) is the lowest preamp seen in the library or loaded, and never rises by
itself. A gain after the room stage (`trimTarget`, ramped: 5 ms down, 50 ms up; the engine starts at it) is
`held − own preamp` while a profile plays and `held` when there is none or Room Correction is off, so every state
plays at the same level. Removing the tap (quitting, EQ off) is the only way to full level; EQ off asks first.
Room Profiles writes the same make-up as a `Preamp:` line in the active file (and `held` alone for No Profile);
its Quit writes an empty active file, asking first.

## Room Profiles (Windows)

`system/windows/room-profiles.cpp`, built by `system/windows/build.sh` (MinGW, like the plugins). The platform-free
part is `ApoProfile.hpp` (tested by `tests/test_room_profiles.cpp`): a profile becomes one stereo float WAV per common
rate (minimum phase, preamp and delays inside, as EXPORT FOR WINDOWS) and an active file that picks the one for the
device's rate. The app adds `Include: pump-room-active.txt` to the equalizer's config.txt once and switches by
rewriting that file (written beside it and swapped in, so it is never read half-written). Choices are kept per output
device id (`room-profiles.ini`) and reapplied when Windows' default output changes. `.pumproom` is registered for the
current user; a second start hands its files to the running copy (WM_COPYDATA). When the config folder can't be
written, *Allow Changes…* grants users modify rights on it (icacls, one administrator prompt).

## On the web

CI's `pages` job puts the room job's page on the `gh-pages` branch (as `index.html`, with `.nojekyll`) on every push to
main, force-pushed as a single commit with plain git. GitHub Pages serves it at https://matthettich.github.io/pump/
once the repo's Settings > Pages source is "Deploy from a branch: gh-pages, / (root)" (a one-time setting the owner
makes; sessions can't reach the Pages API). HTTPS is what the browser needs for the microphone.

### Updates

`room/build.sh` stamps each page with its commit and commit time (`ROOM_BUILD`) and writes the same as
`pump-room-version.json` beside it; the `pages` job publishes that as `version.json`. The page fetches it 3 s after
opening, when it comes back into view (at most every 10 minutes) and every 30 minutes; a newer build shows a bar under
the header. On the web, UPDATE reloads the page under a new URL (so the cached copy isn't reused), after a warning if
there are unsaved measurements; in the desktop app or a page opened from a file, DOWNLOAD opens the latest release.
LATER hides the bar until the next newer build. The simulated room (`#sim`) skips the check.

## The desktop app

`room/electron/` wraps the same page in Electron (`main.js`): one window, only the microphone and output choice
allowed, no network but the update check, Save / Open through the system's file dialogs. `sh room/electron/build.sh` (after `make room-page`)
packages it on a Mac (universal `pump ROOM.app`, ad-hoc signed, zipped) or on Windows (installer + zip); CI's
`room-app-mac` and `room-app-windows` jobs do that and run the packaged app once with `PUMP_ROOM_SMOKE=1`, which
measures the simulated room and fails the build if the timing is off. Electron (45.1.0) and its packager (26.18.0) come
from npm at build time, pinned in build.sh. Not notarized yet: the first open on a Mac is right-click > Open.

## Notes

- The page runs from a file (`file://`). The worklet is loaded from a `data:` URL: a `blob:` module is refused as
  cross-origin when the page's origin is null.
- Choosing the output needs `AudioContext.setSinkId` (Chrome, Edge); elsewhere it plays to the system output.
- On a phone (Android Chrome; a USB mic) it plays to the phone's output, and asks for a screen wake lock while audio
  runs (a sleeping screen stops the audio mid-sweep); the lock is asked for again when the page comes back.
- **LINK: BLUETOOTH** (saved in the browser and with each run, `bt` in sessions): the lead-in is at least 1.5 s
  (receivers wake up and fade in) with the noise floor read from its second half, the search for the sweep at least
  2.4 s, the tail at least 2.6 s, and a -80 dBFS whisper of noise fills the silence (some receivers mute on digital
  silence). Its delay changes between connections and its clock drifts, so a correction that uses any Bluetooth run
  is EQ only: no time alignment, no phase alignment, UP TO capped at 16 kHz (codecs often stop there); the verdict
  says timing isn't measured, and HANDHELD (which tracks the mic by timing) is off.
- The mic is opened with echo cancellation, noise suppression and automatic gain off.
- `#sim` at the end of the page's address replaces the speakers and mic with a simulated room (two speakers at 2.9
  and 3.1 m, a mode at 110 Hz, reflections, 12 ms latency). `node tools/room_e2e.mjs` runs the built page that way
  in headless Chromium (Playwright) and checks the timing, a calibration file and a save / reopen.
