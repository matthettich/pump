#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump FLEX Pattern FX"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Pattern FX"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Pattern effects on the song's grid: 510 patterns in two banks (repeats, rolls, reverses, tape stops, scratches, gates, shuffles, euclid gates, freezes, skips, grains...) in eight slots; automate PLAY to switch."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#flex"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.flex"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmFX

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1   // the patterns run on the song's position

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      372

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "glitch", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aufx

#endif
