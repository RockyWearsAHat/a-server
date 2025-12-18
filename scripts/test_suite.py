#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


@dataclass(frozen=True)
class TestCase:
    id: str  # system.test
    system: str
    name: str
    cmd: List[str]
    cwd: Path
    timeout_s: float
    env: Dict[str, str]


def _workspace_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _build_bin(root: Path) -> Path:
    return root / "build" / "bin"


def _sanitize_test_token(s: str) -> str:
    # Keep identifiers stable and shell-friendly.
    s = re.sub(r"[^A-Za-z0-9_-]+", "_", s)
    s = re.sub(r"_+", "_", s).strip("_")
    return s or "unnamed"


def _split_test_id(test_id: str) -> Tuple[str, str]:
    if test_id.count(".") != 1:
        raise ValueError(
            f"Invalid test id '{test_id}'. Expected format 'system.test_name' (exactly one '.')."
        )
    system, name = test_id.split(".", 1)
    system = system.strip()
    name = name.strip()
    if not system or not name:
        raise ValueError(
            f"Invalid test id '{test_id}'. Expected format 'system.test_name' (non-empty system and test name)."
        )
    return system, name


def _pick_free_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def _run(cmd: Sequence[str], cwd: Path, env: Dict[str, str], timeout_s: float) -> int:
    # Stream output live for better debugging.
    p = subprocess.Popen(
        list(cmd),
        cwd=str(cwd),
        env={**os.environ, **env},
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    start = time.time()
    assert p.stdout is not None
    try:
        for line in p.stdout:
            sys.stdout.write(line)
        return p.wait(timeout=max(0.0, timeout_s - (time.time() - start)))
    except subprocess.TimeoutExpired:
        try:
            p.kill()
        except Exception:
            pass
        return 124


def _build_test_manifest(args: argparse.Namespace) -> List[TestCase]:
    root = _workspace_root()
    bin_dir = _build_bin(root)

    # Reduce noisy Qt output in headless test runs (while preserving any user rules).
    qt_rules = os.environ.get("QT_LOGGING_RULES", "").strip()
    qt_rules = (qt_rules + ";" if qt_rules else "") + "qt.qpa.fonts=false"
    qt_headless_env = {
        "QT_LOGGING_RULES": qt_rules,
        # Ensure QtWebEngine stays off in tests unless explicitly enabled by the user.
        "AIO_ENABLE_STREAMING": "0",
    }

    cases: List[TestCase] = []

    def add(system: str, name: str, cmd: List[str], timeout_s: float, env: Optional[Dict[str, str]] = None) -> None:
        test_id = f"{system}.{name}"
        cases.append(
            TestCase(
                id=test_id,
                system=system,
                name=name,
                cmd=cmd,
                cwd=root,
                timeout_s=timeout_s,
                env=env or {},
            )
        )

    # --- Native unit tests (GoogleTest executables) ---
    cpu = bin_dir / "CPUTests"
    if cpu.exists():
        add("gba", "cpu", [str(cpu)], timeout_s=float(args.unit_timeout_s))

    eeprom = bin_dir / "EEPROMTests"
    if eeprom.exists():
        add("gba", "eeprom", [str(eeprom)], timeout_s=float(args.unit_timeout_s))

    input_logic = bin_dir / "InputLogicTests"
    if input_logic.exists():
        add("input", "logic", [str(input_logic)], timeout_s=float(args.unit_timeout_s))

    # --- App-level smoke tests (AIOServer) ---
    aioserver = bin_dir / "AIOServer"
    if aioserver.exists():
        # NAS smoke: start server, verify index loads, then let it exit via --headless-max-ms.
        # (AIOServer always creates a MainWindow, so this is still a lightweight integration test.)
        port = _pick_free_tcp_port()
        nas_root = args.nas_root or str((root / "test_save_read").resolve())
        env = {
            "AIO_NAS_ROOT": nas_root,
            **qt_headless_env,
        }

        add(
            "app",
            "nas_smoke",
            [
                str(aioserver),
                "--headless",
                "--headless-max-ms",
                str(int(args.nas_smoke_seconds * 1000)),
                "--nas-port",
                str(port),
            ],
            timeout_s=float(args.nas_smoke_seconds + 6.0),
            env=env,
        )

        # Emulator fuzz runs (GBA): run for N seconds or fail fast on crash.
        fuzz_seconds = float(args.fuzz_seconds)
        roms: List[Path] = []

        if args.rom:
            roms.extend([Path(p).expanduser() for p in args.rom])

        if args.roms_dir:
            rom_dir = Path(args.roms_dir).expanduser()
            if rom_dir.exists() and rom_dir.is_dir():
                roms.extend(sorted(rom_dir.glob("*.gba")))

        if not roms and args.auto_discover_roms:
            # Best-effort: discover ROMs in workspace root.
            roms.extend(sorted(root.glob("*.gba")))

        # De-dup
        dedup: Dict[str, Path] = {}
        for r in roms:
            try:
                rp = r.resolve()
            except Exception:
                rp = r
            dedup[str(rp)] = rp
        roms = list(dedup.values())

        for rom in roms:
            if not rom.exists():
                continue
            name = f"fuzz_{_sanitize_test_token(rom.stem)}"
            add(
                "gba",
                name,
                [
                    str(aioserver),
                    "--headless",
                    "--exit-on-crash",
                    "--headless-max-ms",
                    str(int(fuzz_seconds * 1000)),
                    "--rom",
                    str(rom),
                ],
                timeout_s=float(fuzz_seconds + 6.0),
                env=qt_headless_env,
            )

    return cases


def _apply_filters(cases: List[TestCase], only: List[str], exclude: List[str]) -> List[TestCase]:
    by_id = {c.id: c for c in cases}

    def excluded(test_id: str) -> bool:
        for token in exclude:
            token = token.strip()
            if not token:
                continue
            if "." in token:
                # Exact test exclusion
                if token == test_id:
                    return True
            else:
                # Whole-system exclusion
                if test_id.startswith(token + "."):
                    return True
        return False

    selected: List[TestCase]
    if only:
        wanted: List[TestCase] = []
        for oid in only:
            oid = oid.strip()
            if not oid:
                continue
            if "." not in oid:
                # Whole system whitelist
                for c in cases:
                    if c.id.startswith(oid + "."):
                        wanted.append(c)
                continue
            if oid in by_id:
                wanted.append(by_id[oid])
            else:
                raise SystemExit(f"Unknown test id in --only: {oid}")
        # De-dup while preserving order
        seen = set()
        selected = []
        for c in wanted:
            if c.id in seen:
                continue
            seen.add(c.id)
            selected.append(c)
    else:
        selected = list(cases)

    selected = [c for c in selected if not excluded(c.id)]
    return selected


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(
        description=(
            "AIO comprehensive test runner. "
            "Tests are addressed as system.test_name (or just system for whole-system selection/exclusion)."
        )
    )
    p.add_argument("--list", action="store_true", help="List all available tests and exit")
    p.add_argument(
        "--only",
        action="append",
        default=[],
        help=(
            "Whitelist tests to run. Repeatable. "
            "Accepts 'system' or 'system.test_name'. If provided, defaults-to-only those." 
        ),
    )
    p.add_argument(
        "--exclude",
        action="append",
        default=[],
        help=(
            "Exclude tests. Repeatable. Accepts 'system' or 'system.test_name'. "
            "System excludes everything under that system." 
        ),
    )

    p.add_argument("--unit-timeout-s", type=float, default=90.0, help="Timeout per unit-test executable")

    # NAS smoke settings
    p.add_argument("--nas-root", default="", help="Root directory to serve for NAS smoke test")
    p.add_argument("--nas-smoke-seconds", type=float, default=2.0, help="Seconds to run NAS smoke test")

    # Emulator fuzz settings
    p.add_argument("--fuzz-seconds", type=float, default=10.0, help="Seconds per emulator fuzz run")
    p.add_argument(
        "--rom",
        action="append",
        default=[],
        help="ROM path to fuzz (repeatable). If omitted, auto-discovers *.gba in workspace root by default.",
    )
    p.add_argument("--roms-dir", default="", help="Directory containing ROMs to fuzz (uses *.gba)")
    p.add_argument(
        "--no-auto-discover-roms",
        dest="auto_discover_roms",
        action="store_false",
        help="Disable auto-discovery of *.gba in workspace root",
    )
    p.set_defaults(auto_discover_roms=True)

    args = p.parse_args(argv)

    cases = _build_test_manifest(args)
    cases.sort(key=lambda c: c.id)

    if args.list:
        for c in cases:
            print(c.id)
        return 0

    selected = _apply_filters(cases, only=args.only, exclude=args.exclude)

    if not selected:
        print("No tests selected.")
        return 0

    print(f"Running {len(selected)} test(s)...")

    failed: List[Tuple[TestCase, int]] = []

    for i, tc in enumerate(selected, start=1):
        print("=" * 80)
        print(f"[{i}/{len(selected)}] {tc.id}")
        print(f"cmd: {' '.join(tc.cmd)}")
        sys.stdout.flush()

        rc = _run(tc.cmd, cwd=tc.cwd, env=tc.env, timeout_s=tc.timeout_s)
        if rc != 0:
            failed.append((tc, rc))
            print(f"[FAIL] {tc.id} (exit={rc})")
        else:
            print(f"[ OK ] {tc.id}")

    print("=" * 80)
    if failed:
        print(f"{len(failed)} test(s) failed:")
        for tc, rc in failed:
            print(f"- {tc.id} (exit={rc})")
        return 1

    print("All tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
