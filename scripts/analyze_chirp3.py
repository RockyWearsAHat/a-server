#!/usr/bin/env python3
"""Analyze the chirp waveform more closely to determine the source."""
import wave
import struct
import numpy as np

path = '/tmp/mmbn_fixed.wav'
with wave.open(path, 'rb') as wf:
    nch = wf.getnchannels()
    rate = wf.getframerate()
    nframes = wf.getnframes()
    raw = wf.readframes(nframes)

samples = np.array(struct.unpack(f'<{nframes*nch}h', raw), dtype=np.float64)
left = samples[0::2]
right = samples[1::2]

# Find the first chirp
diff = np.abs(np.diff(left))
threshold = 2000
chirp_starts = []
prev = -1000
for i in np.where(diff > threshold)[0]:
    if i - prev > 500:
        chirp_starts.append(i)
        prev = i

print(f"Found {len(chirp_starts)} chirps")
if chirp_starts:
    # Analyze first chirp in detail - both L and R channels
    pos = chirp_starts[0]
    print(f"\nFirst chirp at sample {pos} (t={pos/rate:.4f}s):")
    print(f"{'Offset':>6} {'Left':>8} {'Right':>8} {'L-R':>8}")
    for i in range(max(0, pos-5), min(len(left), pos+50)):
        l = left[i]
        r = right[i]
        marker = " <<<<" if i == pos else ""
        print(f"{i-pos:+6d} {l:+8.0f} {r:+8.0f} {l-r:+8.0f}{marker}")

    # Check if chirps are symmetric (L == R) - indicates mono PSG
    print(f"\nL/R correlation in chirp region:")
    chirp_l = left[pos:pos+40]
    chirp_r = right[pos:pos+40]
    corr = np.corrcoef(chirp_l, chirp_r)[0, 1]
    print(f"  Correlation: {corr:.4f}")
    print(f"  L==R: {np.allclose(chirp_l, chirp_r, atol=1)}")
    print(f"  Max |L-R|: {np.max(np.abs(chirp_l - chirp_r)):.0f}")

    # Unique values in chirp (tells us about quantization / source)
    unique_vals = sorted(set(chirp_l.astype(int).tolist()))
    print(f"\n  Unique L values in chirp: {unique_vals}")

    # Try to determine what DacOutput values these correspond to
    # DacOutput = (sample * 2 - 15) * 256
    # Possible values for volume v, high bit: v -> (v*2-15)*256
    print(f"\n  Reverse-engineering DacOutput values:")
    for v in unique_vals:
        if v == 0:
            continue
        # v might be the raw DacOutput or after PSG mixing
        # DacOutput raw: (sample*2-15)*256, sample = 0 or volume
        # So possible: -3840 (sample=0) or (vol*2-15)*256
        for vol in range(16):
            high = (vol * 2 - 15) * 256
            low = (0 * 2 - 15) * 256  # -3840
            if v == high or v == low:
                print(f"    {v:+6d} = DacOutput(vol={vol}, {'high' if v==high else 'low'})")
