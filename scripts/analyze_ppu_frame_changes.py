#!/usr/bin/env python3
"""Analyze AIOServer debug.log PPU frame traces.

- Prints significant changes between consecutive [PPU_FRAME] lines.
- If [SCRIPT] markers exist, prints nearby PPU frames around each marker.

Usage:
  python3 scripts/analyze_ppu_frame_changes.py debug.log
  python3 scripts/analyze_ppu_frame_changes.py debug.log --only-changes
  python3 scripts/analyze_ppu_frame_changes.py debug.log --script-window 5
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

PPU_RE = re.compile(r"\[PPU_FRAME\]\s+f=(?P<f>\d+)\s+(?P<rest>.*)$")
SCRIPT_RE = re.compile(r"\[SCRIPT\]\s+(?P<rest>.*)$")

# Key=VALUE tokens, where VALUE may be 0x... or decimal.
TOKEN_RE = re.compile(r"(?P<k>[A-Z0-9_]+)=(?P<v>0x[0-9a-fA-F]+|\d+)")

FIELDS_OF_INTEREST = [
    "DISPCNT",
    "mode",
    "BG_EN",
    "OBJ_EN",
    "WIN",
    "BG0",
    "BG1",
    "BG2",
    "BG3",
    "BG0HOFS",
    "BG0VOFS",
    "BG1HOFS",
    "BG1VOFS",
    "BG2HOFS",
    "BG2VOFS",
    "BG3HOFS",
    "BG3VOFS",
    "BLDCNT",
    "BLDALPHA",
    "WININ",
    "WINOUT",
    "WIN0H",
    "WIN0V",
    "WIN1H",
    "WIN1V",
    "MOSAIC",
]


def parse_int(s: str) -> int:
    return int(s, 16) if s.lower().startswith("0x") else int(s)


@dataclass
class PPUFrame:
    line_no: int
    raw_line: str
    f: int
    fields: Dict[str, int]


@dataclass
class ScriptMarker:
    line_no: int
    raw_line: str


def parse_ppu_fields(rest: str) -> Dict[str, int]:
    out: Dict[str, int] = {}
    for m in TOKEN_RE.finditer(rest):
        out[m.group("k")] = parse_int(m.group("v"))
    return out


def diff_fields(prev: Dict[str, int], cur: Dict[str, int], keys: List[str]) -> List[Tuple[str, Optional[int], Optional[int]]]:
    diffs: List[Tuple[str, Optional[int], Optional[int]]] = []
    for k in keys:
        a = prev.get(k)
        b = cur.get(k)
        if a != b:
            diffs.append((k, a, b))
    return diffs


def fmt_val(v: Optional[int]) -> str:
    if v is None:
        return "(missing)"
    # Keep hex formatting for most values; mode is small.
    return f"0x{v:X}" if v > 9 else str(v)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", help="Path to debug.log")
    ap.add_argument("--only-changes", action="store_true", help="Only print when fields change")
    ap.add_argument("--max", type=int, default=200, help="Max number of change blocks to print")
    ap.add_argument("--script-window", type=int, default=8, help="PPU frames to show before/after each [SCRIPT] marker")
    args = ap.parse_args()

    frames: List[PPUFrame] = []
    scripts: List[ScriptMarker] = []

    with open(args.log, "r", errors="replace") as f:
        for idx, line in enumerate(f, start=1):
            if "[PPU_FRAME]" in line:
                m = PPU_RE.search(line)
                if not m:
                    continue
                fno = int(m.group("f"))
                fields = parse_ppu_fields(m.group("rest"))
                frames.append(PPUFrame(line_no=idx, raw_line=line.rstrip("\n"), f=fno, fields=fields))
            elif "[SCRIPT]" in line:
                if SCRIPT_RE.search(line):
                    scripts.append(ScriptMarker(line_no=idx, raw_line=line.rstrip("\n")))

    if not frames:
        print("No [PPU_FRAME] lines found.")
        return 2

    # Print changes between consecutive PPU frames.
    printed = 0
    prev = frames[0]
    if not args.only_changes:
        print(f"First PPU frame: f={prev.f} at log line {prev.line_no}")
        print(prev.raw_line)

    for cur in frames[1:]:
        diffs = diff_fields(prev.fields, cur.fields, FIELDS_OF_INTEREST)
        if diffs:
            printed += 1
            if printed <= args.max:
                print("\n=== PPU change ===")
                print(f"prev f={prev.f} line={prev.line_no}")
                print(f"cur  f={cur.f} line={cur.line_no}")
                # Summarize diff in one line
                summary = ", ".join([f"{k}:{fmt_val(a)}→{fmt_val(b)}" for (k, a, b) in diffs])
                print(summary)
                # Print raw lines for context
                print(prev.raw_line)
                print(cur.raw_line)
            if printed == args.max:
                print(f"\n(reached --max {args.max}; stopping change output)")
                break
        prev = cur

    # If script markers exist, print nearby PPU frames by line proximity.
    if scripts:
        print("\n\n=== SCRIPT markers ===")
        frame_lines = [fr.line_no for fr in frames]

        def nearest_frame_index(line_no: int) -> int:
            # Binary search on frame_lines
            lo, hi = 0, len(frame_lines)
            while lo < hi:
                mid = (lo + hi) // 2
                if frame_lines[mid] < line_no:
                    lo = mid + 1
                else:
                    hi = mid
            # lo is first >= line_no; choose closest of lo-1, lo
            cand = []
            if lo - 1 >= 0:
                cand.append(lo - 1)
            if lo < len(frame_lines):
                cand.append(lo)
            best = cand[0]
            best_dist = abs(frame_lines[best] - line_no)
            for c in cand[1:]:
                d = abs(frame_lines[c] - line_no)
                if d < best_dist:
                    best, best_dist = c, d
            return best

        for sm in scripts:
            idx = nearest_frame_index(sm.line_no)
            start = max(0, idx - args.script_window)
            end = min(len(frames) - 1, idx + args.script_window)
            print("\n---")
            print(f"SCRIPT @ line {sm.line_no}: {sm.raw_line}")
            for fr in frames[start : end + 1]:
                prefix = "*" if fr is frames[idx] else " "
                print(f"{prefix} PPU f={fr.f} line={fr.line_no} {fr.raw_line}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
