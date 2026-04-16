#!/usr/bin/env python3
"""
Bootstrap test ROM and TAS file setup.

Provides instructions and utilities for obtaining test ROMs and TAS files
for emulator validation testing.

This script helps:
1. Create test ROM directories
2. Guide users to where they can download simple test ROMs
3. Prepare TAS files from various sources
"""

import sys
import os
from pathlib import Path

def print_instructions():
    """Print comprehensive instructions for test setup."""
    
    print("""
╔════════════════════════════════════════════════════════════════════════════╗
║          TAS-Based Emulator Validation Test Setup Guide                    ║
╚════════════════════════════════════════════════════════════════════════════╝

QUICK START:
  1. Place test ROMs in test_roms/ folder
  2. Place TAS files in /tmp/test_tas/{system}/ folders
  3. Run: python3 scripts/test_tas_validation.py 3000

═══════════════════════════════════════════════════════════════════════════════

FINDING TEST ROMS:
  
  Since copyrighted ROMs cannot be redistributed, you must obtain them yourself:
  
  NES ROMS:
    - Any commercial NES game
    - Or use homebrew games (free, licensed):
      * Lode Runner (https://github.com/Infinite-NES-Music/Lode_Runner)
      * FlappyBird NES port
    - Single file to test_roms/ as: game.nes
  
  Genesis / Mega Drive:
    - Any commercial Genesis game
    - Or use Sonic 1 (if you have it)
    - File extension: .md, .gen, or .smd
    - Place in: test_roms/game.md
  
  SNES:
    - Any commercial SNES game
    - Or use Super Mario World (if you have it)
    - File extension: .smc, .sfc, .fig, or .swc
    - Place in: test_roms/game.smc
  
  Game Boy:
    - Any commercial Game Boy game
    - Or use Pokemon Red/Green/Blue (if you have it)
    - File extension: .gb or .gbc
    - Place in: test_roms/game.gb

═══════════════════════════════════════════════════════════════════════════════

FINDING TAS FILES:

  TASVideos (https://www.tasvideos.org) is the primary source:
    1. Go to https://www.tasvideos.org/
    2. Filter by game, system (NES, Genesis, SNES, GB)
    3. Download the submission (usually .fm2 or .fm3 for NES/SNES)
    4. Extract to: /tmp/test_tas/{system}/gamename.fm2
  
  Note: Some TAS files are only available in emulator-specific formats.
       The converter will attempt to parse fm2, fm3, and r08 formats.

═══════════════════════════════════════════════════════════════════════════════

DIRECTORY STRUCTURE:

  After setup, your directories should look like:
  
  test_roms/
    ├── game1.nes              (NES ROM)
    ├── game2.md               (Genesis ROM)
    ├── game3.smc              (SNES ROM)
    └── game4.gb               (Game Boy ROM)
  
  /tmp/test_tas/
    ├── nes/
    │   ├── gamename1.fm2      (NES TAS from TASVideos)
    │   └── gamename2.fm2
    ├── genesis/
    │   ├── gamename1.fm3
    │   └── gamename2.fm3
    ├── snes/
    │   ├── gamename1.fm3
    │   └── gamename2.fm3
    └── gb/
        ├── gamename1.fm2
        └── gamename2.fm2

═══════════════════════════════════════════════════════════════════════════════

RUNNING TESTS:

  Basic 3-second validation:
    python3 scripts/test_tas_validation.py
  
  Extended 10-second run per ROM:
    python3 scripts/test_tas_validation.py 10000
  
  Manual ROM test with input script:
    ./build/bin/AIOServer \\
      --headless \\
      --rom test_roms/game.nes \\
      --headless-max-ms 5000 \\
      --input-script /tmp/input.script \\
      --headless-dump-ppm /tmp/frame.ppm

═══════════════════════════════════════════════════════════════════════════════

UNDERSTANDING OUTPUT:

  Success indicators:
    - Exit code 0 = Test passed
    - "non-black" message = Frame output is rendering (not black)
    - "Headless max time reached" = Duration completed successfully
  
  Failure indicators:
    - Exit code non-zero = Test failed
    - "black" frame = PPM output was all black (rendering issue)
    - Timeout = Emulator hung or crashed

═══════════════════════════════════════════════════════════════════════════════

CREATING SYNTHETIC TEST CASES:

  For systems where you don't have ROMs, you can create minimal synthetic tests:
  
  Simple input validation:
    1. Create a 1-second input script: /tmp/test_input.script
       100 START DOWN
       600 START UP
    
    2. Run without TAS (just press START on title screen):
       ./build/bin/AIOServer --headless --rom your_rom.nes \\
         --headless-max-ms 1000 --input-script /tmp/test_input.script
    
    3. If the game responds to START button press, system is working.

═══════════════════════════════════════════════════════════════════════════════

ANALYZING FAILURES:

  If a test fails, check the log output:
    - "ROM not found" = Incorrect path or missing file
    - "Headless crash" = Emulator core crash (check debug.log)
    - "Black frame" = Rendering issue in that emulator's graphics core
    - "Timeout" = Infinite loop or hang in emulator
  
  For detailed debugging:
    1. Check ./debug.log for emulator error messages
    2. Run with system.instructions notes for that emulator
    3. Compare behavior against original hardware via TASVideos
    4. Check commit history for recent changes to that core

═══════════════════════════════════════════════════════════════════════════════
""")

def setup_directories():
    """Create test directories if they don't exist."""
    test_roms = Path("test_roms")
    test_roms.mkdir(exist_ok=True)
    print(f"✓ Created/verified: {test_roms}")
    
    for system in ["nes", "genesis", "snes", "gb"]:
        tas_dir = Path(f"/tmp/test_tas/{system}")
        tas_dir.mkdir(parents=True, exist_ok=True)
        print(f"✓ Created/verified: {tas_dir}")

def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--setup":
        setup_directories()
    else:
        print_instructions()

if __name__ == "__main__":
    main()
