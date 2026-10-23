#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake"
#define DISTRHO_PLUGIN_NAME            "pump COMP"
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#comp"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.comp"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmCO

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
// inputs 1-2: main stereo; inputs 3-4: external sidechain (a VST3 aux bus / CLAP non-main port)
#define DISTRHO_PLUGIN_NUM_INPUTS      4
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
// AU: also offer plain stereo (DPF's AU wrapper has no aux buses; the sidechain then reads as silence)
#define DISTRHO_PLUGIN_EXTRA_IO        { 2, 2 },
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      620

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Dynamics"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "compressor", "stereo", "mastering"
#define DISTRHO_PLUGIN_AU_TYPE         aufx

#endif
