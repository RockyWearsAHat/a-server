#!/usr/bin/env python3
"""Instead of tracing the full decompressor, let's verify the literal loop
behavior by running a MIPS interpreter that mirrors our emulator's load delay
pipeline to see if the literal loop copies the correct number of bytes."""

import struct

def mips_literal_loop_sim(literal_count):
    """Simulate the literal loop with load delay pipeline, starting with a0=literal_count.
    Returns the number of bytes actually copied."""
    
    # Register file
    regs = {
        'v0': 0, 'v1': 0, 'a0': literal_count, 'a1': 0,
        't1': 0xFFFFFFFF,  # -1
        't6': 0x1000,  # src ptr (arbitrary)
        't7': 0x2000,  # dst ptr (arbitrary)
        'a2': 50000,   # remaining (arbitrary)
    }
    
    # Load delay pipeline
    pending_load = None  # (reg, value)
    next_load = None     # (reg, value)
    
    # Memory (fake)
    mem = {i: (i & 0xFF) for i in range(0x1000, 0x2000)}
    output = {}
    
    def read_reg(r):
        return regs[r] & 0xFFFFFFFF
    
    def write_reg(r, v):
        nonlocal pending_load
        regs[r] = v & 0xFFFFFFFF
        # Cancel pending load for same register
        if pending_load and pending_load[0] == r:
            pending_load = None
    
    def apply_pending():
        nonlocal pending_load
        if pending_load:
            r, v = pending_load
            write_reg(r, v)
            pending_load = None
    
    def start_step():
        nonlocal pending_load, next_load
        pending_load = next_load
        next_load = None
    
    def end_step():
        apply_pending()
    
    bytes_copied = 0
    pc = 0  # Instruction index
    
    # Execute: SUBU $a2, $a2, $a0  (remaining -= literal_count)
    start_step()
    regs['a2'] = (regs['a2'] - regs['a0']) & 0xFFFFFFFF
    end_step()
    
    # Execute: ADDIU $a0, $a0, -1  (a0 = literal_count - 1)
    start_step()
    regs['a0'] = (regs['a0'] - 1) & 0xFFFFFFFF
    end_step()
    
    # Execute: BEQ $a0, $t1, skip_loop
    start_step()
    if regs['a0'] == regs['t1']:  # if a0 == -1 (was 0)
        end_step()
        return 0  # Skip loop entirely
    # Branch not taken, fall through
    end_step()
    
    # Execute: NOP (delay slot)
    start_step()
    end_step()
    
    # Execute: ADDIU $v1, $zero, -1  (v1 = -1)
    start_step()
    regs['v1'] = 0xFFFFFFFF
    end_step()
    
    # Enter literal loop
    max_iter = literal_count + 10  # Safety limit
    iterations = 0
    
    while iterations < max_iter:
        iterations += 1
        
        # LBU $v0, 0($t6)
        start_step()
        byte_val = mem.get(regs['t6'], 0)
        next_load = ('v0', byte_val)
        end_step()
        
        # ADDIU $t6, $t6, 1
        start_step()
        regs['t6'] = (regs['t6'] + 1) & 0xFFFFFFFF
        end_step()
        
        # ADDIU $a0, $a0, -1
        start_step()
        regs['a0'] = (regs['a0'] - 1) & 0xFFFFFFFF
        end_step()
        
        # SB $v0, 0($t7)
        start_step()
        output[regs['t7']] = regs['v0'] & 0xFF
        bytes_copied += 1
        end_step()
        
        # BNE $a0, $v1, loop
        start_step()
        branch_taken = regs['a0'] != regs['v1']
        end_step()
        
        # ADDIU $t7, $t7, 1 (delay slot)
        start_step()
        regs['t7'] = (regs['t7'] + 1) & 0xFFFFFFFF
        end_step()
        
        if not branch_taken:
            break
    
    return bytes_copied

# Test various literal counts
print("Testing literal loop simulation:")
print("=" * 50)

for count in [1, 2, 3, 5, 10, 73, 127, 0]:
    actual = mips_literal_loop_sim(count)
    expected = count
    status = "✓" if actual == expected else "✗ BUG!"
    print(f"  literal_count={count:3d} → copied={actual:3d} (expected={expected:3d}) {status}")

print()
print("Testing backref loop simulation:")
print("=" * 50)

def mips_backref_loop_sim(span):
    """Simulate the backref copy loop."""
    regs = {
        'v0': 0, 'v1': 0, 'a0': span, 'a1': 0xFFFFFFFF,
        't1': 0xFFFFFFFF,
        't7': 0x2000,
    }
    
    pending_load = None
    next_load = None
    
    mem = {i: (i & 0xFF) for i in range(0x1000, 0x2000)}
    
    def start_step():
        nonlocal pending_load, next_load
        pending_load = next_load
        next_load = None
    
    def end_step():
        nonlocal pending_load
        if pending_load:
            r, v = pending_load
            regs[r] = v & 0xFFFFFFFF
            pending_load = None
    
    # Before the loop: a0 already has span value
    # SUBU $a2, $a2, $a0 (remaining -= span) — skip, not relevant
    
    # ADDIU $a0, $a0, -1
    start_step()
    regs['a0'] = (span - 1) & 0xFFFFFFFF
    end_step()
    
    # BEQ $a0, $t1, skip
    start_step()
    if regs['a0'] == regs['t1']:
        end_step()
        return 0
    end_step()
    
    # ADDIU $a1, $zero, -1 (delay slot)
    start_step()
    regs['a1'] = 0xFFFFFFFF
    end_step()
    
    # Set up backref source
    backref_src = 0x1800  # Arbitrary
    regs['v1'] = backref_src
    
    bytes_copied = 0
    max_iter = span + 10
    iterations = 0
    
    while iterations < max_iter:
        iterations += 1
        
        # LBU $v0, 0($v1)
        start_step()
        byte_val = mem.get(regs['v1'], 0)
        next_load = ('v0', byte_val)
        end_step()
        
        # ADDIU $v1, $v1, 1
        start_step()
        regs['v1'] = (regs['v1'] + 1) & 0xFFFFFFFF
        end_step()
        
        # ADDIU $a0, $a0, -1
        start_step()
        regs['a0'] = (regs['a0'] - 1) & 0xFFFFFFFF
        end_step()
        
        # SB $v0, 0($t7)
        start_step()
        bytes_copied += 1
        end_step()
        
        # BNE $a0, $a1, loop
        start_step()
        branch_taken = regs['a0'] != regs['a1']
        end_step()
        
        # ADDIU $t7, $t7, 1 (delay slot)
        start_step()
        regs['t7'] = (regs['t7'] + 1) & 0xFFFFFFFF
        end_step()
        
        if not branch_taken:
            break
    
    return bytes_copied

for span in [3, 4, 5, 7, 10, 64]:
    actual = mips_backref_loop_sim(span)
    expected = span
    status = "✓" if actual == expected else "✗ BUG!"
    print(f"  span={span:3d} → copied={actual:3d} (expected={expected:3d}) {status}")
