#!/usr/bin/env python3
"""Writes plugins/machine/MachineMeta.hpp: pump MACHINE's parameters, read from the firmware's menu tables.

Input: the JSON that lxr_meta.py (next to this file) writes from the firmware's headers and menu tables.
Output: every sound parameter of the six voices (drums 1-3, snare, cymbal, hi-hat) on the machine's own pages, with
its label, range, default (the firmware's init kit) and value names; the modulation targets; the sections.

Copyright (C) 2026 the pump and Thunder Plus authors. GPL-3.0-only, like the firmware it reads (see ../firmware/NOTICE.md).
The labels follow Thunder Plus's lxr-engine.src.js.
Usage: python3 lxr_meta.py <fw>/firmware/Core <fw>/stub meta.json && python3 machine_meta.py meta.json MachineMeta.hpp
"""
import json, re, sys

meta = json.load(open(sys.argv[1]))
out = sys.argv[2]
PAR, NP = meta['par'], meta['nparams']
NAME = {}
for k, v in PAR.items():
    if v < NP: NAME[v] = k[4:]   # a later alias wins (OSC_WAVE_DRUM1 over MOD_WHEEL)
DT = meta['dtypeIds']
INIT = meta['init']

LABELS = [
    (r'^COARSE\d$', 'Coarse'), (r'^FINE\d$', 'Fine'), (r'^(OSC_WAVE_|WAVE1_)', 'Waveform'), (r'^PWM_\d$', 'Pulse width'),
    (r'^VELOA\d$', 'Attack'), (r'^VELOD6_CLOSED$', 'Decay (closed)'), (r'^VELOD6_OPEN$', 'Decay (open)'), (r'^VELOD\d$', 'Decay'),
    (r'^VOL_SLOPE\d$', 'Slope'), (r'^REPEAT\d$', 'Repeats'),
    (r'^MOD_EG\d$', 'Pitch decay'), (r'^PITCH_SLOPE\d$', 'Pitch slope'), (r'^MODAMNT\d$', 'Pitch amount'),
    (r'^VEL_DEST_\d$', 'Velocity to'), (r'^VELO_MOD_AMT_\d$', 'Velocity amount'), (r'^VOLUME_MOD_ON_OFF\d$', 'Velocity to volume'),
    (r'^FMAMNT\d$', 'FM amount'), (r'^FM_FREQ\d$', 'FM frequency'), (r'^MOD_WAVE_DRUM\d$', 'FM waveform'), (r'^MIX_MOD_\d$', 'FM mode'),
    (r'^MOD_OSC_F1', 'Mod osc 1 freq'), (r'^MOD_OSC_F2', 'Mod osc 2 freq'), (r'^MOD_OSC_GAIN1', 'Mod osc 1 level'), (r'^MOD_OSC_GAIN2', 'Mod osc 2 level'),
    (r'^WAVE2_', 'Mod osc 1 wave'), (r'^WAVE3_', 'Mod osc 2 wave'), (r'^NOISE_FREQ1$', 'Noise frequency'), (r'^MIX1$', 'Noise / osc mix'),
    (r'^TRANS\d_WAVE$', 'Transient'), (r'^TRANS\d_VOL$', 'Transient level'), (r'^TRANS\d_FREQ$', 'Transient pitch'),
    (r'^FILTER_FREQ_\d$', 'Cutoff'), (r'^RESO_\d$', 'Resonance'), (r'^FILTER_TYPE_\d$', 'Filter type'), (r'^FILTER_DRIVE_\d$', 'Filter drive'),
    (r'^FREQ_LFO\d$', 'LFO rate'), (r'^SYNC_LFO\d$', 'LFO sync'), (r'^AMOUNT_LFO\d$', 'LFO amount'), (r'^WAVE_LFO\d$', 'LFO wave'),
    (r'^RETRIGGER_LFO\d$', 'LFO retrigger'), (r'^OFFSET_LFO\d$', 'LFO offset'), (r'^TARGET_LFO\d$', 'LFO to'), (r'^VOICE_LFO\d$', 'LFO voice'),
    (r'^VOL\d$', 'Volume'), (r'^PAN\d$', 'Pan'), (r'^VOICE_DECIMATION\d$', 'Sample rate'), (r'^VOICE_DECIMATION_ALL$', 'Sample rate (all)'),
    (r'^(DRIVE\d|SNARE_DISTORTION|CYMBAL_DISTORTION|HAT_DISTORTION)$', 'Drive')]
def label(n):
    nm = NAME.get(n, '')
    for rx, l in LABELS:
        if re.search(rx, nm): return l
    return nm.lower().replace('_', ' ')
# what a dial shows (its section says FM, Filter, LFO...)
DIAL = {'Pulse width': 'PW', 'Decay (closed)': 'CL DECAY', 'Decay (open)': 'OP DECAY', 'Pitch decay': 'DECAY', 'Pitch slope': 'SLOPE',
        'Pitch amount': 'AMOUNT', 'Velocity to': 'VEL TO', 'Velocity amount': 'VEL AMT', 'Velocity to volume': 'VEL VOL', 'FM amount': 'AMOUNT',
        'FM frequency': 'FREQ', 'FM waveform': 'WAVE', 'FM mode': 'MODE', 'Mod osc 1 freq': 'FREQ 1', 'Mod osc 2 freq': 'FREQ 2',
        'Mod osc 1 level': 'LEVEL 1', 'Mod osc 2 level': 'LEVEL 2', 'Mod osc 1 wave': 'WAVE 1', 'Mod osc 2 wave': 'WAVE 2',
        'Noise frequency': 'NOISE F', 'Noise / osc mix': 'NOISE', 'Transient': 'SAMPLE', 'Transient level': 'LEVEL', 'Transient pitch': 'PITCH',
        'Filter type': 'TYPE', 'Filter drive': 'DRIVE', 'Resonance': 'RES', 'LFO rate': 'RATE', 'LFO sync': 'SYNC', 'LFO amount': 'AMOUNT',
        'LFO wave': 'WAVE', 'LFO retrigger': 'RETRIG', 'LFO offset': 'OFFSET', 'LFO to': 'TO', 'LFO voice': 'VOICE', 'Sample rate': 'RATE',
        'Waveform': 'WAVE', 'Repeats': 'REPEATS'}
NICE = {'Sin': 'Sine', 'Rec': 'Square', 'Noi': 'Noise', 'Nch': 'Notch', 'Pek': 'Peak', 'UBP': 'BP (unity)', 'LP2': 'LP 2', 'off': 'Off',
        'sin': 'Sine', 'tri': 'Triangle', 'sup': 'Saw up', 'sdn': 'Saw down', 'sqr': 'Square', 'rnd': 'Random', 'xup': 'Exp up', 'xdn': 'Exp down',
        'Snp': 'Snap', 'Ofs': 'Offset', 'Clk': 'Click', 'Ck2': 'Click 2', 'Tik': 'Tick', 'Kik': 'Kick', 'Drp': 'Drop', 'Clp': 'Clap',
        'Kk2': 'Kick 2', 'Snr': 'Snare', 'Sp2': 'Snap 2', '12': '1/12', '16': '1/16', '32': '1/32', 'v1': 'Voice 1', 'v2': 'Voice 2',
        'v3': 'Voice 3', 'v4': 'Voice 4', 'v5': 'Voice 5', 'v6': 'Voice 6'}
PM60 = re.compile(r'^(COARSE\d|FM_FREQ\d|MOD_OSC_F[12](_CYM)?)$')
SKIP = re.compile(r'^(AUDIO_OUT\d)$')

VOICES = ['Drum 1', 'Drum 2', 'Drum 3', 'Snare', 'Cymbal', 'Hat']
VSHORT = ['D1', 'D2', 'D3', 'SN', 'CY', 'HH']
SECTIONS = ['OSCILLATOR', 'ENVELOPE', 'PITCH · VELOCITY', 'FM', 'TRANSIENT', 'FILTER', 'LFO', 'MIX']

def cstr(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'
def init(n): return INIT[n] if n < len(INIT) else 0

menus_used = {}
rows = []        # (fw, sym, name, dial, voice, section, min, max, def, kind, menu)
seen = set()
pages = meta['pages']
for v in range(6):
    plist = [list(sub) for sub in pages[v]]
    if v == 5:   # the open hat's page adds only its decay
        op = PAR['PAR_VELOD6_OPEN']
        for i, (nm, p) in enumerate(plist[1]):
            if p == PAR['PAR_VELOD6_CLOSED']: plist[1].insert(i + 1, [0, op]); break
    for s, sub in enumerate(plist):
        for _, n in sub:
            if n <= 0 or n >= NP or n in seen: continue
            nm = NAME.get(n, '')
            if SKIP.search(nm): continue
            seen.add(n)
            dt, menu = meta['dtypes'][n]
            l = label(n)
            d = init(n)
            if dt == DT['MENU']:
                opts = meta['menus'].get(str(menu), [])
                menus_used[menu] = [NICE.get(o, o) for o in opts]
                kind, lo, hi, d = 'kMenu', 0, max(0, len(opts) - 1), min(d, max(0, len(opts) - 1))
            elif dt == DT['PM63']: kind, lo, hi = ('kPm60' if PM60.search(nm) else 'kPm63'), 0, 127
            elif dt == DT['ON_OFF']: kind, lo, hi, d = 'kOnOff', 0, 1, 1 if d else 0
            elif dt in (DT['LFO_TARGET'], DT['VELO_TARGET']): kind, lo, hi = 'kTarget', 0, len(meta['modTargets']) - 1
            elif dt == DT['VOICE_LFO']: kind, lo, hi, d = 'kVoiceLfo', 1, 6, min(6, max(1, d or (v + 1)))
            else: kind, lo, hi = 'kInt', 0, 127
            sec = 3 if s == 3 else s
            rows.append((n, nm.lower(), VOICES[v] + ' ' + l, DIAL.get(l, l.upper()), v, sec, lo, hi, d, kind, menu if kind == 'kMenu' else -1))

# modulation target names: the voice the target belongs to, then its label
tnames = []
ranges = meta['modVoices']
for g, t in enumerate(meta['modTargets']):
    n = t[1]
    if g == 0 or n == 0: tnames.append('Off'); continue
    v = next((i for i, (a, b) in enumerate(ranges) if a <= g <= b), -1)
    tnames.append((VSHORT[v] + ' ' if v >= 0 else '') + label(n))

L = []
L.append('// pump MACHINE: the firmware\'s sound parameters. GENERATED by fw/tools/machine_meta.py from the firmware\'s menu tables:')
L.append('// do not edit. GPL-3.0-only (see fw/firmware/NOTICE.md).')
L.append('#pragma once')
L.append('')
L.append('namespace pump { namespace machine {')
L.append('')
L.append('enum FwKind : int { kInt = 0, kPm60, kPm63, kOnOff, kMenu, kTarget, kVoiceLfo };')
L.append('struct FwParam { int fw; const char* sym; const char* name; const char* dial; int voice, section, min, max, def, kind; const char* const* choices; };')
L.append('constexpr int kFwNumParams = %d, kNumVoices = 6, kNumSections = 8, kNumModTargets = %d;' % (NP, len(meta['modTargets'])))
L.append('static const char* const kVoiceNames[kNumVoices] = { %s };' % ', '.join(cstr(x.upper()) for x in VOICES))
L.append('static const char* const kSectionNames[kNumSections] = { %s };' % ', '.join(cstr(x) for x in SECTIONS))
L.append('static const char* const kVoiceLfoNames[] = { %s };' % ', '.join(cstr(x) for x in VOICES))
for m, opts in sorted(menus_used.items()):
    L.append('static const char* const kMenu%d[] = { %s };' % (m, ', '.join(cstr(o) for o in opts)))
L.append('static const char* const kTargetNames[kNumModTargets] = {')
for i in range(0, len(tnames), 8): L.append('    ' + ', '.join(cstr(x) for x in tnames[i:i + 8]) + ',')
L.append('};')
L.append('static const FwParam kFw[] = {')
for (n, sym, name, dial, v, sec, lo, hi, d, kind, menu) in rows:
    ch = 'kMenu%d' % menu if kind == 'kMenu' else ('kTargetNames' if kind == 'kTarget' else ('kVoiceLfoNames' if kind == 'kVoiceLfo' else 'nullptr'))
    L.append('    { %d, %s, %s, %s, %d, %d, %d, %d, %d, %s, %s },' % (n, cstr(sym), cstr(name), cstr(dial), v, sec, lo, hi, d, kind, ch))
L.append('};')
L.append('constexpr int kNumFw = (int)(sizeof(kFw) / sizeof(kFw[0]));')
# the firmware's init kit for the parameters that are not on the pages (written as the kit loads)
L.append('static const unsigned char kInitKit[kFwNumParams] = {')
vals = [init(n) for n in range(NP)]
for i in range(0, NP, 24): L.append('    ' + ', '.join(str(x) for x in vals[i:i + 24]) + ',')
L.append('};')
L.append('')
L.append('}} // namespace pump::machine')
open(out, 'w').write('\n'.join(L) + '\n')
print('wrote', out, len(rows), 'parameters')
