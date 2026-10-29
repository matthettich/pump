#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake instruments"
#define DISTRHO_PLUGIN_NAME            "pump BREAK Break Chopper"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Break Chopper"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "Break chopper: built-in soul and funk breaks (or your own .wav) in sixteen slices, a sixteen-step sequencer with a pitch and a volume envelope per step, and eight momentary effects."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#break"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.break"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmBK

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_IS_SYNTH        1
#define DISTRHO_PLUGIN_NUM_INPUTS      0
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_LATENCY    0
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1   // the sequencer follows the song
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0   // see PumpPlugin.hpp: host programs reset everything in Live
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_FILE_BROWSER        1   // LOAD a .wav to chop
#define DISTRHO_UI_DEFAULT_WIDTH       1000
#define DISTRHO_UI_DEFAULT_HEIGHT      640

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Sampler"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "sampler", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumu

#endif
