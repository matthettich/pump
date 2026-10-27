#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump STUTTER Beat Repeat"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Beat Repeat"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Tempo-locked repeats, rolls, reverses, tape stops, half speed, slice shuffles, gates and glitches: by chance on the beat, from pads or from MIDI notes."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#stutter"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.stutter"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmST

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1   // moves land on the song's grid
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1   // notes C..G (any octave) play the eight moves while held

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      866

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Delay"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "glitch", "delay", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumf   /* a music effect: audio in and MIDI in */

#endif
