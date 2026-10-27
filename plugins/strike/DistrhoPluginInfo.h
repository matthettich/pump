#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake instruments"
#define DISTRHO_PLUGIN_NAME            "pump STRIKE Drum Synth"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Drum Synth"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "A drum synth of seven engines: modal membranes, plates, bars and bells with snare wires; waveguide strings; analog bridged-T circuits; three-operator FM; west-coast folds through a low-pass gate; additive cymbals and gongs; and chaos. Eight pads on the General MIDI drum notes."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#strike"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.strike"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmSK

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
#define DISTRHO_UI_DEFAULT_HEIGHT      690

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Drum"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "drum-machine", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumu

#endif
