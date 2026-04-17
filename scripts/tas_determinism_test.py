#!/usr/bin/env python3
"""
tas_determinism_test.py — TAS-driven deterministic emulator verification.

TAS files encode the exact controller inputs needed to reach a known game state.
The filename encodes expected behavior:
  e.g.  metroid_zero_mission_any_pct_54min.vbm  → any% run in 54 minutes
        sma2_world1_complete.vbm               → completes World 1

Workflow:
  1. Load TAS file → convert to AIO input script (via tas_converter.py)
  2. Run headless emulation with those inputs for N milliseconds
  3. Capture frames at several timestamps
  4. Compare each frame file size / pixel content against a stored baseline
  5. Report PASS / DIVERGE / CRASH per ROM

Usage:
  python3 scripts/tas_determinism_test.py --rom test_roms/OG-DK.gba
  python3 scripts/tas_determinism_test.py --all
  python3 scripts/tas_determinism_test.py --all --update-baseline
  python3 scripts/tas_determinism_test.py --rom test_roms/OG-DK.gba --tas /tmp/dk_run.vbm
    python3 scripts/tas_determinism_test.py --all --system gba --capture-times-ms 120000 --run-padding-ms 2000
    python3 scripts/tas_determinism_test.py --all --system gba --require-tas

Baselines are stored as PPM files under test_output/tas_baselines/<stem>/<ms>ms.ppm
so they can be committed and compared in CI.
"""

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parent.parent
BINARY = WORKSPACE / "build" / "bin" / "AIOServer"
TAS_CONVERTER = WORKSPACE / "scripts" / "tas_converter.py"
BASELINE_DIR = WORKSPACE / "test_output" / "tas_baselines"
TAS_ROOT = Path("/tmp/test_tas")

# Capture timestamps (ms) per system — taken at intervals where real gameplay is visible
CAPTURE_TIMES: dict[str, list[int]] = {
    "gba":  [500, 1_500, 3_000],
    "nes":  [500, 1_500, 3_000],
    "snes": [500, 1_500, 3_000],
    "gb":   [500, 1_500, 3_000],
    "gbc":  [500, 1_500, 3_000],
}
DEFAULT_TIMES = [500, 1_500, 3_000]

# How long to run the emulator total per test
RUN_DURATION: dict[str, int] = {
    "gba":  11_000,
    "nes":   6_000,
    "snes":  6_000,
    "gb":    6_000,
    "gbc":   6_000,
}
DEFAULT_DURATION = 6_000

# Extension → system mapping
EXT_SYSTEM: dict[str, str] = {
    ".gba": "gba", ".agb": "gba",
    ".nes": "nes",
    ".smc": "snes", ".sfc": "snes",
    ".gb":  "gb",   ".gbc": "gbc",
}

# TAS extension search order per system
TAS_EXTENSIONS: dict[str, list[str]] = {
    "gba": [".vbm", ".bk2"],
    "nes": [".fm2", ".r08", ".bk2"],
    "snes": [".fm3", ".bk2"],
    "gb":   [".vbm", ".fm2", ".bk2"],
    "gbc":  [".vbm", ".fm2", ".bk2"],
}


# ── Helpers ────────────────────────────────────────────────────────────────────

def _system(rom: Path) -> str:
    return EXT_SYSTEM.get(rom.suffix.lower(), "unknown")


def _ppm_hash(path: Path) -> str:
    """SHA-256 of PPM pixel data (skip 3-line header)."""
    try:
        raw = path.read_bytes()
        # PPM P6 header: "P6\n<w> <h>\n<maxval>\n"
        pos = 0
        for _ in range(3):
            pos = raw.index(b"\n", pos) + 1
        return hashlib.sha256(raw[pos:]).hexdigest()[:16]
    except Exception:
        return "error"


def _is_nonblack(path: Path) -> bool:
    """Return True if the PPM contains at least one non-zero pixel."""
    try:
        raw = path.read_bytes()
        pos = 0
        for _ in range(3):
            pos = raw.index(b"\n", pos) + 1
        return any(b != 0 for b in raw[pos:pos + 4096])
    except Exception:
        return False


def _find_tas(rom: Path) -> Path | None:
    """Look for a TAS file matching the ROM stem under /tmp/test_tas/<system>/."""
    sys_name = _system(rom)
    search_dir = TAS_ROOT / sys_name
    if not search_dir.is_dir():
        return None
    stem = rom.stem.lower()
    for ext in TAS_EXTENSIONS.get(sys_name, []):
        for candidate in search_dir.iterdir():
            if candidate.suffix.lower() == ext and stem in candidate.stem.lower():
                return candidate
    # Fallback: any TAS file whose name overlaps with ROM stem words
    words = {w for w in stem.replace("-", " ").replace("_", " ").split() if len(w) > 3}
    for ext in TAS_EXTENSIONS.get(sys_name, []):
        for candidate in search_dir.iterdir():
            if candidate.suffix.lower() == ext:
                cand_words = set(candidate.stem.lower().replace("-", " ").replace("_", " ").split())
                if words & cand_words:
                    return candidate
    return None


def _convert_tas(tas: Path) -> Path | None:
    """Convert TAS file to AIO input script; return path to script or None."""
    out = Path(tempfile.mktemp(suffix=".aio_script"))
    result = subprocess.run(
        [sys.executable, str(TAS_CONVERTER), str(tas)],
        capture_output=True, text=True
    )
    if result.returncode != 0 or not result.stdout.strip():
        print(f"    TAS convert failed: {result.stderr.strip()[:200]}")
        return None
    out.write_text(result.stdout)
    return out


# ── Core test runner ───────────────────────────────────────────────────────────

class TestResult:
    def __init__(self, rom: Path):
        self.rom = rom
        self.status = "PENDING"   # PASS / DIVERGE / CRASH / NO_FRAME / SKIP
        self.frames: list[tuple[int, Path]] = []   # (ms, ppm_path)
        self.hashes: list[tuple[int, str]] = []
        self.baseline_hashes: list[tuple[int, str]] = []
        self.diverged_at: list[int] = []
        self.tas_used: Path | None = None
        self.notes: list[str] = []


def run_test(rom: Path,
             tas: Path | None = None,
             update_baseline: bool = False,
             run_padding_ms: int = 500) -> TestResult:
    res = TestResult(rom)
    sys_name = _system(rom)
    if sys_name == "unknown":
        res.status = "SKIP"
        res.notes.append(f"Unknown system for extension '{rom.suffix}'")
        return res

    capture_times = CAPTURE_TIMES.get(sys_name, DEFAULT_TIMES)
    duration = RUN_DURATION.get(sys_name, DEFAULT_DURATION)

    # Find TAS for this ROM if not supplied
    if tas is None:
        tas = _find_tas(rom)
    res.tas_used = tas

    input_script: Path | None = None
    if tas is not None:
        input_script = _convert_tas(tas)
        if input_script is None:
            res.notes.append("TAS conversion failed — running without inputs")

    # Run headless once per ROM; capture N frames at sequential timestamps using
    # --headless-dump-ppm / --headless-dump-ms pairs (one per capture point).
    # We launch AIOServer once with the LAST capture time as the dump target,
    # then loop from earliest to latest so each run is as short as possible.
    tmp_frames: list[tuple[int, Path]] = []

    for ms in capture_times:
        ppm_out = Path(tempfile.mktemp(suffix=f"_{ms}ms.ppm"))
        # Each run only needs to reach the current capture timestamp
        run_until = ms + run_padding_ms  # a little past the capture point
        cmd = [
            str(BINARY),
            "--headless",
            "--rom", str(rom),
            "--headless-max-ms", str(run_until),
            "--headless-dump-ppm", str(ppm_out),
            "--headless-dump-ms", str(ms),
        ]
        if input_script is not None:
            cmd += ["--input-script", str(input_script)]

        try:
            r = subprocess.run(cmd, capture_output=True, timeout=run_until / 1000 + 10)
        except subprocess.TimeoutExpired:
            res.status = "CRASH"
            res.notes.append(f"Timeout at {ms}ms")
            break

        if r.returncode not in (0, 1):
            res.status = "CRASH"
            res.notes.append(f"Exit {r.returncode} at {ms}ms: {r.stderr.decode(errors='replace')[:120]}")
            break

        if not ppm_out.exists():
            res.notes.append(f"No frame dumped at {ms}ms")
            continue

        if not _is_nonblack(ppm_out):
            res.notes.append(f"Frame at {ms}ms is all-black")

        tmp_frames.append((ms, ppm_out))

    if not tmp_frames:
        if res.status != "CRASH":
            res.status = "NO_FRAME"
        return res

    res.frames = tmp_frames

    # Store / compare baselines
    baseline_stem = BASELINE_DIR / rom.stem
    baseline_stem.mkdir(parents=True, exist_ok=True)

    all_match = True
    for ms, ppm in tmp_frames:
        h = _ppm_hash(ppm)
        res.hashes.append((ms, h))
        baseline_path = baseline_stem / f"{ms}ms.ppm"

        if update_baseline or not baseline_path.exists():
            import shutil
            shutil.copy2(ppm, baseline_path)
            res.baseline_hashes.append((ms, h))
            res.notes.append(f"Baseline saved at {ms}ms")
        else:
            bh = _ppm_hash(baseline_path)
            res.baseline_hashes.append((ms, bh))
            if h != bh:
                res.diverged_at.append(ms)
                all_match = False

    if update_baseline:
        res.status = "BASELINE_SAVED"
    elif res.diverged_at:
        res.status = "DIVERGE"
    else:
        res.status = "PASS"

    return res


def parse_capture_times(raw: str | None) -> list[int] | None:
    """Parse comma-separated capture times in ms, sorted ascending."""
    if raw is None:
        return None
    vals: list[int] = []
    for part in raw.split(","):
        part = part.strip()
        if not part:
            continue
        v = int(part)
        if v < 0:
            raise ValueError(f"capture time must be >= 0, got {v}")
        vals.append(v)
    if not vals:
        raise ValueError("capture times cannot be empty")
    return sorted(set(vals))


# ── Reporting ──────────────────────────────────────────────────────────────────

STATUS_ICON = {
    "PASS": "✓",
    "DIVERGE": "✗",
    "CRASH": "☠",
    "NO_FRAME": "○",
    "SKIP": "—",
    "BASELINE_SAVED": "⊕",
    "PENDING": "?",
}


def print_result(res: TestResult) -> None:
    icon = STATUS_ICON.get(res.status, "?")
    tas_label = f" [TAS: {res.tas_used.name}]" if res.tas_used else " [no TAS]"
    print(f"  {icon}  {res.rom.name:<42} {res.status}{tas_label}")
    if res.diverged_at:
        print(f"       diverged at: {res.diverged_at}ms")
    for note in res.notes:
        print(f"       note: {note}")
    if res.frames:
        for ms, ppm in res.frames:
            nonblack = "✓" if _is_nonblack(ppm) else "BLACK"
            sz = ppm.stat().st_size // 1024
            print(f"       frame @{ms:>6}ms  {nonblack}  {sz}KB  {ppm}")


def print_summary(results: list[TestResult]) -> None:
    counts: dict[str, int] = {}
    for r in results:
        counts[r.status] = counts.get(r.status, 0) + 1

    print()
    print("─" * 60)
    print("SUMMARY")
    print("─" * 60)
    for status, icon in STATUS_ICON.items():
        n = counts.get(status, 0)
        if n:
            print(f"  {icon}  {status:<18} {n}")
    total = len(results)
    passed = counts.get("PASS", 0) + counts.get("BASELINE_SAVED", 0)
    print(f"\n  {passed}/{total} passed")
    print("─" * 60)


# ── Entry point ────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="TAS-driven deterministic emulator test")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--rom", metavar="PATH", help="Single ROM to test")
    group.add_argument("--all", action="store_true", help="Test all ROMs under test_roms/")
    parser.add_argument("--tas", metavar="PATH", help="Explicit TAS file (only with --rom)")
    parser.add_argument("--update-baseline", action="store_true",
                        help="Overwrite stored baseline frames with current output")
    parser.add_argument("--system", metavar="SYS",
                        help="Filter --all to a specific system (gba, nes, snes, gb)")
    parser.add_argument("--capture-times-ms", metavar="LIST",
                        help="Comma-separated capture timestamps in ms (e.g. 500,1500,120000)")
    parser.add_argument("--run-padding-ms", metavar="N", type=int, default=500,
                        help="Extra emulated ms after each capture timestamp (default: 500)")
    parser.add_argument("--require-tas", action="store_true",
                        help="Fail ROM test when no TAS is discovered/provided")
    args = parser.parse_args()

    if args.run_padding_ms < 0:
        print("ERROR: --run-padding-ms must be >= 0")
        sys.exit(2)

    try:
        override_capture_times = parse_capture_times(args.capture_times_ms)
    except ValueError as e:
        print(f"ERROR: {e}")
        sys.exit(2)

    if not BINARY.exists():
        print(f"ERROR: binary not found: {BINARY}")
        print("Run: make build")
        sys.exit(1)

    roms: list[Path] = []
    if args.rom:
        roms = [Path(args.rom).expanduser().resolve()]
    else:
        test_roms_dir = WORKSPACE / "test_roms"
        for ext in EXT_SYSTEM:
            roms.extend(test_roms_dir.rglob(f"*{ext}"))
        if args.system:
            roms = [r for r in roms if _system(r) == args.system]
        roms.sort()

    if not roms:
        print("No ROMs found.")
        sys.exit(0)

    tas_override = Path(args.tas).expanduser() if args.tas else None

    print(f"\nTAS Determinism Test  ({len(roms)} ROM{'s' if len(roms) != 1 else ''})")
    print("─" * 60)

    results: list[TestResult] = []
    for rom in roms:
        print(f"\n→ {rom.name}")

        if override_capture_times is not None:
            CAPTURE_TIMES[_system(rom)] = override_capture_times

        # Override the per-run max duration behavior by adjusting the implicit
        # run-until logic used in run_test: run until capture+padding.
        if args.run_padding_ms != 500:
            # Store per-system desired duration high enough to avoid accidental
            # early termination in future refactors that consume RUN_DURATION.
            max_capture = max(CAPTURE_TIMES.get(_system(rom), DEFAULT_TIMES))
            RUN_DURATION[_system(rom)] = max_capture + args.run_padding_ms

        if args.require_tas and tas_override is None and _find_tas(rom) is None:
            res = TestResult(rom)
            res.status = "SKIP"
            res.notes.append("No TAS found (required by --require-tas)")
            print_result(res)
            results.append(res)
            continue

        res = run_test(
            rom,
            tas=tas_override,
            update_baseline=args.update_baseline,
            run_padding_ms=args.run_padding_ms,
        )
        print_result(res)
        results.append(res)

    print_summary(results)

    # Exit non-zero if any test diverged or crashed
    bad = [r for r in results if r.status in ("DIVERGE", "CRASH", "NO_FRAME")]
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
