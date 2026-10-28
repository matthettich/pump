#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump ROOM Room Correction"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Room Correction"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Applies a room correction profile (.pumproom) made with the pump ROOM app: per-speaker correction, time alignment and headroom, minimum or linear phase, amount."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#room"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.room"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmRM

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
#define DISTRHO_UI_FILE_BROWSER        1   // LOAD opens a file dialog for profiles
#define DISTRHO_UI_DEFAULT_WIDTH       940
#define DISTRHO_UI_DEFAULT_HEIGHT      540

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|EQ|Tools"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "equalizer", "utility", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aufx

#endif
