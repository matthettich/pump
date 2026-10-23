# …pump roadmap

Planned work that isn't scheduled yet. Newest decisions at the top of each item.

## Instance List (FabFilter-style) — planned, nice to have

One window that shows every pump EQ in the session, like FabFilter's Instance List
(https://fabfilter.com/help/pro-q/using/instance-list). Decided 2026-10-23: build it in tiers, after the EQ bug audit;
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
  level (Pro-Q 4 style), so masking between tracks is visible.
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
  AU `kAudioUnitProperty_ContextName` (name only; FabFilter notes AU gives no colour or order), CLAP `track-info`
  extension (host-dependent).
- Carry it as a patch on DPF: either a fork of DPF as the submodule, or a patch file applied in CI before building.
  Upstreaming to DPF would remove the maintenance cost.

### When other pump plugins exist

Make the registry generic (plugin type + feed interface) so the compressor, limiter etc. can join the same list, as
FabFilter's does across Pro-Q, Pro-C, Pro-DS and Pro-G.
