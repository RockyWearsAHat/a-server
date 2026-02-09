#!/usr/bin/env python3
"""Analyze the chirp pattern in MMBN audio - find click/pop transients"""
import wave, numpy as np, sys

wav_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/mmbn_scnth.wav"

with wave.open(wav_path, "rb") as f:
    sr = f.getframerate()
    data = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16)
    left = data[0::2].astype(float)
    right = data[1::2].astype(float)

# Find sudden large jumps in the left channel (clicks/pops)
derivative = np.abs(np.diff(left))
threshold = 3000  # Jump of 3000+ in one sample = click
clicks = np.where(derivative > threshold)[0]

print(f"Left channel clicks (jump > {threshold}): {len(clicks)}")

# Group clicks that are within 50 samples of each other
if len(clicks) > 0:
    groups = []
    current_group = [clicks[0]]
    for c in clicks[1:]:
        if c - current_group[-1] < 50:
            current_group.append(c)
        else:
            groups.append(current_group)
            current_group = [c]
    groups.append(current_group)
    
    print(f"Click groups: {len(groups)}")
    print(f"\nFirst 20 click groups:")
    for i, g in enumerate(groups[:20]):
        t_ms = g[0] / sr * 1000
        # Show the waveform around the click
        center = g[0]
        start = max(0, center - 5)
        end = min(len(left), center + 15)
        samples = left[start:end].astype(int).tolist()
        max_jump = max(derivative[g])
        print(f"  [{i:2d}] t={t_ms:7.1f}ms  jump={max_jump:6.0f}  samples: {samples}")
    
    # Timing between groups
    if len(groups) > 1:
        intervals = [(groups[i+1][0] - groups[i][0]) / sr * 1000 for i in range(len(groups)-1)]
        print(f"\nInter-click intervals: min={min(intervals):.1f}ms, max={max(intervals):.1f}ms, mean={np.mean(intervals):.1f}ms")
        # Histogram of intervals
        hist, edges = np.histogram(intervals, bins=[0,5,10,20,50,100,200,500,1000,5000])
        print("Interval distribution:")
        for i in range(len(hist)):
            if hist[i] > 0:
                print(f"  {edges[i]:5.0f}-{edges[i+1]:5.0f}ms: {hist[i]:4d}")

# Check for DC offset 
print(f"\nDC offset: left={np.mean(left):.1f}, right={np.mean(right):.1f}")

# Check sample value distribution 
print(f"\nLeft unique values: {len(np.unique(left))}")
print(f"Right unique values: {len(np.unique(right))}")

# Check if audio looks like upsampled lower-rate signal
# Count zero-crossings
zc_left = np.sum(np.diff(np.sign(left)) != 0)
zc_right = np.sum(np.diff(np.sign(right)) != 0)
print(f"\nZero crossings: left={zc_left} ({zc_left/len(left)*sr:.0f}/s), right={zc_right} ({zc_right/len(right)*sr:.0f}/s)")

# Check for repeated sample values (sign of sample-and-hold without interpolation)
repeats_left = np.sum(np.diff(left) == 0)
print(f"Consecutive equal samples: left={repeats_left} ({repeats_left/len(left)*100:.1f}%)")
