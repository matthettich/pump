#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump CHOP Live Chopper"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Live Chopper"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "A live chopper: the last bar of the input, always recording, cut into slices you play from eight momentary pads (MIDI notes or computer keys, learnable): jump, loop between two, pitch, reverse and an effect per pad."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#chop"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.chop"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmCH

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_LATENCY    0
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1   // moves land on the song's grid
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1   // each pad answers its own MIDI note, as a momentary switch

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      690

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Delay"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "glitch", "delay", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumf   /* a music effect: audio in and MIDI in */

#endif
