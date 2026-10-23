# …pump roadmap

Planned work that isn't scheduled yet. Newest decisions at the top of each item.

## Instance List (standard-style) — planned, nice to have

One window that shows every pump EQ in the session, like the Instance List
. Decided 2026-10-23: build it in tiers, after the EQ bug audit;
tier 1 first, tier 2 only once tier 1 is in use.

All instances a host loads into one process share memory, so a process-wide registry can see them all. That covers
Ableton Live and Logic (AUv2) in their normal setup. Hosts that run plugins out of process (Bitwig's sandbox modes,
Reaper's bridging/firewalling, Logic running AUs out of process) only see the instances in the same process; the list
should say so rather than look empty or wrong.

### Tier 1 — list, curves, spectra, collisions (≈ one focused session, mostly new files)

- `common/InstanceRegistry.hpp`: process-wide, mutex-guarded list of live instances. Each plugin registers in its
  constructor and unregisters in its destructor. Entries hand out `std::shared_ptr` to a small "feed" object owned
  jointly by the plugin and any viewer, so a window that is drawing another instance can never touch freed memory when
  that instance is removed (viewers hold `weak_ptr` and `lock()` per frame).
- Feed contents, all written by the owning plugin and read lock-free by any UI:
  - the post-EQ analyzer ring (`pump::ScopeRing` is already multi-reader: `readLatest()` doesn't consume);
  - a snapshot of the band specs for drawing the curve (double buffer + sequence counter, published when params change);
  - an output level for the row (not `pump::MeterValue`, which resets when read and so only works with one reader);
  - a display name and a stable id (creation order).
- UI: an INSTANCES button opens a panel listing each instance with a mini curve and spectrum; the current instance
  is marked. Selecting another instance overlays its spectrum on the main display ("external spectrum").
- Collision view: shade frequencies where this instance's spectrum and the selected one are both loud and close in
  level (EQ style), so masking between tracks is visible.
- Names: DPF gives plugins no track name, so rows are "EQ 1", "EQ 2"… in creation order; a user label can be stored in
  the instance's own `ui` state (round-trips with the session).
- Risks: thread lifetime (handled by the shared feed), CPU when many instances are open (analyse at the UI frame rate
  only for visible rows), and no ordering by track until tier 3.

### Tier 2 — edit other instances from the list (harder)

- Each feed gets a lock-free queue of remote edits (band add/move/delete, bypass). The owning plugin applies them at
  block start and must tell its host, or automation, saving and the host's view of the parameters drift.
- DPF's `requestParameterValueChange()` (DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST) reports such changes for
  VST3 (as output parameter changes during processing) and AU (parameter-change event); in DPF's CLAP wrapper it is a
  stub that does nothing, so CLAP needs a DPF patch (flush output param events) or no remote editing.
- Hosts won't record these as undoable gestures; the list should keep its own undo for remote edits.
- A plugin can't open another instance's editor window, so "open" isn't possible; editing happens in the list itself.

### Tier 3 — track names, colours and order (needs DPF changes)

- DPF exposes none of this. Needed per format: VST3 `IInfoListener::setChannelContextInfos` (name, colour, index),
  AU `kAudioUnitProperty_ContextName` (name only; standard notes AU gives no colour or order), CLAP `track-info`
  extension (host-dependent).
- Carry it as a patch on DPF: either a fork of DPF as the submodule, or a patch file applied in CI before building.
  Upstreaming to DPF would remove the maintenance cost.

### When other pump plugins exist

Make the registry generic (plugin type + feed interface) so the compressor, limiter etc. can join the same list, as
the does across EQ, compressor, de-esser and gate.

## Known issues deferred from the EQ audit (2026-10-23)

The audit's confirmed bugs were fixed (see the commit "EQ: fixes from a full audit"). These were found too but need
more design or DPF work; listed roughly by impact.

### DSP
- **Matched design near Nyquist / infeasible matches** (`common/dsp/Biquad.hpp` `matchedDesign`, `bestDesign`):
  clamping `B2` keeps DC/Nyquist exact (fixes low cuts at very low Q leaking up to +7 dB at DC), but on its own it
  makes some +30 dB high shelves at 17–20 kHz much worse (8 → 60+ dB error in a 92k-design sweep), because
  `sectionError` ignores every point where the analog response is below -30 dB. Needs the clamp *and* a selection
  metric that penalises output above the floor where the analog is below it, verified with the full sweep.
- **Bands set above Nyquist** (Frequency goes to 30 kHz at any rate): pole mapping wraps; resonant band pass/high cut
  at 30 kHz at 48k can boost +16…+50 dB around 14–18 kHz. Fix: a candidate design with the pole frequency clamped
  below Nyquist (or clamp the design frequency, as `designDetector` does), picked with the improved metric above.
- **Band order between modes** with mixed L/R and M/S bands differs (linear phase applies all L/R bands, then M/S).
- **Auto gain ignores stereo placement** (side-only bands change the mono estimate; +1.5 dB with "Wide air").
- **Solo of a Left/Right/Mid/Side band** plays that band's region of both channels: decide whether it should solo
  only the band's own channel (EQ does).
- The dynamic detector in zero latency listens to the band's input in the serial chain (after earlier bands), in
  linear phase to the plugin input; identical unless other bands overlap the detector region.

### Host integration (mostly DPF)
- **AU latency changes** are now announced by patch 0006 (from the render thread, like DPF's output parameter
  notifications); a main-thread notification would be cleaner. Untested in Logic.
- **AU host automation reaches the UI on the render thread** (DPF notifies the UI synchronously from
  `auSetParameter`); ramped AU parameter events are dropped.
- **CLAP: UI edits aren't delivered while the host isn't processing** (DPF only drains its UI queue in `process()`;
  `flush` doesn't, and it never calls `request_flush`).
- **Sample-accurate automation:** DPF applies VST3 changes at block start/end only, CLAP all at block start, AU
  ignores offsets. Would need DPF to split `run()` at event offsets.
- **Hosts ignore the logarithmic hint**, so Frequency and Q automation lanes are linear in VST3/CLAP/AU.
- **The UI's 300 ms echo filter** can override a legitimate host change made within 300 ms of a UI edit (host undo,
  touch automation); a history of the values the UI itself sent would let it reject only stale echoes.
- **UI undo history isn't cleared** when the host recalls a state (plugin UNDO can re-apply the pre-recall settings);
  state-only changes (A/B COPY, display range) don't mark the host project dirty.
- **VST3 parameter ids shifted by one** when host programs were removed (2026-10-23): Live sets saved with an earlier
  build have automation/mappings on the neighbouring parameter (values still restore, by symbol). Decide whether to
  reserve the old id slot (which would then break sets saved since). From now on parameters are append-only.

### UI
- Precise trackpad scrolling counts every small event as a full wheel step (accumulate fractional deltas).
- Ctrl/Cmd-clicking the only selected node doesn't deselect it; the preset name stays in the header after undoing a
  preset load.
- `dynOffset`/`latencyNow` are plain reads of audio-thread data (formally a race; display only).
