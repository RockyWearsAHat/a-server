#!/usr/bin/env python3
"""
TAS (Tool-Assisted Speedrun) to AIO input script converter.

Converts various TAS formats (fm2, fm3, r08, vbm, bk2) to the AIO input script format:
  <milliseconds> <KEY> <ACTION>

Supports:
- fm2/fm3 format (text-based, used by NES, SNES emulators)
- r08 format (binary, NES verification standard)
- vbm format (binary, VisualBoyAdvance GBA/GB standard)
- bk2 format (zip-based, BizHawk multi-system)
- Bizhawk TAStudio format

Key mapping (standard controller):
  A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L
"""

import io
import struct
import sys
import zipfile
from typing import List, Tuple, Dict, Optional

class InputFrame:
    def __init__(self, frame: int, buttons: Dict[str, bool]):
        self.frame = frame
        self.buttons = buttons
    
    def to_input_script(self, frame_rate: float) -> List[Tuple[int, str, str]]:
        """Convert frame to input script events.
        
        Returns list of (milliseconds, key, action) tuples.
        """
        events = []
        ms = int(self.frame * 1000.0 / frame_rate)
        
        # When buttons change from previous frame, emit events
        for key in self.buttons:
            if self.buttons[key]:
                events.append((ms, key, "DOWN"))
        return events

def parse_fm2_file(filepath: str, frame_rate: float = 60.0) -> List[InputFrame]:
    """Parse fm2 format (used by NES/SNES emulators).
    
    Format:
    - Header lines starting with '|'
    - Each input line: "|<button_data>"
    - Button order for standard controller: A B S T R L U D (for NES)
    
    fm2 buttons (8 characters for standard controller):
      0:A 1:B 2:S(elect) 3:T(art) 4:R(ight) 5:L(eft) 6:U(p) 7:D(own)
    """
    frames = []
    button_names = ["A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN"]
    
    try:
        with open(filepath, 'r') as f:
            frame_num = 0
            for line in f:
                line = line.strip()
                
                # Skip header/comments and empty lines
                if not line or not line.startswith('|'):
                    continue
                
                # Extract button data (between pipes)
                # Format: |AABBCCDDDD...| where each position is 0 (released) or 1 (pressed)
                button_data = line[1:-1] if line.endswith('|') else line[1:]
                
                if len(button_data) < 8:
                    continue
                
                buttons = {}
                for i, key in enumerate(button_names):
                    if i < len(button_data):
                        buttons[key] = button_data[i] != '0'
                
                frames.append(InputFrame(frame_num, buttons))
                frame_num += 1
        
        return frames
    except Exception as e:
        print(f"Error parsing fm2 file: {e}", file=sys.stderr)
        return []

def parse_r08_file(filepath: str, frame_rate: float = 60.0) -> List[InputFrame]:
    """Parse r08 format (NES verification standard, binary).
    
    Format:
    - Header: 'r08' (3 bytes) + version info
    - Each frame: 1 byte for controller data
    - Bit mapping: 7:A 6:B 5:Start 4:Select 3:Down 2:Up 1:Left 0:Right
    """
    frames = []
    
    try:
        with open(filepath, 'rb') as f:
            header = f.read(3)
            if header != b'r08':
                print(f"Invalid r08 file header: {header}", file=sys.stderr)
                return []
            
            # Skip version info (1 byte)
            f.read(1)
            
            frame_num = 0
            while True:
                byte = f.read(1)
                if not byte:
                    break
                
                ctrl = byte[0]
                buttons = {
                    "A": (ctrl & 0x80) != 0,
                    "B": (ctrl & 0x40) != 0,
                    "START": (ctrl & 0x10) != 0,
                    "SELECT": (ctrl & 0x20) != 0,
                    "DOWN": (ctrl & 0x08) != 0,
                    "UP": (ctrl & 0x04) != 0,
                    "LEFT": (ctrl & 0x02) != 0,
                    "RIGHT": (ctrl & 0x01) != 0,
                }
                
                frames.append(InputFrame(frame_num, buttons))
                frame_num += 1
        
        return frames
    except Exception as e:
        print(f"Error parsing r08 file: {e}", file=sys.stderr)
        return []

def parse_vbm_file(filepath: str, frame_rate: float = 59.7275) -> List[InputFrame]:
    """Parse VBM format (VisualBoyAdvance-M GBA/GBC recording).

    Binary layout (all little-endian):
      0x00  4B  magic "VBM\x1a"
      0x04  4B  version (must be 1)
      0x08  4B  UID
      0x0C  4B  number of frames
      0x10  4B  rerecord count
      0x14  1B  start flags
      0x15  1B  controller flags  (bit 0 = port1 present)
      0x16  1B  system flags      (bit 0 = GBA, bit 1 = GBC/SGB)
      0x17  1B  video flags
      0x18  4B  length of SRAM embedded at file end (0 if none)
      0x1C  4B  offset of input data from file start
      0x20  4B  offset of BIOS/init state (0 if SRAM start)
      0x24  192B  ROM title string (null-padded)
      ...   up to input_offset: metadata
      [input_offset]  2B per frame: GBA 16-bit button mask

    GBA button bit layout (16 bits, active-LOW from hardware, but VBM stores
    active-HIGH for the buttons that are pressed):
      bit 0   A
      bit 1   B
      bit 2   SELECT
      bit 3   START
      bit 4   RIGHT
      bit 5   LEFT
      bit 6   UP
      bit 7   DOWN
      bit 8   R
      bit 9   L
      bits 10-15  unused / reset / speed bits

    Reference: https://tasvideos.org/EmulatorResources/VBA/VBM
    """
    GBA_BITS = [
        (0,  "A"),
        (1,  "B"),
        (2,  "SELECT"),
        (3,  "START"),
        (4,  "RIGHT"),
        (5,  "LEFT"),
        (6,  "UP"),
        (7,  "DOWN"),
        (8,  "R"),
        (9,  "L"),
    ]

    frames = []
    try:
        with open(filepath, "rb") as f:
            data = f.read()

        magic = data[0:4]
        if magic != b"VBM\x1a":
            # Some encoders omit the trailing 0x1a; accept three-byte prefix too
            if magic[:3] != b"VBM":
                print(f"Not a VBM file (magic={magic!r})", file=sys.stderr)
                return []

        version = struct.unpack_from("<I", data, 0x04)[0]
        num_frames = struct.unpack_from("<I", data, 0x0C)[0]
        input_offset = struct.unpack_from("<I", data, 0x1C)[0]

        if input_offset == 0 or input_offset >= len(data):
            print(f"VBM: bad input_offset {input_offset:#x}", file=sys.stderr)
            return []

        for i in range(num_frames):
            pos = input_offset + i * 2
            if pos + 2 > len(data):
                break
            word = struct.unpack_from("<H", data, pos)[0]
            buttons = {name: bool(word & (1 << bit)) for bit, name in GBA_BITS}
            frames.append(InputFrame(i, buttons))

    except Exception as exc:
        print(f"Error parsing VBM file: {exc}", file=sys.stderr)

    return frames


def parse_bk2_file(filepath: str, frame_rate: float = 59.7275) -> List[InputFrame]:
    """Parse BK2 format (BizHawk multi-system recording).

    A BK2 file is a ZIP archive.  The relevant member is "Input Log.txt", which
    uses a pipe-delimited format similar to fm2 but with named columns:

        [Input]
        LogKey:#P1 A#P1 B#P1 Select#P1 Start#P1 Up#P1 Down#P1 Left#P1 Right#P1 L#P1 R#
        |..........|
        |A.........|   <- A pressed, everything else released
        ...

    Each data row is one frame.  A column character other than '.' or space
    means the button is pressed.  The exact character in the column is the
    button abbreviation from the LogKey header.

    Reference: https://tasvideos.org/BizHawk/BK2Format
    """
    # Map BizHawk column labels to AIO button names
    _BK2_ALIAS: Dict[str, str] = {
        "A": "A", "B": "B", "Select": "SELECT", "Start": "START",
        "Up": "UP", "Down": "DOWN", "Left": "LEFT", "Right": "RIGHT",
        "L": "L", "R": "R", "X": "X", "Y": "Y",
    }

    frames: List[InputFrame] = []
    try:
        with zipfile.ZipFile(filepath, "r") as zf:
            names = zf.namelist()
            log_name = next((n for n in names if n.lower() == "input log.txt"), None)
            if log_name is None:
                print(f"BK2: no 'Input Log.txt' in archive (found: {names})", file=sys.stderr)
                return []
            raw = zf.read(log_name).decode("utf-8", errors="replace")

        columns: List[str] = []
        frame_num = 0
        for line in raw.splitlines():
            line = line.rstrip()
            if line.startswith("LogKey:"):
                # Extract column labels between '#' delimiters, dropping the player prefix
                raw_key = line[len("LogKey:"):]
                parts = [p for p in raw_key.split("#") if p]
                columns = []
                for part in parts:
                    # Strip player prefix e.g. "P1 A" -> "A"
                    label = part.strip()
                    if " " in label:
                        label = label.split(" ", 1)[1]
                    aio_name = _BK2_ALIAS.get(label)
                    if aio_name:
                        columns.append(aio_name)
                    else:
                        columns.append("")  # placeholder for unknown columns
                continue

            if not line.startswith("|") or not columns:
                continue

            # Strip leading/trailing pipes and split on '|'
            inner = line.strip("|")
            segments = inner.split("|")
            if not segments:
                continue
            # First segment is P1 controller data
            ctrl = segments[0]
            buttons: Dict[str, bool] = {}
            for col_idx, aio_name in enumerate(columns):
                if not aio_name:
                    continue
                pressed = col_idx < len(ctrl) and ctrl[col_idx] not in (".", " ", "\x00")
                buttons[aio_name] = pressed

            frames.append(InputFrame(frame_num, buttons))
            frame_num += 1

    except zipfile.BadZipFile:
        print(f"BK2: file is not a valid ZIP archive: {filepath}", file=sys.stderr)
    except Exception as exc:
        print(f"Error parsing BK2 file: {exc}", file=sys.stderr)

    return frames


def generate_input_script(frames: List[InputFrame], frame_rate: float = 60.0) -> str:
    """Generate input script from frames.
    
    Tracks button state changes and emits DOWN/UP events at frame transitions.
    """
    lines = []
    prev_buttons = {key: False for key in ["A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"]}
    
    for frame in frames:
        ms = int(frame.frame * 1000.0 / frame_rate)
        
        # Check each button for state change
        for key in prev_buttons:
            curr = frame.buttons.get(key, False)
            prev = prev_buttons[key]
            
            if curr and not prev:  # Button pressed
                lines.append(f"{ms} {key} DOWN")
            elif not curr and prev:  # Button released
                lines.append(f"{ms} {key} UP")
        
        # Update previous state
        prev_buttons = frame.buttons.copy()
    
    return '\n'.join(lines)

def main():
    if len(sys.argv) < 2:
        print("Usage: tas_converter.py <input_file> [format] [frame_rate]")
        print("  format: auto (default), fm2, r08, fm3, vbm, bk2")
        print("  frame_rate: 60 (default), 59.7275 (GBA), 50, etc.")
        sys.exit(1)

    input_file = sys.argv[1]
    format_hint = sys.argv[2].lower() if len(sys.argv) > 2 else "auto"
    frame_rate = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0

    # Auto-detect format from extension
    if format_hint == "auto":
        if input_file.endswith(".fm2"):
            format_hint = "fm2"
        elif input_file.endswith(".fm3"):
            format_hint = "fm3"  # Use fm2 parser, button layout may differ
        elif input_file.endswith(".r08"):
            format_hint = "r08"
        elif input_file.endswith(".vbm"):
            format_hint = "vbm"
            if frame_rate == 60.0:
                frame_rate = 59.7275  # GBA native rate
        elif input_file.endswith(".bk2"):
            format_hint = "bk2"
            if frame_rate == 60.0:
                frame_rate = 59.7275  # default to GBA; caller may override
        else:
            print(f"Cannot auto-detect format for {input_file}", file=sys.stderr)
            sys.exit(1)

    frames = []
    if format_hint in ("fm2", "fm3"):
        frames = parse_fm2_file(input_file, frame_rate)
    elif format_hint == "r08":
        frames = parse_r08_file(input_file, frame_rate)
    elif format_hint == "vbm":
        frames = parse_vbm_file(input_file, frame_rate)
    elif format_hint == "bk2":
        frames = parse_bk2_file(input_file, frame_rate)
    else:
        print(f"Unknown format: {format_hint}", file=sys.stderr)
        sys.exit(1)

    if not frames:
        print(f"No frames parsed from {input_file}", file=sys.stderr)
        sys.exit(1)

    script = generate_input_script(frames, frame_rate)
    print(script)

if __name__ == "__main__":
    main()
