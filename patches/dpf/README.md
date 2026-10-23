# Patches to DPF

Fixes to DPF (the plugin framework, `dpf/` submodule) that pump needs and upstream doesn't have yet. `make`
applies them automatically (`tools/apply-dpf-patches.sh`, idempotent), so CI and local builds both get them; the
submodule itself stays on upstream DPF. If a DPF update makes one fail to apply, refresh or drop it.

| Patch | Fixes |
|---|---|
| 0001-vst3-report-latency-changes | VST3: latency changes (linear phase) were never reported to the host: DPF sent the constructor's latency every time, so the host never restarted for the new delay. |
| 0002-vst3-last-point-at-frame-zero-wins | VST3: when a host queues several points at sample 0 for one parameter (Ableton Live pushes its own value when a gesture begins, then the edited value), DPF applied the first (stale) one. |
| 0003-vst3-apply-parameter-flushes | VST3: parameter changes sent in zero-length process calls (host flushing while the plugin is idle) were dropped. |
| 0004-au-bypass-notification-index | AU: the bypass-changed notification compared the element instead of the parameter index. |

Worth sending upstream: https://github.com/DISTRHO/DPF
