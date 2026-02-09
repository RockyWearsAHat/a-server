#!/usr/bin/env python3
import wave, numpy as np, sys

wav_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/mmbn_scnth.wav"

with wave.open(wav_path, "rb") as f:
    sr = f.getframerate()
    data = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16)
    left = data[0::2]
    right = data[1::2]

print(f"Sample rate: {sr}, Total samples: {len(left)}, Duration: {len(left)/sr:.2f}s")
print(f"Left  - nonzero: {np.count_nonzero(left)}, max: {np.max(np.abs(left))}, rms: {np.sqrt(np.mean(left.astype(float)**2)):.1f}")
print(f"Right - nonzero: {np.count_nonzero(right)}, max: {np.max(np.abs(right))}, rms: {np.sqrt(np.mean(right.astype(float)**2)):.1f}")

window_size = 1000  # ~30ms at 32768Hz
print(f"\nLeft channel activity ({window_size/sr*1000:.0f}ms windows):")
active_count = 0
silent_count = 0
for i in range(0, len(left) - window_size, window_size):
    window = left[i:i+window_size]
    rms = np.sqrt(np.mean(window.astype(float)**2))
    if rms > 10:
        active_count += 1
    else:
        silent_count += 1
print(f"  Active windows: {active_count}, Silent windows: {silent_count}")
if active_count + silent_count > 0:
    print(f"  Active ratio: {active_count/(active_count+silent_count)*100:.1f}%")

first_nonzero_l = np.argmax(np.abs(left) > 0)
print(f"  First audio at sample {first_nonzero_l} ({first_nonzero_l/sr*1000:.0f}ms)")

# Show activity pattern as timeline
print("\nTimeline (10ms resolution, '=' active, '.' silent):")
w2 = int(sr * 0.01)  # 10ms
timeline = []
for i in range(0, len(left) - w2, w2):
    rms = np.sqrt(np.mean(left[i:i+w2].astype(float)**2))
    timeline.append("=" if rms > 10 else ".")

# Print in rows of 100 (= 1 second per row)
for row_start in range(0, len(timeline), 100):
    row = "".join(timeline[row_start:row_start+100])
    t = row_start * 10 / 1000
    print(f"  {t:5.1f}s: {row}")
