#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump BOUNCE Bouncing Delay"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Delay"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Bouncing-ball delay: repeats whose gaps shrink (or grow) geometrically, as a multitap or as a slice re-fired by a trigger with pitch, tone and level changing on every bounce."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#bounce"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.bounce"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmBO

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1   // a note-on fires a drop (TRIGGER = MIDI)
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1   // synced times and the SYNC trigger follow the host tempo
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       860
#define DISTRHO_UI_DEFAULT_HEIGHT      528

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Delay"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "delay", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumf   /* a music effect: audio in and MIDI in */

#endif
