#!/usr/bin/env python3
"""Check the spectral content of audio to find imaging artifacts from upsampling"""
import wave, numpy as np, sys

wav_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/sma2_current.wav"

with wave.open(wav_path, "rb") as f:
    sr = f.getframerate()
    data = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16)
    left = data[0::2].astype(float)
    right = data[1::2].astype(float)

# Use mono mix
mono = (left + right) / 2

# Find a section with audio content
window_ms = 500  # analyze 500ms chunks
w = int(sr * window_ms / 1000)
best_rms = 0
best_start = 0
for i in range(0, len(mono) - w, w // 4):
    rms = np.sqrt(np.mean(mono[i:i+w]**2))
    if rms > best_rms:
        best_rms = rms
        best_start = i

print(f"File: {wav_path}")
print(f"Sample rate: {sr}")
print(f"Analyzing {window_ms}ms chunk at {best_start/sr:.2f}s (rms={best_rms:.1f})")

chunk = mono[best_start:best_start+w]

# FFT
spectrum = np.abs(np.fft.rfft(chunk))
freqs = np.fft.rfftfreq(len(chunk), 1/sr)

# Find energy in bands
bands = [(0, 2000), (2000, 4000), (4000, 6000), (6000, 8000), (8000, 10000),
         (10000, 12000), (12000, 14000), (14000, 16000)]
print("\nSpectral energy by band:")
for lo, hi in bands:
    mask = (freqs >= lo) & (freqs < hi)
    energy = np.sqrt(np.mean(spectrum[mask]**2)) if mask.any() else 0
    bar = '#' * min(60, int(energy / 10))
    print(f"  {lo:5d}-{hi:5d}Hz: {energy:8.1f} {bar}")

# Check for sample repetition patterns
print(f"\nSample repetition analysis:")
# Count runs of identical samples
run_lengths = []
current_val = chunk[0]
run_len = 1
for s in chunk[1:]:
    if s == current_val:
        run_len += 1
    else:
        if run_len > 1:
            run_lengths.append(run_len)
        current_val = s
        run_len = 1

if run_lengths:
    hist, edges = np.histogram(run_lengths, bins=range(2, 12))
    print("  Run lengths (consecutive identical samples):")
    for i in range(len(hist)):
        if hist[i] > 0:
            print(f"    {int(edges[i]):2d}: {hist[i]:5d} occurrences")

# Detect source sample rate by looking at zero-crossing spacing
# If upsampled from ~18kHz to 32kHz, we'd see ~1.8 samples between changes
diffs = np.diff(chunk)
nonzero_diffs = np.where(np.abs(diffs) > 1)[0]
if len(nonzero_diffs) > 1:
    spacings = np.diff(nonzero_diffs)
    print(f"\n  Sample change spacings: mean={np.mean(spacings):.2f}, median={np.median(spacings):.1f}")
    hist2, edges2 = np.histogram(spacings, bins=range(1, 10))
    print("  Spacing distribution:")
    for i in range(len(hist2)):
        if hist2[i] > 0:
            pct = hist2[i] / len(spacings) * 100
            print(f"    {int(edges2[i]):2d} samples: {hist2[i]:6d} ({pct:5.1f}%)")
    
    mean_spacing = np.mean(spacings)
    estimated_source_rate = sr / mean_spacing
    print(f"\n  Estimated source sample rate: {estimated_source_rate:.0f} Hz")
    print(f"  Upsample ratio: {sr/estimated_source_rate:.2f}x")
