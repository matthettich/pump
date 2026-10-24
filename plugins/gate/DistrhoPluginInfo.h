#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake"
#define DISTRHO_PLUGIN_NAME            "pump GATE Sequenced Gate"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Sequenced Gate"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Tempo-synced step gate: up to 32 steps with swing, envelope, smoothing, a resonant filter mode and stereo offset."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#gate"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.gate"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmGA

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1   // locks the pattern to the host's song position and tempo
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      620

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Dynamics|Modulation"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "gate", "tremolo", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aufx

#endif
