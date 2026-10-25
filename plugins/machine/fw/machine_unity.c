/*
 * machine_unity.c: the drum machine firmware's sound engine and machine_fw.c as one translation unit.
 * Copyright (C) 2026 the pump authors. SPDX-License-Identifier: GPL-3.0-only (see firmware/NOTICE.md)
 *
 * The firmware files are compiled unchanged. On the machine some code and data live in its tightly coupled
 * memories (__attribute__((section(".itcm_text"))) and the like); a desktop build has no such memories and macOS
 * rejects those section names, so here every section attribute becomes a harmless "unused". The system headers are
 * read first, so the macro cannot reach them.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#define section(x) unused
#include "firmware/Core/Src/Soundengine/DSPAudio/1PoleLp.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/BufferTools.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/CymbalVoice.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/Decay.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/DrumVoice.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/HiHat.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/Oscillator.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/ResonantFilter.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/Samples.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/SlopeEg2.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/Snare.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/TiltEq.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/automationNode.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/distortion.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/dither.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/lfo.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/mixer.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/modulationNode.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/random.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/snapEg.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/squareRootLut.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/transientGenerator.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/transientTables.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/wavetable.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/Compressor.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/Delay.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/Distortion.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/FxRamBuffer.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/Reverb.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/Ringmod.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/VerbTools/AllpassDelay.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/VerbTools/VerbDelay.c"
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/VerbTools/sinCos.c"
#include "firmware/Core/Src/Soundengine/MIDI/MidiParser.c"
#include "firmware/Core/Src/Soundengine/MIDI/ParameterArray.c"
#include "firmware/Core/Src/Soundengine/FxBus.c"
#include "firmware/Core/Src/Parameters.c"
#include "firmware/Core/Src/menu/Cc2Text.c"
#include "firmware/Core/Src/Preset/initKit.c"
#include "machine_fw.c"
