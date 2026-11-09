#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump STRETCH Extreme Stretch"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Extreme Stretch"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Extreme time stretch: the last 12 seconds smeared into a long ambient wash up to 50 times slower (phase-randomised, the pitch stays), or one moment frozen into a drifting drone."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#stretch"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.stretch"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmXT

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1   // a song start (or a jump back) moves the read head to the newest sound

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       900
#define DISTRHO_UI_DEFAULT_HEIGHT      470

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Delay"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "delay", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aufx

#endif
