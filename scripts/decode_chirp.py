#!/usr/bin/env python3
"""Check if chirps match FIFO sample values (int8_t * volume) rather than PSG."""
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

# Find chirps
diff = np.abs(np.diff(left))
chirp_starts = []
prev = -1000
for i in np.where(diff > 2000)[0]:
    if i - prev > 500:
        chirp_starts.append(i)
        prev = i

# Look at the chirp samples and try to decode as FIFO values
# FIFO output = int8_t * vol (32 or 64)
# After HPF, values are shifted, so look at raw differences
print(f"Found {len(chirp_starts)} chirps\n")

if chirp_starts:
    pos = chirp_starts[0]
    chirp = left[pos:pos+60]
    
    print("Trying to decode chirp as FIFO samples (vol=64, left only):")
    for i, v in enumerate(chirp):
        if v != 0:
            as_fifo64 = v / 64.0
            as_fifo32 = v / 32.0
            as_psg = "?"
            # PSG DacOutput = (sample*2-15)*256, sample 0-15
            for s in range(16):
                if abs(v - (s*2-15)*256) < 50:
                    as_psg = f"vol={s}"
            print(f"  [{i:3d}] L={v:+8.0f}  as FIFO@64={as_fifo64:+7.1f}  "
                  f"as FIFO@32={as_fifo32:+7.1f}  as PSG: {as_psg}")

    # Check right channel during chirp — should be pure FIFO if chirp is psg
    print(f"\nRight channel during chirp (should show FIFO audio if separate from chirp):")
    for i in range(max(0,pos-5), min(len(right), pos+60)):
        if right[i] != 0:
            print(f"  [{i-pos:+4d}] R={right[i]:+8.0f}")

    # Look at the steady-state FIFO audio before the chirp 
    before = left[max(0,pos-200):pos]
    after = left[pos+200:pos+400]
    print(f"\nSteady state before chirp: mean={np.mean(before):.1f}, rms={np.sqrt(np.mean(before**2)):.1f}")
    print(f"Steady state after chirp:  mean={np.mean(after):.1f}, rms={np.sqrt(np.mean(after**2)):.1f}")
