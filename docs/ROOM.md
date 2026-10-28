# pump ROOM: room measurement and correction

The plan, what's built, and how the measurement works. Stage 1 (measurement) is built; the rest is planned.

## Stages

1. **Measurement** — built (2026-10-28). `room/`: a single page (`bin/pump-room.html`, `make room`) that plays log
   sweeps out of each speaker, records any measurement mic with its calibration file, and shows the response, the
   impulse, and the timing and level differences between the speakers. The DSP is `common/dsp/Room.hpp` (tested in
   `tests/test_room.cpp`), compiled to wasm through `wasm/room_wasm.cpp`.
2. **Averaging and correction design** — next. Several positions around the listening spot averaged (power
   average, frequency-dependent window), a target curve (flat, tilt, drawn), and the correction: cuts first, limited
   boost, no filling of deep nulls, correction faded out above a chosen frequency, per-channel delay and trim from
   the arrival times. Saved as a profile (FIR kernels or filters + delays + trims).
3. **Applying it** — pump ROOM as a plugin (VST3 / CLAP / AU) that loads a profile: minimum phase (no latency) or
   linear phase; in System EQ on the Mac (after the EQ, per output); on Windows, exported as an impulse response and
   a config for the free system-wide convolution equalizer the owner uses on Windows (named in the owner's
   notes, not here). A native Windows system-wide app maybe later.
4. **Handheld measurement** — the long-term goal (the owner's): fast, easy tuning holding the mic in the hand. Before
   each sweep, short pings from each speaker give the mic's distance to each (time of flight, from the arrival times
   stage 1 already measures to a few hundredths of a sample); with the speakers' spacing that places the mic. A map
   guides it to the next point, measurements too far off or too noisy are refused, about 20-40 points around the
   listening position are averaged. Needs: a latency reference (the mic held at a known spot first, or a loopback
   input), short sweeps, a hold-still check, and averaging that tolerates the body's reflections.

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

- **Calibration files:** one point per line, frequency and dB (phase ignored), separated by spaces, tabs, commas or
  semicolons (with semicolons a decimal comma is fine). Headers, comments, a BOM and CRLF are skipped; a
  "Sens Factor" line is read but not applied (levels here are relative). The curve is subtracted, linear in
  log-frequency between points and held flat past the ends.
- **Sessions** (`pump-room-*.json`, `format: "pump-room-session"`, version 1): the calibration file's text and,
  per measurement and speaker, the impulse response from 20 ms before the direct sound to 1 s after it
  (float32, base64), its start in the run (`irStart`), the arrival, polarity and levels.
- **Exports:** TXT (frequency, dB, phase 0, every 1/48 octave, with the current smoothing, window and calibration:
  the usual text format other tools import) and WAV (the impulse response, 32-bit float).

## Notes

- The page runs from a file (`file://`). The worklet is loaded from a `data:` URL: a `blob:` module is refused as
  cross-origin when the page's origin is null.
- Choosing the output needs `AudioContext.setSinkId` (Chrome, Edge); elsewhere it plays to the system output.
- The mic is opened with echo cancellation, noise suppression and automatic gain off.
- `#sim` at the end of the page's address replaces the speakers and mic with a simulated room (two speakers at 2.9
  and 3.1 m, a mode at 110 Hz, reflections, 12 ms latency). `node tools/room_e2e.mjs` runs the built page that way
  in headless Chromium (Playwright) and checks the timing, a calibration file and a save / reopen.
