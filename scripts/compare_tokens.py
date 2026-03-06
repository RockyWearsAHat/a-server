#!/usr/bin/env python3
"""Compare emulator DECOMP_TOKEN trace with Python reference trace to find
the first token where they diverge."""

import re

def parse_emu_trace(logfile):
    """Extract DECOMP_TOKEN entries from emulator log."""
    tokens = []
    pattern = re.compile(
        r'DECOMP_TOKEN: #(\d+) prefix=0x([0-9A-Fa-f]+) a2=(\d+) '
        r't6=0x([0-9A-Fa-f]+) t7=0x([0-9A-Fa-f]+) dst_off=0x([0-9A-Fa-f]+)'
    )
    with open(logfile) as f:
        for line in f:
            m = pattern.search(line)
            if m:
                tokens.append({
                    'num': int(m.group(1)),
                    'prefix': int(m.group(2), 16),
                    'a2': int(m.group(3)),
                    't6': int(m.group(4), 16),
                    't7': int(m.group(5), 16),
                    'dst_off': int(m.group(6), 16),
                })
    return tokens

def parse_py_trace(txtfile):
    """Extract DECOMP_TOKEN_PY entries from Python trace."""
    tokens = []
    pattern = re.compile(
        r'DECOMP_TOKEN_PY: #(\d+) prefix=0x([0-9A-Fa-f]+) a2=(\d+) '
        r't6=0x([0-9A-Fa-f]+) t7=0x([0-9A-Fa-f]+) dst_off=0x([0-9A-Fa-f]+)'
    )
    with open(txtfile) as f:
        for line in f:
            m = pattern.search(line)
            if m:
                tokens.append({
                    'num': int(m.group(1)),
                    'prefix': int(m.group(2), 16),
                    'a2': int(m.group(3)),
                    't6': int(m.group(4), 16),
                    't7': int(m.group(5), 16),
                    'dst_off': int(m.group(6), 16),
                })
    return tokens

def main():
    emu_tokens = parse_emu_trace("/tmp/ps1_token_trace.log")
    py_tokens = parse_py_trace("/tmp/py_token_trace.txt")
    
    print(f"Emulator tokens: {len(emu_tokens)}")
    print(f"Python tokens:   {len(py_tokens)}")
    print()
    
    # Compare token by token
    min_len = min(len(emu_tokens), len(py_tokens))
    first_mismatch = None
    
    for i in range(min_len):
        e = emu_tokens[i]
        p = py_tokens[i]
        
        mismatches = []
        if e['prefix'] != p['prefix']:
            mismatches.append(f"prefix: emu=0x{e['prefix']:02X} py=0x{p['prefix']:02X}")
        if e['a2'] != p['a2']:
            mismatches.append(f"a2: emu={e['a2']} py={p['a2']} (diff={e['a2'] - p['a2']})")
        if e['dst_off'] != p['dst_off']:
            mismatches.append(f"dst_off: emu=0x{e['dst_off']:04X} py=0x{p['dst_off']:04X}")
        if e['t6'] != p['t6']:
            mismatches.append(f"t6: emu=0x{e['t6']:08X} py=0x{p['t6']:08X} (diff={e['t6'] - p['t6']})")
        
        if mismatches:
            if first_mismatch is None:
                first_mismatch = i
            
            # Print context: 3 matching tokens before
            if first_mismatch == i:
                start = max(0, i - 3)
                print("Last matching tokens before mismatch:")
                for j in range(start, i):
                    ej = emu_tokens[j]
                    pj = py_tokens[j]
                    print(f"  Token #{ej['num']:4d}: prefix=0x{ej['prefix']:02X} "
                          f"a2={ej['a2']:6d} dst_off=0x{ej['dst_off']:04X} t6=0x{ej['t6']:08X} ✓")
                print()
            
            print(f"*** MISMATCH at token #{e['num']} (py #{p['num']}):")
            for m in mismatches:
                print(f"    {m}")
            
            # Print a few more
            if i >= first_mismatch + 10:
                print(f"\n(Stopping after 10 mismatches)")
                break
    
    if first_mismatch is None:
        print(f"✅ All {min_len} tokens match perfectly!")
    else:
        print(f"\n❌ First mismatch at token #{first_mismatch}")
        
        # Show what type of token it is
        e = emu_tokens[first_mismatch]
        p = py_tokens[first_mismatch]
        
        emu_type = "BACKREF" if e['prefix'] & 0x80 else f"LITERAL({e['prefix']})"
        py_type = "BACKREF" if p['prefix'] & 0x80 else f"LITERAL({p['prefix']})"
        
        print(f"  Emulator: {emu_type}, prefix=0x{e['prefix']:02X}")
        print(f"  Python:   {py_type}, prefix=0x{p['prefix']:02X}")
        
        # If this is the same token but offset, show what happened
        if e['prefix'] == p['prefix'] and e['a2'] != p['a2']:
            print(f"\n  Same prefix but different remaining count!")
            print(f"  a2 difference = {e['a2'] - p['a2']} bytes")
            print(f"  This means earlier tokens processed differently.")

if __name__ == "__main__":
    main()
