/*
 * machine_fw.h: the interface pump MACHINE calls (see machine_fw.c).
 * Copyright (C) 2026 the pump and Thunder Plus authors. SPDX-License-Identifier: GPL-3.0-only (see firmware/NOTICE.md)
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int mfw_block(void);                 /* frames per block (64) */
float mfw_rate(void);                /* the machine's sample rate (44140 Hz) */
int mfw_num_params(void);
uint8_t* mfw_params(void);           /* the kit's parameter bytes */
int mfw_num_mod_targets(void);
int mfw_mod_target_param(int i);
void mfw_init(uint32_t seed);        /* power on */
void mfw_init_kit(void);             /* the firmware's init kit into the parameter bytes */
int mfw_init_kit_value(int p);
void mfw_load(void);                 /* send every parameter byte to the voices, as loading a kit does */
void mfw_set(int p, int value);      /* one parameter */
void mfw_trigger(int voice, int vel, int note);   /* 0-2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat; 60 = own pitch */
void mfw_render(float* left, float* right);       /* one block */
void mfw_set_bpm(int bpm);
size_t mfw_state_size(void);
void mfw_state_save(void* dst);
void mfw_state_load(const void* src);

#ifdef __cplusplus
}
#endif
