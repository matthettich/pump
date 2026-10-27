# Patches to DPF

Fixes to DPF (the plugin framework, `dpf/` submodule) that pump needs and upstream doesn't have yet. `make`
applies them automatically, in order (`tools/apply-dpf-patches.sh`, idempotent: it finds the last applied patch of
the series, since later patches may touch lines earlier ones added), so CI and local builds both get them; the
submodule itself stays on upstream DPF. If a DPF update makes one fail to apply, refresh or drop it.

| Patch | Fixes |
|---|---|
| 0001-vst3-report-latency-changes | VST3: latency changes (linear phase) were never reported to the host: DPF sent the constructor's latency every time, so the host never restarted for the new delay. |
| 0002-vst3-last-point-at-frame-zero-wins | VST3: when a host queues several points at sample 0 for one parameter (Ableton Live pushes its own value when a gesture begins, then the edited value), DPF applied the first (stale) one. |
| 0003-vst3-apply-parameter-flushes | VST3: parameter changes sent in zero-length process calls (host flushing while the plugin is idle) were dropped. |
| 0004-au-bypass-notification-index | AU: the bypass-changed notification compared the element instead of the parameter index. |
| 0005-vst3-tolerate-unused-queues-and-null-outputs | VST3: a parameter queue with an invalid id (kNoParamId, which Steinberg's validator sends) made DPF drop every later queue in that block; and a host that passes no output parameter list got a DPF assertion printed to stderr from the audio thread on every block. |
| 0006-au-report-latency-changes | AU: latency changes were only answered when the host asked (DPF: "TODO report latency changes"), so Logic/Live never re-aligned after switching to linear phase, oversampling or lookahead. The wrapper now announces kAudioUnitProperty_Latency to property listeners and as an AU property-change event after the render that changed it (the same way it already announces output parameter changes). |
| 0007-windows-embedded-window-matches-host-dpi-awareness | Windows: the editor window is created with its host window's DPI awareness. A per-monitor-aware plugin window inside a DPI-unaware host window (Renoise with "auto-scale external editor on HiDPI screens" off, at 150 % display scaling) was drawn but got no or misplaced mouse input. The scale factor then follows the window: 1x when Windows scales it, the window's own DPI otherwise. |
| 0008-vst3-state-strings-keep-utf8 | VST3: state strings sent between the editor and the plugin (a loaded file's path) lost every non-ASCII byte and left those places unset (uninitialised memory), so a file in a folder named with accents or Japanese never loaded. The state message now carries every byte; and DPF's ASCII-only UTF-16 copy for host strings drops non-ASCII characters without leaving holes. |

Worth sending upstream: https://github.com/DISTRHO/DPF
