#!/usr/bin/env python3
"""
Quick demonstration of TAS-based testing with Game Boy.

This script:
1. Creates a minimal TAS input sequence
2. Converts it to AIO input script format
3. Runs Game Boy emulator with those inputs
4. Captures and verifies output
"""

import subprocess
import tempfile
from pathlib import Path
import sys

def demo():
    """Run demonstration of TAS validation system."""
    
    print("""
╔════════════════════════════════════════════════════════════════════════════╗
║              TAS Validation Test Suite - Demonstration                     ║
╚════════════════════════════════════════════════════════════════════════════╝
""")
    
    aio_bin = Path("/Users/alexwaldmann/Desktop/AIO Server/build/bin/AIOServer")
    if not aio_bin.exists():
        print(f"ERROR: AIOServer not found at {aio_bin}")
        print("Please build first: make build")
        return False
    
    # Create temp directory for test artifacts
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)
        
        # Create sample input script
        input_script = tmpdir / "demo.script"
        input_script.write_text("""# Demo input script for emulator testing
# Format: <milliseconds> <KEY> <ACTION>

100 START DOWN
600 START UP
1200 A DOWN
1600 A UP
2200 B DOWN
2600 B UP
""")
        
        print("Step 1: Input Script Created")
        print(f"  Path: {input_script}")
        print(f"  Content:")
        for line in input_script.read_text().split('\n'):
            if line and not line.startswith('#'):
                print(f"    {line}")
        print()
        
        # Test with Game Boy (requires test ROM)
        test_rom = Path("/Users/alexwaldmann/Desktop/AIO Server/test_roms/minimal_test.gb")
        if not test_rom.exists():
            print("Step 2: Locate Test ROM")
            print(f"  ⚠ Test ROM not found at {test_rom}")
            print(f"  Creating synthetic test instead...")
            
            # Demonstrate with a longer headless test
            cmd = [
                str(aio_bin),
                "--headless",
                "--rom", "/tmp/minimal_test.gb",
                "--headless-max-ms", "2000",
                "--input-script", str(input_script),
                "--headless-assert-nonblack",
            ]
        else:
            print(f"Step 2: Test ROM Located")
            print(f"  Path: {test_rom}")
            print(f"  Size: {test_rom.stat().st_size} bytes")
            print()
            
            # Build command
            cmd = [
                str(aio_bin),
                "--headless",
                "--rom", str(test_rom),
                "--headless-max-ms", "3000",
                "--input-script", str(input_script),
                "--headless-assert-nonblack",
            ]
        
        print("Step 3: Run Emulator with Input Script")
        print(f"  Command: {' '.join(cmd[-4:])}")  # Show key args
        print(f"  Running...")
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=10
            )
            
            print(f"  Exit Code: {result.returncode}")
            
            # Parse output
            if "non-black" in result.stderr or "non-black" in result.stdout:
                print("  Frame Output: ✓ Non-black (rendering working)")
            if "Headless max time reached" in result.stderr or "Headless max time reached" in result.stdout:
                print("  Duration: ✓ Completed requested duration")
            
            if result.returncode == 0:
                print("  Status: ✓ PASSED")
            else:
                print("  Status: ✗ FAILED")
                print(f"\n  Output:\n{result.stderr}")
            
            print()
            print("Step 4: Test Complete")
            print(f"  Result: {'SUCCESS' if result.returncode == 0 else 'FAILED'}")
            
            return result.returncode == 0
            
        except subprocess.TimeoutExpired:
            print("  ✗ Test timeout (emulator hung)")
            return False
        except Exception as e:
            print(f"  ✗ Error: {e}")
            return False

if __name__ == "__main__":
    success = demo()
    print("\n" + "="*80)
    print("For full TAS testing with ROMs and TAS files:")
    print("  1. See scripts/TAS_TESTING_README.md for setup instructions")
    print("  2. Obtain test ROMs from your own collection or legal sources")
    print("  3. Download TAS files from https://www.tasvideos.org")
    print("  4. Run: python3 scripts/test_tas_validation.py 5000")
    print("="*80)
    
    sys.exit(0 if success else 1)
