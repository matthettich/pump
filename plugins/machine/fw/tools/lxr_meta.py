#!/usr/bin/env python3
"""Writes lxr-meta.json: the LXR-02's own sound pages, parameter names, value types, option lists, modulation
targets and init kit, read from the firmware's menu tables, for the Thunder Plus engine's controls.

It compiles a small host program against the firmware's headers (menu/menuPages.h, menu/MenuText.h,
Parameters.h, the valueNames and parameter_dtypes tables copied out of menu/menu.c, and menu/Cc2Text.c),
so every enum value comes from the compiler rather than from parsing.

GPL-3.0-only, like the firmware it reads.
Usage: python3 lxr_meta.py <firmware Core folder> <stub include folder> <out.json>
"""
import json, os, re, subprocess, sys, tempfile

core, stub, out = sys.argv[1:4]
menu_c = open(os.path.join(core, 'Src/menu/menu.c'), encoding='utf-8', errors='replace').read()

def table(name):
    """The source of a const table in menu.c, from its declaration to the closing '};'."""
    m = re.search(r'^const [^\n]*\b' + name + r'\s*\[[^\n]*=', menu_c, re.M)
    if not m: sys.exit('no table ' + name)
    end = menu_c.index('\n};', m.start())
    return menu_c[m.start():end + 3] + '\n'

params_h = open(os.path.join(core, 'Inc/Parameters.h'), encoding='utf-8', errors='replace').read()
body = params_h[params_h.index('enum ParamEnums'):]
body = body[:body.index('};')]
body = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
par_names = []
for tok in re.findall(r'\b(PAR_[A-Z0-9_]+|END_OF_SOUND_PARAMETERS|NUM_PARAMS)\b', body):
    if tok not in par_names: par_names.append(tok)

src = r'''
#include <stdio.h>
#include "stm32f7xx_hal.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "menu/MenuText.h"
#include "menu/menuPages.h"
#include "menu/CcNr2Text.h"
#include "Preset/InitKit.h"
uint32_t lxr_rng_next(void){ return 0; }
LxrDwt lxr_dwt; GPIO_TypeDef lxr_gpio;
''' + table('valueNames') + table('parameter_dtypes') + r'''
extern const ModTarg modTargets[];
extern const ModTargetVoiceOffset modTargetVoiceOffsets[6];
uint8_t getNumModTargets();
static void str(const char* s, int n){ putchar('"'); for (int i = 0; i < n && s[i]; i++){ char c = s[i]; if (c == '"' || c == '\\') putchar('\\'); if ((unsigned char)c >= 32 && (unsigned char)c < 127) putchar(c); } putchar('"'); }
#define LIST(nm, arr, w) printf("\"%d\":[", nm); for (int i = 1; i <= arr[0][0]; i++){ if (i > 1) putchar(','); str(arr[i], w); } printf("]");
int main(void){
  printf("{\"nparams\":%d,\"rate\":44140,", END_OF_SOUND_PARAMETERS);
  printf("\"par\":{");
'''
for i, nm in enumerate(par_names):
    src += '  printf("%s\\"%s\\":%%d", (int)(%s));\n' % ('' if i == 0 else ',', nm, nm)
src += r'''  printf("},");
  printf("\"init\":["); for (int i = 8; i < END_OF_SOUND_PARAMETERS; i++) printf("%s%d", i > 8 ? "," : "", initKit[i]); printf("],");
  printf("\"pages\":[");
  for (int p = 0; p < 7; p++){ printf("%s[", p ? "," : "");
    for (int s = 0; s < NUM_SUB_PAGES; s++){ const Page* g = &menuPages[p][s]; const uint8_t* t = &g->top1; const uint16_t* b = &g->bot1;
      printf("%s[", s ? "," : ""); for (int k = 0; k < 8; k++) printf("%s[%d,%d]", k ? "," : "", t[k], b[k]); printf("]"); }
    printf("]"); }
  printf("],\"names\":[");
  for (int i = 0; i < NUM_NAMES; i++){ printf("%s[", i ? "," : ""); str(shortNames[valueNames[i].shortName], 4); putchar(','); str(catNames[valueNames[i].category], 16); putchar(','); str(longNames[valueNames[i].longName], 16); printf("]"); }
  printf("],\"dtypes\":[");
  for (int i = 0; i < END_OF_SOUND_PARAMETERS; i++) printf("%s[%d,%d]", i ? "," : "", parameter_dtypes[i] & 0xff, (parameter_dtypes[i] >> 8) & 0xff);
  printf("],\"menus\":{");
  LIST(MENU_FILTER, filterTypes, 8) putchar(','); LIST(MENU_WAVEFORM, waveformNames, 4) putchar(','); LIST(MENU_SYNC_RATES, syncRateNames, 4) putchar(',');
  LIST(MENU_LFO_WAVES, lfoWaveNames, 4) putchar(','); LIST(MENU_RETRIGGER, retriggerNames, 4) putchar(','); LIST(MENU_TRANS, transientNames, 4) putchar(',');
  LIST(MENU_MIX_FM, mixFmNames, 6) putchar(','); LIST(MENU_AUDIO_OUT, outputNames, 7)
  printf("},\"dtypeIds\":{\"0B127\":%d,\"PM63\":%d,\"MENU\":%d,\"ON_OFF\":%d,\"LFO_TARGET\":%d,\"VELO_TARGET\":%d,\"VOICE_LFO\":%d,\"0B255\":%d,\"1B16\":%d,\"PM100\":%d,\"0B100\":%d,\"0B15\":%d,\"1B64\":%d,\"NOTE\":%d,\"0b1\":%d},",
    DTYPE_0B127, DTYPE_PM63, DTYPE_MENU, DTYPE_ON_OFF, DTYPE_TARGET_SELECTION_LFO, DTYPE_TARGET_SELECTION_VELO, DTYPE_VOICE_LFO, DTYPE_0B255, DTYPE_1B16, DTYPE_PM100, DTYPE_0B100, DTYPE_0B15, DTYPE_1B64, DTYPE_NOTE_NAME, DTYPE_0b1);
  printf("\"modTargets\":["); for (int i = 0; i < getNumModTargets(); i++) printf("%s[%d,%d]", i ? "," : "", modTargets[i].nameIdx, modTargets[i].param);
  printf("],\"modVoices\":["); for (int v = 0; v < 6; v++) printf("%s[%d,%d]", v ? "," : "", modTargetVoiceOffsets[v].start, modTargetVoiceOffsets[v].end);
  printf("]}\n");
  return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = os.path.join(d, 'meta.c'); open(c, 'w').write(src)
    exe = os.path.join(d, 'meta')
    cmd = ['clang', '-w', '-std=gnu11', '-I' + stub, '-I' + os.path.join(core, 'Inc'), c,
           os.path.join(core, 'Src/menu/Cc2Text.c'), os.path.join(core, 'Src/Preset/initKit.c'),
           '-Wl,--unresolved-symbols=ignore-all', '-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit(r.stderr[-4000:])
    js = subprocess.run([exe], capture_output=True, text=True, check=True).stdout
json.loads(js)
open(out, 'w').write(js)
print('wrote', out, len(js), 'bytes')
