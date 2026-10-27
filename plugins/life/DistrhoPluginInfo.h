#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake instruments"
#define DISTRHO_PLUGIN_NAME            "pump LIFE Groovebox"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Groovebox"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "A groovebox played by a Game of Life: eight FM / ring / kick tone voices, a pitch-shifting delay with a filter in its loop, and a grid of cells whose births play the voice of their colour, in loops that follow the song or patterns that evolve."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#life"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.life"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmLF

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_IS_SYNTH        1
#define DISTRHO_PLUGIN_NUM_INPUTS      0
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_LATENCY    0
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      800

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Drum|Synth"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "drum-machine", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumu

#endif
