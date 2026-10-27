#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake effects"
#define DISTRHO_PLUGIN_NAME            "pump TWANG Spring Resonator"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Spring Resonator"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Tuned spring resonator: SPRING's springs tuned to a chord or to MIDI notes, rung by the audio you feed them, from clanging metal to clean strings."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#twang"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.twang"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmTW

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1   // MIDI notes can tune the springs
#define DISTRHO_PLUGIN_WANT_LATENCY    0
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      658

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Filter|Reverb"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "filter", "reverb", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumf

#endif
