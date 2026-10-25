# pump MACHINE: copyright, licence and trademark notice

This folder is **pump MACHINE**: the drum, snare, cymbal and hi-hat voices of the LXR-02 drum machine's firmware,
compiled into a plugin (VST3, CLAP, AU and a standalone app) and played from MIDI and its window.

## Licence

Everything in this folder is free software under the **GNU General Public License version 3** (GPL-3.0-only), with
the additional permission and the additional terms (GPL-3.0 section 7) that come with the firmware, which are in
[`fw/firmware/NOTICE.md`](fw/firmware/NOTICE.md). The full licence text is in [`fw/LICENSE`](fw/LICENSE).

A built pump MACHINE is a work based on the firmware, and it also contains pump's own shared code (`common/`), the
plugin build files (`plugins/Plugin.mk`, `patches/`, `tools/`) and DPF (ISC). **pump's authors (wake) license the pump
code compiled into pump MACHINE under the GPL-3.0-only, with the firmware's additional terms, as part of pump
MACHINE.** That grant covers pump MACHINE; the other pump plugins keep their own terms (pump's code in them is
© wake), and nothing in them uses this folder. The source of every published pump MACHINE is the pump repository at
https://github.com/matthettich/pump, at the commit the build was made from (the release notes name it); the
packages carry this notice and the licence text.

- **The firmware**: `fw/firmware/` holds the firmware files this build reads, unchanged, with the firmware's own
  LICENSE and NOTICE.md. Copyright (C) 2012-2026 Erica Synths. Source:
  https://github.com/erica-synths/lxr-desktop-app, commit `a43cd4fcda23ec3d577dc425261c3eb28bd5abff` (23 Oct 2026),
  by way of Thunder Plus's `lxr/src/firmware/`.
- **Additions**, Copyright (C) 2026 the pump and Thunder Plus authors, under the same licence and terms: `fw/stub/`
  (stand-ins for the STM32 HAL, the Cortex-M DSP instructions and ST's USB header, from Thunder Plus),
  `fw/tools/lxr_meta.py` (from Thunder Plus), `fw/tools/machine_meta.py`, `fw/machine_fw.c` and `fw/machine_fw.h`
  (grown from Thunder Plus's `lxr_wasm.c`: it reproduces what the firmware does when a kit is loaded, a knob is
  turned, a voice is triggered and a block is mixed, and lists the firmware's variables so each plugin instance keeps
  its own copy), `fw/machine_unity.c` and `fw/machine_unity2.c`, and the plugin itself: `MachineMeta.hpp` (generated
  from the firmware's menu tables), `MachineParams.hpp`, `MachineEngine.hpp`, `PluginMachine.*`, `UIMachine.cpp`,
  `DistrhoPluginInfo.h`, `Makefile`, and `../../tests/test_machine.cpp`.

The Corresponding Source of a built pump MACHINE is this folder together with the pump repository it is built in
(`common/`, `dpf/`, `plugins/Plugin.mk`, `patches/`, `tools/`): `make -C plugins/machine` rebuilds it.

## Modified version

This is a **modified version** of the firmware's sound engine (GPL-3.0 section 7(c)): it runs inside a plugin host on
a computer, not on the machine; it has no sequencer, no screen menus and no SD card; its two stereo outputs are summed
into one; and it is named "pump MACHINE". Its window and its description say that it is a modified version. It is
**not official Erica Synths firmware or software**, and Erica Synths provides no support for it.

**Differences from the machine.** The hardware random number generator is a seeded xorshift (so a hit's noise
repeats from session to session); the voice faders are at the top; the tempo that synced LFOs and the delay follow is
the host's; the machine's per-voice audio outputs and the parameter for them are left out (everything plays on the
main stereo output), and so for now is the FX bus (its delay, reverb and compressor have no page yet); the voices'
"Length", "Channel" and "Note" (sequencer settings) are left out, and MIDI follows pump's map (README).

## Trademarks

pump is not affiliated with, approved by or endorsed by Erica Synths. "Erica Synths", "LXR" and "LXR-02" are Erica
Synths' names; they appear here, in `fw/` and in the source files' notices only to say truthfully where this code
comes from (GPL-3.0 section 7(e) terms in `fw/firmware/NOTICE.md`). They are not part of the plugin's name.
