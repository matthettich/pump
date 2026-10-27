#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump VERB Reverb"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Reverb"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Algorithmic reverb: smooth modern halls, rooms, chambers and plates, grainy late-70s and lush 80s digital, a 90s rack, gated, reverse and shimmer."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#verb"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.verb"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmVE

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1   // pre-delay sync

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      620

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Reverb"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "reverb", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aufx

#endif
