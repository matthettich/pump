#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake instruments"
#define DISTRHO_PLUGIN_NAME            "pump FOLD West Coast Synth"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "West Coast Synth"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "A west-coast-style synth: a complex oscillator (FM, AM and folding from its modulation oscillator), a multi-stage wavefolder with symmetry and edge, and a low-pass gate that rings when struck; a function generator that strikes, sustains or cycles, and a random source."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#fold"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.fold"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmFD

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_IS_SYNTH        1
#define DISTRHO_PLUGIN_NUM_INPUTS      0
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    0
#define DISTRHO_PLUGIN_WANT_LATENCY    0
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_FILE_BROWSER        0
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      756

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumu

#endif
