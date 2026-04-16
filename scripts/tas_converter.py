#!/usr/bin/env python3
"""
TAS (Tool-Assisted Speedrun) to AIO input script converter.

Converts various TAS formats (fm2, fm3, r08, etc.) to the AIO input script format:
  <milliseconds> <KEY> <ACTION>

Supports:
- fm2/fm3 format (text-based, used by NES, SNES emulators)
- r08 format (binary, NES verification standard)
- Bizhawk TAStudio format

Key mapping (standard controller):
  A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L
"""

import struct
import sys
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
        print("  format: auto (default), fm2, r08, fm3")
        print("  frame_rate: 60 (default), 50, etc.")
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
        else:
            print(f"Cannot auto-detect format for {input_file}", file=sys.stderr)
            sys.exit(1)
    
    frames = []
    if format_hint in ("fm2", "fm3"):
        frames = parse_fm2_file(input_file, frame_rate)
    elif format_hint == "r08":
        frames = parse_r08_file(input_file, frame_rate)
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
