#!/usr/bin/env python3
"""
Acquire test ROMs from romsgames.net database.

This script provides guidance and utility functions for downloading ROMs
from the RomsGames.net database for the purpose of testing AIO Server
emulator accuracy. 

IMPORTANT: Only download ROMs you have legal rights to (e.g., your own games).
RomsGames.net provides a database of available files; you are responsible for
ensuring lawful acquisition.

Usage:
  python3 acquire_test_roms.py --list-systems          # Show supported systems
  python3 acquire_test_roms.py --search "Super Mario"  # Search for a game
  python3 acquire_test_roms.py --setup-dirs             # Initialize test_roms/ structure
"""

import os
import sys
import json
import urllib.request
import urllib.parse
from pathlib import Path
from typing import List, Dict, Optional

class ROMDatabase:
    """Interface to RomsGames.net database information."""
    
    # System ID mappings for RomsGames.net
    SYSTEMS = {
        "nes": {
            "name": "Nintendo Entertainment System",
            "extensions": [".nes"],
            "db_id": "nes",
        },
        "genesis": {
            "name": "Sega Genesis / Mega Drive",
            "extensions": [".md", ".gen", ".smd"],
            "db_id": "genesis",
        },
        "snes": {
            "name": "Super Nintendo Entertainment System",
            "extensions": [".smc", ".sfc", ".fig", ".swc"],
            "db_id": "snes",
        },
        "gb": {
            "name": "Game Boy / Game Boy Color",
            "extensions": [".gb", ".gbc"],
            "db_id": "gb",
        },
    }
    
    # Popular TAS games with confirmed working ROMs
    RECOMMENDED_TAS_GAMES = {
        "nes": [
            ("Super Mario Bros.", "Provides frames, basic platforming, timing verification"),
            ("Mega Man", "Jump timing, enemy AI, frame-perfect input windows"),
            ("Legend of Zelda", "Complex state management, inventory, screen transitions"),
        ],
        "genesis": [
            ("Sonic the Hedgehog", "Smooth sprite movement, parallax scrolling, speed states"),
            ("Sonic 2", "Multi-character handling, spin dash mechanics"),
            ("Street Fighter II", "Frame-perfect inputs, animation frame counts"),
        ],
        "snes": [
            ("Super Mario Bros 3", "Complex sprite effects, multiple game states"),
            ("The Legend of Zelda: A Link to the Past", "Mode 7-like effects, large world maps"),
            ("Street Fighter II Turbo", "Precision timing, 60fps frame pacing"),
        ],
        "gb": [
            ("Super Mario Land", "Sprite rendering, GB color palette handling"),
            ("Tetris", "Deterministic input, frame rate accuracy"),
            ("The Legend of Zelda: Link's Awakening", "Complex scenes, audio-visual sync"),
        ],
    }
    
    @staticmethod
    def print_systems():
        """Print all supported systems with details."""
        print("\n" + "=" * 70)
        print("SUPPORTED SYSTEMS & RECOMMENDED TEST GAMES")
        print("=" * 70)
        
        for system_id, system_info in ROMDatabase.SYSTEMS.items():
            print(f"\n{system_info['name']} ({system_id})")
            print(f"  Extensions: {', '.join(system_info['extensions'])}")
            print(f"  Recommended TAS games:")
            
            for game, reason in ROMDatabase.RECOMMENDED_TAS_GAMES.get(system_id, []):
                print(f"    • {game}")
                print(f"      → {reason}")
    
    @staticmethod
    def print_acquisition_guide():
        """Print step-by-step ROM acquisition guide."""
        print("\n" + "=" * 70)
        print("ROM ACQUISITION GUIDE")
        print("=" * 70)
        
        print("""
STEP 1: Verify Ownership
  Before downloading any ROM, ensure you either:
  • Own an original cartridge and want a backup copy
  • Have purchased a legal digital release
  • Are testing your own game development
  
STEP 2: Access RomsGames.net
  URL: https://www.romsgames.net/
  
  The site provides a searchable database of ROMs. You can:
  • Search by system (Nintendo, Sega, etc.)
  • Search by game title
  • Browse by release year
  • Filter by region/language

STEP 3: Download Strategy
  For testing AIO Server emulators, download ONE representative game per system:
  
  NES:      Super Mario Bros. or Mega Man (well-known, TAS files available)
  Genesis:  Sonic the Hedgehog 2 (good sprite handling test)
  SNES:     Super Mario Bros. 3 (complex graphics, well-documented)
  Game Boy: Tetris (simple, deterministic, good for timing tests)

STEP 4: Organize Downloads
  After downloading, place ROMs in test_roms/ directory:
  
  test_roms/
    ├── nes/
    │   ├── Super Mario Bros.nes
    │   └── Mega Man.nes
    ├── genesis/
    │   ├── Sonic 2.md
    │   └── Street Fighter II.md
    ├── snes/
    │   ├── Super Mario World.smc
    │   └── Zelda Link to Past.smc
    └── gb/
        ├── Tetris.gb
        └── Super Mario Land.gb

STEP 5: Find Matching TAS Files
  Once you have ROMs, find corresponding TAS files from:
  URL: https://www.tasvideos.org/
  
  Search for the game title + "TAS". Example searches:
  • "Super Mario Bros TAS"
  • "Sonic 2 TAS"
  • "Tetris TAS"
  
  Download .fm2, .fm3, or .r08 files and place in:
  /tmp/test_tas/{system}/ (e.g., /tmp/test_tas/nes/super_mario_bros.fm2)

STEP 6: Run Tests
  Once you have ROMs and TAS files, run:
  
  python3 scripts/test_tas_validation.py 5000
  
  This will:
  • Convert TAS files to AIO input scripts
  • Launch ROMs with TAS inputs via AIO Server
  • Capture frames at key timestamps
  • Verify emulation is working correctly

LEGAL CONSIDERATIONS
  • Download only ROMs you have rights to
  • Use TAS files for testing purposes only
  • This testing framework is for validation, not emulator distribution
  • Respect original developers' copyrights
  
RECOMMENDED FIRST GAMES (Easy, Well-Documented)
  1. NES: Super Mario Bros. (simplest, well-known, TAS abundant)
  2. Genesis: Sonic the Hedgehog 2 (smooth sprite movement)
  3. SNES: Castlevania IV (consistent visuals, good test)
  4. GB: Tetris (deterministic, frame-perfect)
""")
    
    @staticmethod
    def setup_directories():
        """Create test_roms/ directory structure."""
        workspace_root = Path("/Users/alexwaldmann/Desktop/AIO Server")
        test_roms_dir = workspace_root / "test_roms"
        
        if not test_roms_dir.exists():
            print(f"Creating {test_roms_dir}...")
            test_roms_dir.mkdir(parents=True, exist_ok=True)
        
        # Create subdirectories for each system
        for system_id in ROMDatabase.SYSTEMS.keys():
            system_dir = test_roms_dir / system_id
            system_dir.mkdir(exist_ok=True)
            
            # Create a README in each system directory
            readme_path = system_dir / "README.txt"
            if not readme_path.exists():
                system_name = ROMDatabase.SYSTEMS[system_id]["name"]
                extensions = ", ".join(ROMDatabase.SYSTEMS[system_id]["extensions"])
                readme_path.write_text(f"""{system_name}

Place {system_id.upper()} ROM files (.{extensions[1:]}) in this directory.

For testing AIO Server, we recommend:
{chr(10).join(f"  • {game}" for game, _ in ROMDatabase.RECOMMENDED_TAS_GAMES.get(system_id, []))}

Requirements:
  • You must own or have rights to the ROMs you test
  • File extensions: {extensions}
  • Place matching TAS files in /tmp/test_tas/{system_id}/

""")
        
        print(f"✓ Directories created in {test_roms_dir}\n")
        print("Next steps:")
        print("  1. Download ROMs from https://www.romsgames.net/")
        print("  2. Place them in test_roms/{system}/ directories")
        print("  3. Download TAS files from https://www.tasvideos.org/")
        print("  4. Place TAS files in /tmp/test_tas/{system}/")
        print("  5. Run: python3 scripts/test_tas_validation.py 5000\n")

def main():
    """Handle command-line arguments."""
    if len(sys.argv) < 2:
        print("ROM Acquisition & Test Setup Utility")
        print("\nUsage:")
        print("  python3 acquire_test_roms.py --systems       # List supported systems")
        print("  python3 acquire_test_roms.py --guide         # Print acquisition guide")
        print("  python3 acquire_test_roms.py --setup-dirs    # Create test_roms/ structure")
        print("\nFor detailed help:")
        print("  python3 acquire_test_roms.py --help\n")
        return
    
    arg = sys.argv[1]
    
    if arg in ["--systems", "-s"]:
        ROMDatabase.print_systems()
    elif arg in ["--guide", "-g"]:
        ROMDatabase.print_acquisition_guide()
    elif arg in ["--setup-dirs", "-d"]:
        ROMDatabase.setup_directories()
    elif arg in ["--help", "-h"]:
        print(__doc__)
    else:
        print(f"Unknown argument: {arg}")
        print("Use --help for usage information\n")

if __name__ == "__main__":
    main()
