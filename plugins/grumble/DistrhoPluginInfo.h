#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump GRUMBLE Creature Voice"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Creature Voice"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Creature voice designer: four layers (BEAST pitch and formant with fry, SPLIT a second wobbling voice, GROWL a rough throat vocoded by your voice, SWARM a grain cloud) blended on a morph pad, with gate, drive, tone and a cave."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#grumble"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.grumble"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmGU

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      620

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Pitch Shift"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "pitch-shifter", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aufx

#endif
