#!/usr/bin/env python3
"""More precise analysis: compute exact chirp interval and correlate with GBA timing."""
import wave
import struct
import numpy as np

path = '/tmp/MegaManBattleNetwork_audio.wav'
with wave.open(path, 'rb') as wf:
    nch = wf.getnchannels()
    rate = wf.getframerate()
    nframes = wf.getnframes()
    raw = wf.readframes(nframes)

samples = np.array(struct.unpack(f'<{nframes*nch}h', raw), dtype=np.float64)
left = samples[0::2]
right = samples[1::2]
mono = (left + right) / 2

# Find all chirp starts (sharp rise from near-zero)
diff = np.abs(np.diff(mono))
threshold = 2000
chirp_starts = []
prev = -1000
for i in np.where(diff > threshold)[0]:
    if i - prev > 500:  # minimum 15ms between chirps
        chirp_starts.append(i)
        prev = i

print(f"Found {len(chirp_starts)} chirps")
print(f"Chirp positions (sample, time):")
for i, pos in enumerate(chirp_starts[:25]):
    t = pos / rate
    # Measure peak amplitude in 100-sample window
    window = mono[pos:min(len(mono), pos+200)]
    peak = np.max(np.abs(window))
    print(f"  Chirp {i}: sample={pos}, t={t:.4f}s, peak={peak:.0f}")

if len(chirp_starts) >= 2:
    intervals = np.diff(chirp_starts) / rate
    print(f"\nInterval stats:")
    print(f"  Mean: {np.mean(intervals)*1000:.1f} ms")
    print(f"  Std:  {np.std(intervals)*1000:.1f} ms")
    print(f"  Min:  {np.min(intervals)*1000:.1f} ms")
    print(f"  Max:  {np.max(intervals)*1000:.1f} ms")
    
    # Convert to GBA frames (16.73ms/frame)
    frame_period = 280896 / 16777216  # seconds per frame
    intervals_in_frames = intervals / frame_period
    print(f"  In GBA frames: mean={np.mean(intervals_in_frames):.2f}, "
          f"min={np.min(intervals_in_frames):.2f}, max={np.max(intervals_in_frames):.2f}")
    
    # Check if it matches frame sequencer period
    fs_period = 32768 / 16777216  # frame sequencer step period
    intervals_in_fs = intervals / fs_period
    print(f"  In frame-seq steps: mean={np.mean(intervals_in_fs):.1f}")

# Analyze the chirp waveform shape
if chirp_starts:
    print(f"\nChirp waveform (first chirp at sample {chirp_starts[0]}):")
    start = max(0, chirp_starts[0] - 20)
    end = min(len(mono), chirp_starts[0] + 100)
    for i in range(start, end):
        bar = '#' * max(0, int(abs(mono[i]) / 100))
        sign = '+' if mono[i] >= 0 else '-'
        print(f"  [{i-chirp_starts[0]:+4d}] {mono[i]:+8.0f} {sign}{bar}")
