#!/usr/bin/env python3
"""
Visual Development Testing Tool

Provides easy commands for capturing screen output, recording sessions (with audio),
and running automated input tests for the AIO Server emulator.

Usage:
    ./scripts/visual_test.py capture [--rom ROM] [--wait SECONDS]
    ./scripts/visual_test.py record [--rom ROM] [--duration SECONDS]
    ./scripts/visual_test.py frame [--rom ROM] [--at-ms MS] [--output FILE]
    ./scripts/visual_test.py run-inputs [--rom ROM] [--script SCRIPT] [--capture-at MS]
    ./scripts/visual_test.py compare BEFORE AFTER [--output DIFF]
    ./scripts/visual_test.py audio-check [--rom ROM] [--duration SECONDS]
"""

from __future__ import annotations

import argparse
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional


def workspace_root() -> Path:
    return Path(__file__).resolve().parent.parent


def build_bin() -> Path:
    return workspace_root() / "build" / "bin"


def aioserver_path() -> Path:
    return build_bin() / "AIOServer"


def default_rom() -> Path:
    return workspace_root() / "test_roms" / "MegaManBattleNetwork.gba"


def kill_aioserver() -> None:
    """Kill any running AIOServer instances."""
    subprocess.run(["pkill", "-f", "AIOServer"], capture_output=True)
    time.sleep(0.5)


def ensure_output_dir() -> Path:
    """Ensure output directory exists and return path."""
    out_dir = workspace_root() / "test_output"
    out_dir.mkdir(exist_ok=True)
    return out_dir


def timestamp() -> str:
    """Generate timestamp string for filenames."""
    return time.strftime("%Y%m%d_%H%M%S")


def cmd_capture(args: argparse.Namespace) -> int:
    """Capture a screenshot of the running application."""
    rom = Path(args.rom).expanduser() if args.rom else default_rom()
    wait = float(args.wait)
    output = ensure_output_dir() / f"capture_{timestamp()}.png"
    
    if not rom.exists():
        print(f"ROM not found: {rom}")
        return 1
    
    if not aioserver_path().exists():
        print("AIOServer not built. Run 'make build' first.")
        return 1
    
    kill_aioserver()
    
    print(f"Starting AIOServer with {rom.name}...")
    proc = subprocess.Popen(
        [str(aioserver_path()), "--rom", str(rom)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    
    print(f"Waiting {wait} seconds...")
    time.sleep(wait)
    
    print(f"Capturing screen to {output}...")
    subprocess.run(["screencapture", "-x", str(output)])
    
    print("Stopping AIOServer...")
    proc.terminate()
    proc.wait(timeout=5)
    
    if output.exists():
        print(f"✓ Captured: {output}")
        print(f"  Size: {output.stat().st_size:,} bytes")
        return 0
    else:
        print("✗ Capture failed")
        return 1


def cmd_record(args: argparse.Namespace) -> int:
    """Record screen video of the running application."""
    rom = Path(args.rom).expanduser() if args.rom else default_rom()
    duration = float(args.duration)
    output = ensure_output_dir() / f"recording_{timestamp()}.mov"
    
    if not rom.exists():
        print(f"ROM not found: {rom}")
        return 1
    
    if not aioserver_path().exists():
        print("AIOServer not built. Run 'make build' first.")
        return 1
    
    kill_aioserver()
    
    print(f"Recording for {duration} seconds to {output}...")
    print("  (recording starts FIRST, then app launches)")
    
    # Start recording BEFORE the app so we capture the boot sequence
    # -v: video, -g: capture audio from default input, -V N: record for N seconds
    rec_proc = subprocess.Popen(
        ["screencapture", "-v", "-g", "-V", str(int(duration)), str(output)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    
    # Small delay to ensure recording has started
    time.sleep(0.5)
    
    print(f"Starting AIOServer with {rom.name}...")
    app_proc = subprocess.Popen(
        [str(aioserver_path()), "--rom", str(rom)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    
    # Wait for recording to complete (screencapture -V handles the duration)
    rec_proc.wait(timeout=duration + 10)
    
    print("Stopping AIOServer...")
    app_proc.terminate()
    try:
        app_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        print("  Force killing AIOServer...")
        app_proc.kill()
        app_proc.wait(timeout=2)
    
    # Also use pkill as backup to ensure it's dead
    subprocess.run(["pkill", "-f", "AIOServer"], capture_output=True)
    
    if output.exists():
        print(f"✓ Recorded: {output}")
        print(f"  Size: {output.stat().st_size:,} bytes")
        print(f"\n💡 Note: -g captures from default INPUT (microphone).")
        print(f"   To capture system audio, you need a loopback device like BlackHole.")
        print(f"   Install: brew install blackhole-2ch")
        print(f"   Then set BlackHole as your system output in System Preferences > Sound")
        return 0
    else:
        print("✗ Recording failed")
        return 1


def cmd_frame(args: argparse.Namespace) -> int:
    """Capture emulator framebuffer at specific time using headless mode."""
    rom = Path(args.rom).expanduser() if args.rom else default_rom()
    at_ms = int(args.at_ms)
    max_ms = at_ms + 2000  # Run a bit longer than capture time
    
    out_dir = ensure_output_dir()
    ppm_output = out_dir / f"frame_{timestamp()}.ppm"
    png_output = Path(args.output) if args.output else out_dir / f"frame_{timestamp()}.png"
    
    if not rom.exists():
        print(f"ROM not found: {rom}")
        return 1
    
    if not aioserver_path().exists():
        print("AIOServer not built. Run 'make build' first.")
        return 1
    
    kill_aioserver()
    
    print(f"Running headless with {rom.name}...")
    print(f"Capturing frame at {at_ms}ms...")
    
    result = subprocess.run(
        [
            str(aioserver_path()),
            "--headless",
            "--rom", str(rom),
            "--headless-max-ms", str(max_ms),
            "--headless-dump-ppm", str(ppm_output),
            "--headless-dump-ms", str(at_ms),
            "--headless-assert-nonblack",
        ],
        capture_output=True,
        text=True,
    )
    
    if result.returncode != 0:
        print(f"✗ Headless run failed (exit {result.returncode})")
        if "black" in result.stderr.lower() or "black" in result.stdout.lower():
            print("  Frame appears to be entirely black")
        return 1
    
    if not ppm_output.exists():
        print("✗ Frame dump not created")
        return 1
    
    # Convert PPM to PNG if ImageMagick is available
    if shutil.which("convert"):
        print(f"Converting to PNG...")
        subprocess.run(["convert", str(ppm_output), str(png_output)], check=True)
        ppm_output.unlink()  # Remove PPM
        print(f"✓ Captured: {png_output}")
        print(f"  Size: {png_output.stat().st_size:,} bytes")
    else:
        print(f"✓ Captured: {ppm_output}")
        print(f"  Size: {ppm_output.stat().st_size:,} bytes")
        print("  (Install ImageMagick to auto-convert to PNG)")
    
    return 0


def cmd_run_inputs(args: argparse.Namespace) -> int:
    """Run with input script and optionally capture result."""
    rom = Path(args.rom).expanduser() if args.rom else default_rom()
    script = Path(args.script).expanduser() if args.script else workspace_root() / "test_inputs" / "boot_and_start.input"
    capture_at = int(args.capture_at) if args.capture_at else None
    
    if not rom.exists():
        print(f"ROM not found: {rom}")
        return 1
    
    if not script.exists():
        print(f"Input script not found: {script}")
        return 1
    
    if not aioserver_path().exists():
        print("AIOServer not built. Run 'make build' first.")
        return 1
    
    kill_aioserver()
    
    if capture_at:
        # Headless mode with frame capture
        out_dir = ensure_output_dir()
        ppm_output = out_dir / f"inputs_{timestamp()}.ppm"
        png_output = out_dir / f"inputs_{timestamp()}.png"
        max_ms = capture_at + 2000
        
        print(f"Running headless with {rom.name}...")
        print(f"Input script: {script.name}")
        print(f"Capturing at {capture_at}ms...")
        
        result = subprocess.run(
            [
                str(aioserver_path()),
                "--headless",
                "--rom", str(rom),
                "--input-script", str(script),
                "--headless-max-ms", str(max_ms),
                "--headless-dump-ppm", str(ppm_output),
                "--headless-dump-ms", str(capture_at),
            ],
            capture_output=True,
            text=True,
        )
        
        if not ppm_output.exists():
            print("✗ Frame dump not created")
            return 1
        
        if shutil.which("convert"):
            subprocess.run(["convert", str(ppm_output), str(png_output)], check=True)
            ppm_output.unlink()
            print(f"✓ Captured: {png_output}")
        else:
            print(f"✓ Captured: {ppm_output}")
        
        return 0
    else:
        # GUI mode - just run
        print(f"Starting AIOServer with {rom.name}...")
        print(f"Input script: {script.name}")
        print("Press Ctrl+C to stop.")
        
        try:
            subprocess.run(
                [str(aioserver_path()), "--rom", str(rom), "--input-script", str(script)],
            )
        except KeyboardInterrupt:
            print("\nStopped.")
        
        return 0


def cmd_compare(args: argparse.Namespace) -> int:
    """Compare two images and generate a diff."""
    before = Path(args.before).expanduser()
    after = Path(args.after).expanduser()
    output = Path(args.output) if args.output else ensure_output_dir() / f"diff_{timestamp()}.png"
    
    if not before.exists():
        print(f"Before image not found: {before}")
        return 1
    
    if not after.exists():
        print(f"After image not found: {after}")
        return 1
    
    if not shutil.which("compare"):
        print("ImageMagick 'compare' command not found. Install with: brew install imagemagick")
        return 1
    
    print(f"Comparing {before.name} vs {after.name}...")
    
    # Generate diff image
    subprocess.run(["compare", str(before), str(after), str(output)], check=False)
    
    # Get diff statistics
    result = subprocess.run(
        ["compare", "-metric", "AE", str(before), str(after), "null:"],
        capture_output=True,
        text=True,
    )
    
    diff_pixels = result.stderr.strip() if result.stderr else "unknown"
    
    print(f"✓ Diff image: {output}")
    print(f"  Different pixels: {diff_pixels}")
    
    return 0


def cmd_audio_check(args: argparse.Namespace) -> int:
    """Run the app and check audio statistics."""
    rom = Path(args.rom).expanduser() if args.rom else default_rom()
    duration = float(args.duration)
    
    if not rom.exists():
        print(f"ROM not found: {rom}")
        return 1
    
    if not aioserver_path().exists():
        print("AIOServer not built. Run 'make build' first.")
        return 1
    
    kill_aioserver()
    
    out_dir = ensure_output_dir()
    log_file = out_dir / f"audio_trace_{timestamp()}.log"
    audio_wav = out_dir / f"audio_dump_{timestamp()}.wav"
    
    print(f"Starting AIOServer with audio recording...")
    print(f"ROM: {rom.name}")
    print(f"Duration: {duration} seconds")
    
    # Run with audio stats and direct audio dump
    env = os.environ.copy()
    env["AIO_TRACE_AUDIO_STATS"] = "1"
    max_ms = int(duration * 1000)
    
    print(f"\nRecording audio directly from SDL buffer to {audio_wav}...")
    
    result = subprocess.run(
        [
            str(aioserver_path()),
            "--headless",
            "--rom", str(rom),
            "--headless-max-ms", str(max_ms),
            "--dump-audio", str(audio_wav),
        ],
        capture_output=True,
        text=True,
        env=env,
    )
    
    # Analyze results
    all_output = result.stdout + result.stderr
    output_lines = all_output.split('\n')
    audio_lines = [l for l in output_lines if "audio" in l.lower() or "buffer" in l.lower()]
    underrun_lines = [l for l in output_lines if "underrun" in l.lower()]
    
    print(f"\n{'='*60}")
    print("AUDIO ANALYSIS RESULTS")
    print(f"{'='*60}")
    print(f"Total log lines: {len(output_lines)}")
    print(f"Audio-related lines: {len(audio_lines)}")
    print(f"Underrun warnings: {len(underrun_lines)}")
    
    if underrun_lines:
        print(f"\n⚠️  AUDIO UNDERRUNS DETECTED:")
        for line in underrun_lines[:5]:
            print(f"  {line.rstrip()}")
        if len(underrun_lines) > 5:
            print(f"  ... and {len(underrun_lines) - 5} more")
    else:
        print(f"\n✓ No audio underruns detected")
    
    print(f"\n📁 Files created:")
    if audio_wav.exists():
        print(f"   Audio WAV: {audio_wav}")
        print(f"   Size: {audio_wav.stat().st_size:,} bytes")
        duration_s = audio_wav.stat().st_size / (2 * 2 * 32768)  # 16-bit stereo 32768Hz
        print(f"   Duration: {duration_s:.2f} seconds")
    
    print(f"\n💡 TIP: Play the .wav file to hear the actual emulator audio output!")
    
    return 0


def cmd_av_record(args: argparse.Namespace) -> int:
    """Record audio/video with internal audio capture, then combine."""
    rom = Path(args.rom).expanduser() if args.rom else default_rom()
    duration = float(args.duration)
    
    out_dir = ensure_output_dir()
    ts = timestamp()
    video_file = out_dir / f"temp_video_{ts}.mov"
    audio_file = out_dir / f"temp_audio_{ts}.wav"
    output = Path(args.output) if args.output else out_dir / f"av_recording_{ts}.mp4"
    
    if not rom.exists():
        print(f"ROM not found: {rom}")
        return 1
    
    if not aioserver_path().exists():
        print("AIOServer not built. Run 'make build' first.")
        return 1
    
    # Check for ffmpeg (needed for encoding)
    if not shutil.which("ffmpeg"):
        print("ffmpeg not found. Install with: brew install ffmpeg")
        return 1
    
    kill_aioserver()
    
    print(f"Recording audio+video for {duration} seconds...")
    print(f"ROM: {rom.name}")
    print(f"Output: {output}")
    print()
    
    # Use the new --record-av option which captures directly from emulator buffers
    # This gives perfect A/V sync and works cross-platform (no screencapture dependency)
    max_ms = int(duration * 1000)
    
    print("Recording directly from emulator framebuffer and audio buffer...")
    print("  (No screen capture - frame-accurate, cross-platform)")
    
    result = subprocess.run(
        [
            str(aioserver_path()),
            "--headless",
            "--rom", str(rom),
            "--headless-max-ms", str(max_ms),
            "--record-av", str(output),
        ],
        capture_output=True,
        text=True,
    )
    
    if result.returncode != 0:
        print(f"✗ Recording failed (exit {result.returncode})")
        if result.stderr:
            print(result.stderr[-500:] if len(result.stderr) > 500 else result.stderr)
        return 1
    
    if output.exists():
        print(f"\n✓ Created: {output}")
        print(f"  Size: {output.stat().st_size:,} bytes")
        print(f"\n🎬 This recording contains ACTUAL EMULATOR AUDIO captured from the SDL buffer!")
        return 0
    else:
        print("✗ Output file not created")
        return 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Visual Development Testing Tool for AIO Server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    subparsers = parser.add_subparsers(dest="command", required=True)
    
    # capture command
    p_capture = subparsers.add_parser("capture", help="Capture screenshot of running app")
    p_capture.add_argument("--rom", help="ROM file to load")
    p_capture.add_argument("--wait", default="3", help="Seconds to wait before capture (default: 3)")
    
    # record command
    p_record = subparsers.add_parser("record", help="Record video of running app")
    p_record.add_argument("--rom", help="ROM file to load")
    p_record.add_argument("--duration", default="10", help="Recording duration in seconds (default: 10)")
    
    # frame command
    p_frame = subparsers.add_parser("frame", help="Capture emulator framebuffer (headless)")
    p_frame.add_argument("--rom", help="ROM file to load")
    p_frame.add_argument("--at-ms", default="3000", help="Capture frame at this millisecond (default: 3000)")
    p_frame.add_argument("--output", help="Output file path")
    
    # run-inputs command
    p_inputs = subparsers.add_parser("run-inputs", help="Run with input script")
    p_inputs.add_argument("--rom", help="ROM file to load")
    p_inputs.add_argument("--script", help="Input script file")
    p_inputs.add_argument("--capture-at", help="Capture frame at this ms (enables headless mode)")
    
    # compare command
    p_compare = subparsers.add_parser("compare", help="Compare two images")
    p_compare.add_argument("before", help="Before image")
    p_compare.add_argument("after", help="After image")
    p_compare.add_argument("--output", help="Output diff image path")
    
    # audio-check command
    p_audio = subparsers.add_parser("audio-check", help="Run app and check audio output")
    p_audio.add_argument("--rom", help="ROM file to load")
    p_audio.add_argument("--duration", default="5", help="Duration in seconds (default: 5)")
    
    # av-record command (NEW - combines screen capture with internal audio)
    p_av = subparsers.add_parser("av-record", help="Record video+audio (uses internal audio capture)")
    p_av.add_argument("--rom", help="ROM file to load")
    p_av.add_argument("--duration", default="10", help="Recording duration in seconds (default: 10)")
    p_av.add_argument("--output", help="Output file path (default: test_output/av_recording_*.mp4)")
    
    args = parser.parse_args()
    
    if args.command == "capture":
        return cmd_capture(args)
    elif args.command == "record":
        return cmd_record(args)
    elif args.command == "frame":
        return cmd_frame(args)
    elif args.command == "run-inputs":
        return cmd_run_inputs(args)
    elif args.command == "compare":
        return cmd_compare(args)
    elif args.command == "audio-check":
        return cmd_audio_check(args)
    elif args.command == "av-record":
        return cmd_av_record(args)
    else:
        parser.print_help()
        return 1


if __name__ == "__main__":
    sys.exit(main())
