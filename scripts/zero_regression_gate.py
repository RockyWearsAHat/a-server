#!/usr/bin/env python3
"""
Zero-regression verification gate for emulator correctness.

This script enforces a strict evidence contract:
1) Build must pass.
2) Full native ctest suite must pass.
3) GBA TAS determinism must pass (and be repeatable run-to-run).
4) Every launchable emulator must have at least one available ROM smoke run.

It exits non-zero on any failed/missing evidence item.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

WORKSPACE = Path(__file__).resolve().parent.parent
AIO_BIN = WORKSPACE / "build" / "bin" / "AIOServer"
TEST_ROMS = WORKSPACE / "test_roms"

LAUNCHABLE_FORMATS: Dict[str, Tuple[str, ...]] = {
    "GBA": (".gba",),
    "PS1": (".bin", ".cue", ".iso", ".img", ".chd", ".pbp"),
    "Atari2600": (".a26",),
    "NES": (".nes",),
    "Genesis": (".md", ".gen", ".smd"),
    "SNES": (".smc", ".sfc", ".fig", ".swc"),
    "GameBoy": (".gb", ".gbc"),
}


@dataclass
class GateResult:
    name: str
    passed: bool
    details: List[str] = field(default_factory=list)


def run_cmd(cmd: Sequence[str], cwd: Path, timeout: int) -> Tuple[int, str]:
    try:
        proc = subprocess.run(
            list(cmd),
            cwd=str(cwd),
            text=True,
            capture_output=True,
            timeout=timeout,
        )
        out = (proc.stdout or "") + (proc.stderr or "")
        return proc.returncode, out
    except subprocess.TimeoutExpired as exc:
        out = (exc.stdout or "") + (exc.stderr or "")
        out += f"\n[TIMEOUT] command exceeded {timeout}s\n"
        return 124, out


def hash_ppm_pixel_data(ppm_path: Path) -> str:
    raw = ppm_path.read_bytes()
    pos = 0
    for _ in range(3):
        pos = raw.index(b"\n", pos) + 1
    return hashlib.sha256(raw[pos:]).hexdigest()[:16]


def parse_ppm_paths(output: str) -> Dict[int, Path]:
    found: Dict[int, Path] = {}
    for line in output.splitlines():
        m = re.search(r"(/var/.+?_((500|1500|3000)ms)\\.ppm)", line)
        if m:
            ms = int(m.group(2).replace("ms", ""))
            found[ms] = Path(m.group(1))
    return found


def list_roms_for_extensions(exts: Tuple[str, ...]) -> List[Path]:
    if not TEST_ROMS.exists():
        return []
    exts_lower = {e.lower() for e in exts}
    roms: List[Path] = []
    for p in sorted(TEST_ROMS.iterdir()):
        if p.is_file() and p.suffix.lower() in exts_lower:
            roms.append(p)
    return roms


def gate_build(skip: bool) -> GateResult:
    if skip:
        return GateResult("build", True, ["skipped via --skip-build"])
    code, out = run_cmd(["make", "build"], WORKSPACE, timeout=1200)
    tail = "\n".join(out.splitlines()[-20:])
    return GateResult("build", code == 0, [tail])


def gate_ctest(skip: bool) -> GateResult:
    if skip:
        return GateResult("ctest", True, ["skipped via --skip-ctest"])
    code, out = run_cmd(
        ["ctest", "--output-on-failure"],
        WORKSPACE / "build" / "generated" / "cmake",
        timeout=1800,
    )
    summary = [
        line
        for line in out.splitlines()
        if "tests passed" in line.lower() or "tests failed" in line.lower()
    ]
    details = summary[-3:] if summary else ["no ctest summary parsed"]
    if code != 0:
        details.append("ctest failed")
    return GateResult("ctest", code == 0, details)


def gate_gba_tas(skip: bool) -> GateResult:
    if skip:
        return GateResult("gba_tas", True, ["skipped via --skip-gba-tas"])
    code, out = run_cmd(
        ["python3", "scripts/tas_determinism_test.py", "--system", "gba", "--all"],
        WORKSPACE,
        timeout=1800,
    )
    lines = out.splitlines()
    summary = [l for l in lines if "PASS" in l or "DIVERGE" in l or "SUMMARY" in l]
    details = summary[-12:] if summary else lines[-20:]
    return GateResult("gba_tas", code == 0, details)


def gate_gba_repeatability(skip: bool) -> GateResult:
    if skip:
        return GateResult("gba_repeatability", True, ["skipped via --skip-gba-repeatability"])
    gba_roms = list_roms_for_extensions((".gba",))
    if not gba_roms:
        return GateResult("gba_repeatability", False, ["no .gba ROMs found in test_roms/"])

    details: List[str] = []
    all_ok = True

    for rom in gba_roms:
        hashes_per_run: List[Dict[int, str]] = []
        for _ in range(2):
            code, out = run_cmd(
                [
                    "python3",
                    "scripts/tas_determinism_test.py",
                    "--system",
                    "gba",
                    "--rom",
                    str(rom),
                ],
                WORKSPACE,
                timeout=600,
            )
            if code != 0:
                all_ok = False
                details.append(f"{rom.name}: tas run failed (exit {code})")
                continue
            ppm_by_ms = parse_ppm_paths(out)
            if not ppm_by_ms:
                all_ok = False
                details.append(f"{rom.name}: no captured ppm paths parsed")
                continue
            hashes = {ms: hash_ppm_pixel_data(path) for ms, path in ppm_by_ms.items()}
            hashes_per_run.append(hashes)

        if len(hashes_per_run) == 2:
            run1, run2 = hashes_per_run
            if run1 != run2:
                all_ok = False
                details.append(f"{rom.name}: nondeterministic hashes run1={run1} run2={run2}")
            else:
                details.append(f"{rom.name}: deterministic hashes {run1}")

    return GateResult("gba_repeatability", all_ok, details)


def gate_launchable_smoke(skip: bool, allow_missing: bool) -> GateResult:
    if skip:
        return GateResult("launchable_smoke", True, ["skipped via --skip-smoke"])
    if not AIO_BIN.exists():
        return GateResult("launchable_smoke", False, [f"binary missing: {AIO_BIN}"])

    details: List[str] = []
    missing: List[str] = []
    failed: List[str] = []

    for platform, exts in LAUNCHABLE_FORMATS.items():
        roms = list_roms_for_extensions(exts)
        if not roms:
            missing.append(f"{platform}: no ROM for extensions {exts}")
            continue

        rom = roms[0]
        ppm = Path(tempfile.mktemp(suffix=f"_{platform}.ppm"))
        cmd = [
            str(AIO_BIN),
            "--headless",
            "--rom",
            str(rom),
            "--headless-max-ms",
            "1200",
            "--headless-dump-ppm",
            str(ppm),
            "--headless-dump-ms",
            "800",
            "--headless-assert-nonblack",
        ]
        code, out = run_cmd(cmd, WORKSPACE, timeout=90)
        if code == 0:
            details.append(f"{platform}: PASS using {rom.name}")
        else:
            failed.append(f"{platform}: FAIL using {rom.name} (exit {code})")
            details.extend(out.splitlines()[-8:])

    passed = len(failed) == 0 and (allow_missing or len(missing) == 0)
    details.extend(missing)
    details.extend(failed)

    return GateResult("launchable_smoke", passed, details)


def print_result(r: GateResult) -> None:
    icon = "PASS" if r.passed else "FAIL"
    print(f"[{icon}] {r.name}")
    for line in r.details[:40]:
        print(f"  - {line}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Strict zero-regression gate")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--skip-ctest", action="store_true")
    ap.add_argument("--skip-gba-tas", action="store_true")
    ap.add_argument("--skip-gba-repeatability", action="store_true")
    ap.add_argument("--skip-smoke", action="store_true")
    ap.add_argument(
        "--allow-missing-rom-evidence",
        action="store_true",
        help="Do not fail solely because some launchable platforms have no ROMs in test_roms/",
    )
    args = ap.parse_args()

    gates = [
        gate_build(args.skip_build),
        gate_ctest(args.skip_ctest),
        gate_gba_tas(args.skip_gba_tas),
        gate_gba_repeatability(args.skip_gba_repeatability),
        gate_launchable_smoke(args.skip_smoke, args.allow_missing_rom_evidence),
    ]

    print("\n=== ZERO REGRESSION GATE REPORT ===")
    for g in gates:
        print_result(g)

    failed = [g.name for g in gates if not g.passed]
    if failed:
        print("\nOVERALL: FAIL")
        print("Failed gates:", ", ".join(failed))
        return 1

    print("\nOVERALL: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
