# pump: handover

The pick-up doc: where things stand, what is verified and what is not, and what is open. Conventions and the layout
are in [DEVELOPING.md](DEVELOPING.md) and the plan is in [ROADMAP.md](ROADMAP.md); this file does not repeat them.
Last updated 2026-10-27 (GUI pass, COIL modulation, LIFE sound, AU crash fix).

## State

- 28 plugins (effects, mastering, instruments), VST3 + CLAP everywhere, AU on macOS. All 28 build on macOS with
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

## NEXT: LIFE's sound against its reference (in progress, 2026-10-27)

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
| COIL VOICES (1-16, default 8) and **modulation**: 2 LFOs, velocity, note, wheel, aftertouch, random per note; 32 connection slots (appended params); drag a source's grip onto a dial; MOD page lists connections | `common/dsp/Mod.hpp`, `plugins/coil/CoilDsp.hpp`, `UICoil.cpp` | `tests/test_coil_mod.cpp` (no connection = identical output, removal leaves no trace, sync, CPU), `parity_coil` (plugin = web build), UI driven with `tools/xdo.cpp` |
| Drum synths' pads on C4 up in whole steps, note shown on each pad; old GM notes still play (STRIKE's old 60-67 range partly overlaps) | BOING, KIT, STRIKE, MACHINE | `== notes` sections in their tests |
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
