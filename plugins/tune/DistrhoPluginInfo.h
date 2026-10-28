#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump TUNE Pitch Correction"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Pitch Correction"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Pitch correction for a solo voice: live key/scale or MIDI correction, a graph to capture and edit notes, up to four harmonies, formant shift."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#tune"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.tune"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmTN

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
// MIDI: the notes to sing (lead) or the harmony notes
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1   // the graph's notes are placed on the host's timeline
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1100
#define DISTRHO_UI_DEFAULT_HEIGHT      620

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Pitch Shift"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "pitch-correction", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumf   /* a music effect: audio in and MIDI in */

#endif
