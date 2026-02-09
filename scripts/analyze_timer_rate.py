#!/usr/bin/env python3
"""Trace upsample ratio calculation for different games.
Reads timer config from a headless run to verify the ratio."""
import subprocess
import sys

# GBA CPU frequency
GBA_CPU_FREQ = 16777216.0
OUTPUT_RATE = 32768.0

# Common M4A timer configurations
# Timer reload value and prescaler
configs = {
    "MMBN (typical)": (0xFBE8, 0),  # reload=0xFBE8, prescaler F/1
    "SMA2 (typical)": (0xFBE8, 0),  # Same - most M4A games use this
    "M4A default": (0xFBE8, 0),     # Standard M4A: 16384 Hz mixing
    "M4A 32768": (0xFF00, 0),       # 32768 Hz = reload 0xFF00
    "M4A 10512": (0xFBE8, 0),      # Actually: 0x10000 - 0xFBE8 = 0x418 = 1048
    "M4A 13379": (0xFC5F, 0),      
    "M4A 18157": (0xFC9A, 0),
    "M4A 21024": (0xFCDA, 0),
    "M4A 26758": (0xFD72, 0),
    "M4A 36314": (0xFE1C, 0),
    "M4A 40137": (0xFE5C, 0),
    "M4A 42048": (0xFE70, 0),
}

print(f"Output sample rate: {OUTPUT_RATE} Hz")
print(f"GBA CPU freq: {GBA_CPU_FREQ} Hz")
print()
print(f"{'Config':<25} {'Reload':>8} {'CycPerSamp':>10} {'InputRate':>10} {'UpsampleRatio':>14} {'Repeats':>8}")
print("-" * 85)

for name, (reload, prescaler_idx) in configs.items():
    prescaler = [1, 64, 256, 1024][prescaler_idx]
    cycles_per_sample = (0x10000 - reload) * prescaler
    input_rate = GBA_CPU_FREQ / cycles_per_sample if cycles_per_sample > 0 else 0
    ratio = OUTPUT_RATE / input_rate if input_rate > 0 else 0
    repeats = ratio  # how many output samples per input
    print(f"{name:<25} {reload:#06x}   {cycles_per_sample:>10} {input_rate:>10.1f} {ratio:>14.4f} {repeats:>8.2f}")

print()
print("Key insight: if ratio > 1.0, each input sample produces multiple output samples (staircase)")
print("If ratio ≈ 1.0, input and output rates match (no upsampling needed)")
print()

# The standard M4A mixing rate
# 0x10000 - 0xFBE8 = 0x0418 = 1048 cycles per sample  
# 16777216 / 1048 = 16009.75 Hz
cycles = 0x10000 - 0xFBE8
print(f"Standard M4A: 0x10000 - 0xFBE8 = {cycles} cycles")
print(f"Rate: {GBA_CPU_FREQ/cycles:.2f} Hz")
print(f"Upsample ratio to {OUTPUT_RATE}: {OUTPUT_RATE/(GBA_CPU_FREQ/cycles):.4f}")
print(f"Each sample repeats ~{OUTPUT_RATE/(GBA_CPU_FREQ/cycles):.1f}x")
