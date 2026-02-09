#!/usr/bin/env python3
"""Analyze WAV audio for silence gaps and jitter patterns."""
import sys
import wave
import struct
import numpy as np

def analyze_gaps(path, threshold=100, min_gap_samples=8):
    """Find silence gaps in audio and report timing."""
    with wave.open(path, 'rb') as wf:
        rate = wf.getframerate()
        channels = wf.getnchannels()
        nframes = wf.getnframes()
        raw = wf.readframes(nframes)

    samples = np.frombuffer(raw, dtype=np.int16)
    if channels == 2:
        # Mix to mono for analysis
        left = samples[0::2]
        right = samples[1::2]
        mono = ((left.astype(np.int32) + right.astype(np.int32)) // 2).astype(np.int16)
    else:
        mono = samples

    print(f"File: {path}")
    print(f"Sample rate: {rate} Hz, Channels: {channels}, Duration: {nframes/rate:.3f}s")
    print(f"Total samples: {len(mono)}")
    print()

    # Find silence runs (absolute value below threshold)
    is_silent = np.abs(mono.astype(np.int32)) < threshold
    
    # Find runs of silence
    gaps = []
    in_gap = False
    gap_start = 0
    for i in range(len(is_silent)):
        if is_silent[i] and not in_gap:
            in_gap = True
            gap_start = i
        elif not is_silent[i] and in_gap:
            in_gap = False
            gap_len = i - gap_start
            if gap_len >= min_gap_samples:
                gaps.append((gap_start, gap_len))
    if in_gap:
        gap_len = len(is_silent) - gap_start
        if gap_len >= min_gap_samples:
            gaps.append((gap_start, gap_len))

    # Skip initial boot silence (first 0.5s)
    boot_samples = int(rate * 0.5)
    gaps_after_boot = [(s, l) for s, l in gaps if s > boot_samples]

    print(f"=== Silence gaps (>{min_gap_samples} samples, threshold={threshold}) ===")
    print(f"Total gaps: {len(gaps)}")
    print(f"Gaps after boot (0.5s): {len(gaps_after_boot)}")
    
    if gaps_after_boot:
        gap_lengths = [l for _, l in gaps_after_boot]
        gap_times = [s / rate * 1000 for s, _ in gaps_after_boot]
        gap_durations_ms = [l / rate * 1000 for _, l in gaps_after_boot]
        
        print(f"\nGap length stats (samples): min={min(gap_lengths)}, max={max(gap_lengths)}, "
              f"mean={np.mean(gap_lengths):.1f}, median={np.median(gap_lengths):.1f}")
        print(f"Gap duration stats (ms): min={min(gap_durations_ms):.2f}, max={max(gap_durations_ms):.2f}, "
              f"mean={np.mean(gap_durations_ms):.2f}")
        
        # Check for periodic gaps
        if len(gap_times) > 2:
            intervals = np.diff(gap_times)
            print(f"\nGap intervals (ms): min={min(intervals):.2f}, max={max(intervals):.2f}, "
                  f"mean={np.mean(intervals):.2f}, std={np.std(intervals):.2f}")
        
        print(f"\nFirst 30 gaps after boot:")
        for i, (start, length) in enumerate(gaps_after_boot[:30]):
            time_ms = start / rate * 1000
            dur_ms = length / rate * 1000
            print(f"  Gap {i}: at {time_ms:.1f}ms, length={length} samples ({dur_ms:.2f}ms)")

    # Also check for zero-sample runs (ring buffer underruns)
    is_zero = mono == 0
    zero_runs = []
    in_zero = False
    zero_start = 0
    for i in range(len(is_zero)):
        if is_zero[i] and not in_zero:
            in_zero = True
            zero_start = i
        elif not is_zero[i] and in_zero:
            in_zero = False
            run_len = i - zero_start
            if run_len >= 16:
                zero_runs.append((zero_start, run_len))
    
    zero_runs_after_boot = [(s, l) for s, l in zero_runs if s > boot_samples]
    print(f"\n=== Exact-zero runs (>=16 samples, after boot) ===")
    print(f"Count: {len(zero_runs_after_boot)}")
    if zero_runs_after_boot:
        for i, (start, length) in enumerate(zero_runs_after_boot[:20]):
            time_ms = start / rate * 1000
            dur_ms = length / rate * 1000
            print(f"  Zero run {i}: at {time_ms:.1f}ms, length={length} ({dur_ms:.2f}ms)")

    # Analyze sample-to-sample differences to find "steppy" behavior
    print(f"\n=== Sample continuity analysis ===")
    diffs = np.abs(np.diff(mono.astype(np.int32)))
    
    # After boot
    diffs_after = diffs[boot_samples:]
    active_region = mono[boot_samples:]
    active_mask = np.abs(active_region.astype(np.int32)) > threshold
    
    if np.any(active_mask):
        active_diffs = diffs_after[active_mask[:-1]]
        print(f"Active region sample-to-sample diff: mean={np.mean(active_diffs):.1f}, "
              f"max={np.max(active_diffs)}, p99={np.percentile(active_diffs, 99):.0f}")
        
        # Large jumps (potential clicks/gaps)
        large_jumps = np.where(diffs_after > 5000)[0]
        print(f"Large jumps (>5000): {len(large_jumps)}")
        
        # Count consecutive identical samples (sample-and-hold repeats)
        consecutive_equal = np.sum(diffs_after == 0)
        print(f"Consecutive equal samples: {consecutive_equal}/{len(diffs_after)} "
              f"({100*consecutive_equal/len(diffs_after):.1f}%)")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/sma2_raw.wav"
    analyze_gaps(path)
