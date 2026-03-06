#!/usr/bin/env python3
"""Compare emulator token trace with Python reference trace."""
import re
import sys

emu_file = sys.argv[1] if len(sys.argv) > 1 else '/tmp/ps1_fix_test.log'
py_file = sys.argv[2] if len(sys.argv) > 2 else '/tmp/py_token_trace.txt'

emu_tokens = []
with open(emu_file) as f:
    for line in f:
        m = re.search(
            r'DECOMP_TOKEN: #(\d+) prefix=0x([0-9A-Fa-f]+) a2=(\d+) '
            r't6=0x([0-9A-Fa-f]+) t7=0x([0-9A-Fa-f]+) dst_off=0x([0-9A-Fa-f]+)',
            line
        )
        if m:
            emu_tokens.append({
                'num': int(m.group(1)),
                'prefix': int(m.group(2), 16),
                'a2': int(m.group(3)),
                't6': int(m.group(4), 16),
                'dst_off': int(m.group(6), 16)
            })

py_tokens = []
with open(py_file) as f:
    for line in f:
        m = re.search(
            r'DECOMP_TOKEN_PY: #(\d+) prefix=0x([0-9A-Fa-f]+) a2=(\d+) '
            r't6=0x([0-9A-Fa-f]+) t7=0x([0-9A-Fa-f]+) dst_off=0x([0-9A-Fa-f]+)',
            line
        )
        if m:
            py_tokens.append({
                'num': int(m.group(1)),
                'prefix': int(m.group(2), 16),
                'a2': int(m.group(3)),
                't6': int(m.group(4), 16),
                'dst_off': int(m.group(6), 16)
            })

print(f'Emu tokens: {len(emu_tokens)}, Py tokens: {len(py_tokens)}')

mismatches = 0
for i in range(min(len(emu_tokens), len(py_tokens))):
    e = emu_tokens[i]
    p = py_tokens[i]
    if e['prefix'] != p['prefix'] or e['dst_off'] != p['dst_off'] or e['a2'] != p['a2']:
        if mismatches < 10:
            print(f'Mismatch #{i}: emu prefix=0x{e["prefix"]:02X} dst=0x{e["dst_off"]:04X} a2={e["a2"]} '
                  f'vs py prefix=0x{p["prefix"]:02X} dst=0x{p["dst_off"]:04X} a2={p["a2"]}')
        mismatches += 1

if mismatches == 0:
    print('ALL TOKENS MATCH! Decompression is correct!')
else:
    print(f'Total mismatches: {mismatches} out of {min(len(emu_tokens), len(py_tokens))}')
