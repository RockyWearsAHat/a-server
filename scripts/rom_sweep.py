#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional


@dataclass
class RomResult:
  rom: Path
  exit_code: int
  crash_log: Optional[Path]


def _workspace_root() -> Path:
  return Path(__file__).resolve().parent.parent


def _build_bin(root: Path) -> Path:
  return root / "build" / "bin"


def _run_aioserver(
  root: Path,
  aioserver: Path,
  rom: Path,
  timeout_s: float,
) -> RomResult:
  crash_log = root / "crash_log.txt"

  # Remove any previous crash log so we only see crashes from this run.
  if crash_log.exists():
    try:
      crash_log.unlink()
    except OSError:
      pass

  cmd = [
    str(aioserver),
    "--headless",
    "--exit-on-crash",
    "--headless-max-ms",
    str(int(timeout_s * 1000.0)),
    "--rom",
    str(rom),
  ]

  print(f"[RUN] {rom.name}")
  print("cmd:", " ".join(cmd))
  sys.stdout.flush()

  # Inherit environment; caller can set AIO_LOG_* or tracing flags if desired.
  proc = subprocess.run(cmd, cwd=str(root))
  exit_code = int(proc.returncode)

  crash_copy: Optional[Path] = None
  if crash_log.exists() and crash_log.stat().st_size > 0:
    # Store per-ROM crash log under dumps/rom_sweep/.
    dumps_dir = root / "dumps" / "rom_sweep"
    dumps_dir.mkdir(parents=True, exist_ok=True)
    crash_copy = dumps_dir / f"{rom.stem}.crash_log.txt"
    try:
      shutil.copy2(crash_log, crash_copy)
      print(f"  -> crash_log captured at {crash_copy}")
    except OSError as e:
      print(f"  !! failed to copy crash_log.txt for {rom.name}: {e}")

  return RomResult(rom=rom, exit_code=exit_code, crash_log=crash_copy)


def main(argv: Optional[List[str]] = None) -> int:
  p = argparse.ArgumentParser(
    description=(
      "Sweep all GBA ROMs with AIOServer in headless mode, "
      "reporting crashes and capturing per-ROM crash logs."
    )
  )
  p.add_argument(
    "--roms-dir",
    default=str(Path.home() / "Desktop" / "ROMs" / "GBA"),
    help="Directory containing .gba files to test (default: ~/Desktop/ROMs/GBA)",
  )
  p.add_argument(
    "--timeout-s",
    type=float,
    default=10.0,
    help="Headless run time per ROM in seconds (default: 10)",
  )
  p.add_argument(
    "--aioserver",
    default="",
    help="Optional explicit path to AIOServer binary (defaults to build/bin/AIOServer)",
  )

  args = p.parse_args(argv)

  root = _workspace_root()
  bin_dir = _build_bin(root)

  aioserver_path = Path(args.aioserver) if args.aioserver else (bin_dir / "AIOServer")
  if not aioserver_path.exists():
    print(f"AIOServer not found at {aioserver_path}. Build the project first.")
    return 1

  rom_dir = Path(args.roms_dir).expanduser()
  if not rom_dir.exists() or not rom_dir.is_dir():
    print(f"ROM directory does not exist or is not a directory: {rom_dir}")
    return 1

  roms = sorted(r for r in rom_dir.glob("*.gba") if r.is_file())
  if not roms:
    print(f"No .gba ROMs found in {rom_dir}")
    return 0

  print(f"Found {len(roms)} ROM(s) in {rom_dir}")

  results: List[RomResult] = []
  for rom in roms:
    res = _run_aioserver(root, aioserver_path, rom, timeout_s=float(args.timeout_s))
    results.append(res)

  failed = [r for r in results if r.exit_code != 0]

  print("=" * 80)
  print(f"ROM sweep complete: {len(results)} tested, {len(failed)} failed.")

  if failed:
    print("Failures:")
    for r in failed:
      line = f"- {r.rom.name}: exit={r.exit_code}"
      if r.crash_log is not None:
        line += f", crash_log={r.crash_log}"
      print(line)
    return 1

  print("All ROMs exited cleanly within the timeout.")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
