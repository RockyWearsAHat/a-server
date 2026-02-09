#!/usr/bin/env python3
"""Analyze WAV file for chirp/click artifacts characteristic of PSG DC offset issues."""
import wave
import struct
import sys
import numpy as np

def analyze_wav(path):
    with wave.open(path, 'rb') as wf:
        nch = wf.getnchannels()
        sw = wf.getsampwidth()
        rate = wf.getframerate()
        nframes = wf.getnframes()
        raw = wf.readframes(nframes)

    fmt = f'<{nframes * nch}h'
    samples = np.array(struct.unpack(fmt, raw), dtype=np.float64)
    if nch == 2:
        left = samples[0::2]
        right = samples[1::2]
        mono = (left + right) / 2
    else:
        mono = samples

    print(f"File: {path}")
    print(f"  Rate: {rate} Hz, Channels: {nch}, Frames: {nframes}, Duration: {nframes/rate:.2f}s")
    print(f"  RMS: {np.sqrt(np.mean(mono**2)):.1f}")
    print(f"  Peak: {np.max(np.abs(mono)):.0f}")
    print(f"  DC offset: {np.mean(mono):.1f}")

    # Detect clicks/chirps: look for sudden amplitude changes
    # A chirp manifests as a brief spike in the derivative
    diff = np.diff(mono)
    threshold = 500  # sample-to-sample jump threshold
    clicks = np.where(np.abs(diff) > threshold)[0]
    print(f"\n  Sudden jumps (>{threshold}): {len(clicks)}")
    if len(clicks) > 0:
        # Group nearby clicks into events
        events = []
        current_start = clicks[0]
        current_end = clicks[0]
        for c in clicks[1:]:
            if c - current_end < 100:  # within ~3ms at 32768Hz
                current_end = c
            else:
                events.append((current_start, current_end))
                current_start = c
                current_end = c
        events.append((current_start, current_end))
        print(f"  Click events: {len(events)}")
        for i, (s, e) in enumerate(events[:20]):
            t_s = s / rate
            t_e = e / rate
            peak_in_range = np.max(np.abs(diff[s:e+1]))
            # Look at DC level before and after
            before = np.mean(mono[max(0,s-50):s]) if s > 50 else 0
            after = np.mean(mono[e:min(len(mono),e+50)])
            print(f"    Event {i}: t={t_s:.4f}-{t_e:.4f}s, peak_jump={peak_in_range:.0f}, "
                  f"DC before={before:.0f}, DC after={after:.0f}")

    # Analyze frequency content in short windows to find chirp frequencies
    window_size = 512
    num_windows = min(20, len(mono) // window_size)
    print(f"\n  Spectral analysis (first {num_windows} windows of {window_size} samples):")
    for w in range(num_windows):
        start = w * window_size
        end = start + window_size
        segment = mono[start:end]
        fft = np.fft.rfft(segment)
        magnitudes = np.abs(fft)
        freqs = np.fft.rfftfreq(window_size, 1.0/rate)
        # Find dominant frequency (skip DC at index 0)
        if len(magnitudes) > 1:
            peak_idx = np.argmax(magnitudes[1:]) + 1
            peak_freq = freqs[peak_idx]
            peak_mag = magnitudes[peak_idx]
            dc_mag = magnitudes[0]
            t = start / rate
            if peak_mag > 100:
                print(f"    Window {w} (t={t:.3f}s): dominant={peak_freq:.0f}Hz "
                      f"mag={peak_mag:.0f}, DC={dc_mag:.0f}")

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/MegaManBattleNetwork_audio.wav'
    analyze_wav(path)
