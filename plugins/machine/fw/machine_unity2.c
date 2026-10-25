/*
 * machine_unity2.c: the one firmware file that cannot share machine_unity.c's translation unit (its static helpers
 * have the same names as another file's).
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
#include "firmware/Core/Src/Soundengine/DSPAudio/FX/VerbTools/VerbModDelay.c"
