/*
 * machine_fw.c: the drum machine firmware's sound engine as a library for pump MACHINE.
 *
 * Copyright (C) 2026 the pump and Thunder Plus authors. Built on the LXR firmware, Copyright (C) 2012-2026 Erica Synths.
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is a modified version of parts of the LXR firmware (it reproduces what the firmware does when a kit is
 * loaded, a parameter is turned, a voice is triggered and a block of samples is mixed), and is licensed under the GNU
 * General Public License version 3, with the additional permission and terms in firmware/NOTICE.md. It is not
 * official Erica Synths firmware or software. It grew from Thunder Plus's lxr_wasm.c.
 *
 * The firmware's own voice, modulation, parameter, mixer and FX code is compiled unchanged. This file only:
 *   - provides what the rest of the firmware would have (the kit edit buffer, the hardware random number generator,
 *     the voice faders, the tempo, the menu page),
 *   - loads a kit the way preset_sendDrumsetParametersImmediate() does, and one parameter the same way,
 *   - triggers a voice the way voiceControl_noteOn() does,
 *   - renders a block through mixer_calcNextSampleBlock() (all six voices, the panning, the FX bus), at the machine's
 *     own rate (REAL_FS, 44.14 kHz), both stereo pairs summed,
 *   - and lists every variable the engine changes, so a host program can keep one copy per instance (the firmware
 *     is written for one machine: its state is global).
 */
#include <stdint.h>
#include <string.h>
#include "stm32f7xx_hal.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "Soundengine/config.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/ResonantFilter.h"
#include "Soundengine/DSPAudio/FX/FxRamBuffer.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/ParameterArray.h"
#include "frontPanelParser_ui.h"   /* CC_2 */
#include "machine_fw.h"

void parameterArray_init(void);                                       /* MIDI/ParameterArray.c */
void mixer_calcNextSampleBlock(int16_t* output, int16_t* output2);    /* DSPAudio/mixer.c */

/* --- what the rest of the firmware would have provided ------------------------------------------------- */
uint8_t activeParameters[NUM_PARAMS];   /* menu.c: the kit being edited */
RNG_HandleTypeDef hrng;
LxrDwt lxr_dwt;
GPIO_TypeDef lxr_gpio;
static uint32_t rng_state = 0x2545F491u;
/* The machine's noise comes from the STM32's hardware random number generator: here a xorshift. */
uint32_t lxr_rng_next(void){ uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }
/* The voice volume faders (IO/adcPots.c) sit at the top. */
float adc_getFaderVolume(uint8_t idx){ (void)idx; return 1.f; }
/* The tempo that tempo-synced LFOs and the delay follow (Sequencer/sequencer.c): the host's. */
static uint16_t lxr_bpm = 120;
uint16_t seq_getBpm(void){ return lxr_bpm; }
void mfw_set_bpm(int bpm){ lxr_bpm = (uint16_t)(bpm < 20 ? 20 : bpm > 400 ? 400 : bpm); }
/* Track mutes and the menu page the FX bus shows: the sequencer and the screen, not the sound. */
void seq_setMute(uint8_t trackNr, uint8_t isMuted){ (void)trackNr; (void)isMuted; }
uint8_t menu_activePage = 0;   /* never the sequencer page here, so a note always plays */
uint8_t menu_getSubPage(void){ return 0; }
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle){ (void)subPageNr; (void)toggle; }

/* --- the sequencer, the front panel and the MIDI outputs: not part of the sound ------------------------------ */
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/clockSync.h"
#include "Soundengine/USB/usb_manager.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
uint8_t frontParser_activeTrack = 0;
PatternSet seq_patternSet;
volatile uint32_t systick_ticks = 0;
uint8_t seq_isTrackMuted(uint8_t trackNr){ (void)trackNr; return 0; }
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel){ (void)voice; (void)note; (void)vel; }
void seq_addNote(uint8_t trackNr, uint8_t vel, uint8_t note){ (void)trackNr; (void)vel; (void)note; }
void seq_midiNoteOff(uint8_t chan){ (void)chan; }
uint8_t menu_getActiveVoice(){ return 0; }
uint8_t menu_getViewedPattern(){ return 0; }
void menu_repaint(){}
uint8_t seq_isRunning(){ return 0; }
uint8_t seq_getExtSync(){ return 0; }
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value){ (void)voice; (void)dest; (void)value; }
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc){ (void)channel; (void)note; (void)veloc; }
void seq_setNextPattern(const uint8_t patNr){ (void)patNr; }
void seq_setRunning(uint8_t isRunning){ (void)isRunning; }
void seq_sync(){}
void sync_midiResetPosition(){}
void sync_midiStartStop(uint8_t isStart){ (void)isStart; }
void uart_sendFrontpanelByte(uint8_t data){ (void)data; }
void uart_sendMidi(MidiMsg msg){ (void)msg; }
void usb_sendMidi(MidiMsg msg){ (void)msg; }

extern const ModTarg modTargets[];                        /* menu/Cc2Text.c */
extern const uint8_t initKit[];                           /* Preset/initKit.c */
uint8_t getNumModTargets(void);                           /* menu/Cc2Text.c */
/* defined in the firmware without a header declaration */
extern uint8_t l1_Available, l2_Available, r1_Available, r2_Available;
extern float mixer_decimation_cnt[6];
extern int16_t mixer_voice_samples[6];
extern uint8_t midiParser_selectedLfoVoice[6];
extern MidiMsg midiMsg_tmp;

_Static_assert(OUTPUT_DMA_SIZE <= 64, "MachineEngine.hpp renders into 64-frame buffers (kMaxFwBlock)");
int mfw_block(void){ return OUTPUT_DMA_SIZE; }
float mfw_rate(void){ return REAL_FS; }
int mfw_num_params(void){ return END_OF_SOUND_PARAMETERS; }
uint8_t* mfw_params(void){ return activeParameters; }
int mfw_num_mod_targets(void){ return getNumModTargets(); }
int mfw_mod_target_param(int i){ return (i >= 0 && i < getNumModTargets()) ? modTargets[i].param : -1; }

/* The firmware's init kit: 8 name bytes, then the parameters from number 0 (preset_init()). */
void mfw_init_kit(void){
  memset(activeParameters, 0, sizeof(activeParameters));
  for (int i = 8; i < END_OF_SOUND_PARAMETERS; i++) activeParameters[i - 8] = initKit[i];
}
int mfw_init_kit_value(int p){ return (p >= 0 && p + 8 < END_OF_SOUND_PARAMETERS) ? initKit[p + 8] : 0; }

/* Power-on state (soundengine_init() without the codec, sequencer and MIDI). */
void mfw_init(uint32_t seed){
  rng_state = seed ? seed : 0x2545F491u;
  mixer_init();
  parameterArray_init();
  for (int i = 0; i < 6; i++) modNode_init(&velocityModulators[i]);
  initDrumVoice();
  Snare_init();
  HiHat_init();
  Cymbal_init();
  FxBus_init();
}

/* The velocity and LFO destinations of voice i (0-5): an index into modTargets becomes its parameter
   (FRONT_CC_VELO_TARGET and FRONT_CC_LFO_TARGET in CortexParser.c). */
static void mfw_load_targets(int i){
  const uint8_t nmt = getNumModTargets();
  if (activeParameters[PAR_VEL_DEST_1 + i] >= nmt) activeParameters[PAR_VEL_DEST_1 + i] = 0;   /* preset_readDrumsetData() */
  if (activeParameters[PAR_TARGET_LFO1 + i] >= nmt) activeParameters[PAR_TARGET_LFO1 + i] = 0;
  if (activeParameters[PAR_VOICE_LFO1 + i] < 1 || activeParameters[PAR_VOICE_LFO1 + i] > 6) activeParameters[PAR_VOICE_LFO1 + i] = 1;
  const uint8_t velo = (uint8_t)modTargets[activeParameters[PAR_VEL_DEST_1 + i]].param;
  modNode_setDestination(&velocityModulators[i], velo);
  const uint8_t lfoT = (uint8_t)modTargets[activeParameters[PAR_TARGET_LFO1 + i]].param;
  switch (i){
    case 0: case 1: case 2: modNode_setDestination(&voiceArray[i].lfo.modTarget, lfoT); break;
    case 3: modNode_setDestination(&snareVoice.lfo.modTarget, lfoT); break;
    case 4: modNode_setDestination(&cymbalVoice.lfo.modTarget, lfoT); break;
    default: modNode_setDestination(&hatVoice.lfo.modTarget, lfoT); break;
  }
}
static void mfw_send(uint8_t p){
  if (p == 0) return;     /* PAR_NONE: nothing to set (and the "off" modulation destination: the firmware would read through its null value pointer) */
  if (p == 127) return;   /* PAR_BANK_CHANGE: not a sound; as CC 0 the parser would store 64 KB past the end of its CC table */
  MidiMsg msg = {0};
  if (p < 128){ msg.status = MIDI_CC; msg.data1 = (uint8_t)((p + 1) & 0x7f); }
  else { msg.status = CC_2; msg.data1 = (uint8_t)((p - 128) & 0x7f); }
  msg.data2 = activeParameters[p];
  midiParser_ccHandler(msg, 1, 0, 0.f);
}

/* preset_sendDrumsetParametersImmediate() for the bytes in activeParameters. */
void mfw_load(void){
  for (int i = 0; i < 6; i++) mfw_load_targets(i);
  for (uint32_t i = 0; i < END_OF_SOUND_PARAMETERS; i++) mfw_send(parameterOrder[i]);
}

/* One parameter turned (what the front panel does): the targets go through the mod table, the rest as a CC. */
void mfw_set(int p, int value){
  if (p <= 0 || p >= END_OF_SOUND_PARAMETERS) return;
  activeParameters[p] = (uint8_t)value;
  for (int i = 0; i < 6; i++)
    if (p == PAR_VEL_DEST_1 + i || p == PAR_TARGET_LFO1 + i || p == PAR_VOICE_LFO1 + i){ mfw_load_targets(i); if (p != PAR_VOICE_LFO1 + i) return; }
  mfw_send((uint8_t)p);
}

/* voiceControl_noteOn(): voices 0-2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat. Note 60 is the voice's
   own pitch. */
void mfw_trigger(int voice, int vel, int note){
  if (vel < 1) vel = 1;
  if (vel > 127) vel = 127;
  if (voice < 3) Drum_trigger((uint8_t)voice, (uint8_t)vel, (uint8_t)note);
  else if (voice < 4) Snare_trigger((uint8_t)vel, (uint8_t)note);
  else if (voice < 5) Cymbal_trigger((uint8_t)vel, (uint8_t)note);
  else HiHat_trigger((uint8_t)vel, (uint8_t)(voice - 5), (uint8_t)note);
}

/* One block (mfw_block() frames) of the whole machine, both stereo pairs summed, as floats. */
void mfw_render(float* left, float* right){
  int16_t a[OUTPUT_DMA_SIZE * 2], b[OUTPUT_DMA_SIZE * 2];
  mixer_calcNextSampleBlock(a, b);
  for (int i = 0; i < OUTPUT_DMA_SIZE; i++){
    left[i] = (a[2 * i] + b[2 * i]) * (1.f / 32768.f);
    right[i] = (a[2 * i + 1] + b[2 * i + 1]) * (1.f / 32768.f);
  }
}

/* Every variable the sound engine changes (and the FX bus's half-float buffer last, the largest). */
#define S(x) { (void*)&(x), sizeof(x) }
static const struct { void* p; size_t n; } mfw_vars[] = {
  S(activeParameters), S(hrng), S(lxr_dwt), S(lxr_gpio), S(rng_state), S(lxr_bpm), S(menu_activePage),
  S(voiceArray), S(snareVoice), S(hatVoice), S(cymbalVoice), S(velocityModulators), S(parameterArray), S(parameterOrder),
  S(paramToModTarget), S(midiParser_originalCcValues), S(midi_MidiChannels), S(midiParser_txRxFilter), S(midiParser_selectedLfoVoice),
  S(midiMsg_tmp), S(mixer_audioRouting), S(mixer_decimation_rate), S(mixer_decimation_cnt), S(mixer_voice_samples),
  S(l1_Available), S(l2_Available), S(r1_Available), S(r2_Available), S(fxBus), S(FX_BUFFER),
};
size_t mfw_state_size(void){ size_t n = 0; for (size_t i = 0; i < sizeof(mfw_vars) / sizeof(mfw_vars[0]); i++) n += mfw_vars[i].n; return n; }
void mfw_state_save(void* dst){ uint8_t* d = (uint8_t*)dst; for (size_t i = 0; i < sizeof(mfw_vars) / sizeof(mfw_vars[0]); i++){ memcpy(d, mfw_vars[i].p, mfw_vars[i].n); d += mfw_vars[i].n; } }
void mfw_state_load(const void* src){ const uint8_t* s = (const uint8_t*)src; for (size_t i = 0; i < sizeof(mfw_vars) / sizeof(mfw_vars[0]); i++){ memcpy(mfw_vars[i].p, s, mfw_vars[i].n); s += mfw_vars[i].n; } }
