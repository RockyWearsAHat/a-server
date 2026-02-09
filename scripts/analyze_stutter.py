#!/usr/bin/env python3
"""Analyze audio for stuttering / note fragmentation patterns"""
import wave, numpy as np, sys

wav_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/sma2_current.wav"

with wave.open(wav_path, "rb") as f:
    sr = f.getframerate()
    data = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16)
    left = data[0::2].astype(float)
    right = data[1::2].astype(float)

print(f"File: {wav_path}")
print(f"Sample rate: {sr}, Duration: {len(left)/sr:.2f}s")
print(f"Left  rms: {np.sqrt(np.mean(left**2)):.1f}, max: {np.max(np.abs(left)):.0f}")
print(f"Right rms: {np.sqrt(np.mean(right**2)):.1f}, max: {np.max(np.abs(right)):.0f}")

# Look for silence gaps (stuttering = audio with silence gaps inserted)
window_ms = 2  # 2ms windows
w = int(sr * window_ms / 1000)
mono = (left + right) / 2

silence_threshold = 30
windows = []
for i in range(0, len(mono) - w, w):
    rms = np.sqrt(np.mean(mono[i:i+w]**2))
    windows.append(('.' if rms < silence_threshold else '='))

# Find alternating silent/active pattern (stuttering signature)
transitions = 0
for i in range(1, len(windows)):
    if windows[i] != windows[i-1]:
        transitions += 1

total_active = windows.count('=')
total_silent = windows.count('.')
print(f"\n{window_ms}ms windows: {total_active} active, {total_silent} silent, {transitions} transitions")
print(f"Transitions per second: {transitions / (len(mono)/sr):.1f}")

# Show first 5 seconds in detail (2ms resolution)
print(f"\nTimeline ({window_ms}ms resolution, '=' active, '.' silent):")
per_row = int(500 / window_ms)  # 500ms per row
for row_start in range(0, min(len(windows), int(5000/window_ms)), per_row):
    row = "".join(windows[row_start:row_start+per_row])
    t = row_start * window_ms / 1000
    print(f"  {t:5.2f}s: {row}")

# Detect envelope of the signal (are notes being cut short?)
envelope_window_ms = 10  # 10ms
ew = int(sr * envelope_window_ms / 1000)
print(f"\nEnvelope ({envelope_window_ms}ms windows) - first 3s:")
for i in range(0, min(len(mono), int(3 * sr)), ew):
    chunk = mono[i:i+ew]
    rms = np.sqrt(np.mean(chunk**2))
    t = i / sr
    bar = '#' * min(60, int(rms / 30))
    if rms > 10 or (i % (ew * 10) == 0):  # show all active + every 10th silent
        print(f"  {t:5.2f}s: rms={rms:6.1f} {bar}")

# Check for periodic silence insertion
# If stuttering, we'd see regular gaps
print(f"\nGap analysis (silence runs in {window_ms}ms units):")
runs = []
current_type = windows[0] if windows else '.'
run_len = 1
for w_char in windows[1:]:
    if w_char == current_type:
        run_len += 1
    else:
        runs.append((current_type, run_len))
        current_type = w_char
        run_len = 1
runs.append((current_type, run_len))

# Show statistics of silence runs
silence_runs = [r[1] for r in runs if r[0] == '.']
active_runs = [r[1] for r in runs if r[0] == '=']
if silence_runs:
    print(f"  Silence gaps: count={len(silence_runs)}, min={min(silence_runs)*window_ms}ms, max={max(silence_runs)*window_ms}ms, mean={np.mean(silence_runs)*window_ms:.1f}ms")
if active_runs:
    print(f"  Active  runs: count={len(active_runs)}, min={min(active_runs)*window_ms}ms, max={max(active_runs)*window_ms}ms, mean={np.mean(active_runs)*window_ms:.1f}ms")
