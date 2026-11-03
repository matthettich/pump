#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump STRIP Channel Strip"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Channel Strip"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "A channel strip that is eight consoles: preamp and transformer colour, filters, a four-band EQ in each console's curves, gate and compressor."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#strip"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.strip"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmCS

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
#define DISTRHO_UI_DEFAULT_WIDTH       1140
#define DISTRHO_UI_DEFAULT_HEIGHT      600

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|EQ|Dynamics"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "equalizer", "compressor", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aufx

#endif
