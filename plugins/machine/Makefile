#!/usr/bin/make -f
# pump MACHINE: GPL-3.0-only (NOTICE.md, fw/firmware/NOTICE.md). The firmware is C: compiled as
# on the machine (ARM's char is unsigned), its half-float FX buffer as _Float16 where the compiler lacks __fp16 (GCC).
NAME = pump-machine
FILES_DSP = PluginMachine.cpp pffft.c fw/machine_unity.c fw/machine_unity2.c
FILES_UI = UIMachine.cpp fonts.cpp
include ../Plugin.mk
BUILD_C_FLAGS += -std=gnu11 -funsigned-char -w -Ifw/stub -Ifw/firmware/Core/Inc -Ifw
ifeq ($(shell $(CC) --version 2>/dev/null | grep -ci clang),0)
BUILD_C_FLAGS += -D__fp16=_Float16
endif
