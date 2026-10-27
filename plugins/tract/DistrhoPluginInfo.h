#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND           "wake instruments"
#define DISTRHO_PLUGIN_NAME            "pump TRACT Vocal Tract Pad"   // hosts identify plugins by the ids below, not the name
#define PUMP_PLUGIN_KIND               "Vocal Tract Pad"   // shown next to the tag in the window
#define PUMP_PLUGIN_ABOUT              "A vocal-tract pad: each singer a physical model of a voice (a glottis pulsing air into a 44-section tube from the vocal folds to the lips), the vowel shaped by the tongue and the lips, up to four singers a note, detuned, sized and spread."
#define DISTRHO_PLUGIN_URI             "https://github.com/matthettich/pump#tract"
#define DISTRHO_PLUGIN_CLAP_ID         "com.wake.pump.tract"
#define DISTRHO_PLUGIN_BRAND_ID        Wake
#define DISTRHO_PLUGIN_UNIQUE_ID       pmTR

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
#define DISTRHO_UI_DEFAULT_HEIGHT      738

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE         aumu

#endif
