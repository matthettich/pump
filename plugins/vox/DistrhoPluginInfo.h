#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake"
#define DISTRHO_PLUGIN_NAME            "pump VOX Vocoder"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Vocoder"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Vocoder: classic band vocoder, spectral, talkbox and convolution modes, with a built-in carrier synth that plays MIDI or follows your voice."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#vox"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.vox"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmVX

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
// inputs 1-2: the track; inputs 3-4: the sidechain (a VST3 aux bus / CLAP non-main port): the carrier or the voice
#define DISTRHO_PLUGIN_NUM_INPUTS      4
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
// AU: also offer plain stereo (DPF's AU wrapper has no aux buses; use the synth carrier or Split L/R there)
#define DISTRHO_PLUGIN_EXTRA_IO        { 2, 2 },
// MIDI plays the built-in carrier synth
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1040
#define DISTRHO_UI_DEFAULT_HEIGHT      700

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Modulation"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumf   /* a music effect: audio in and MIDI in */

#endif
