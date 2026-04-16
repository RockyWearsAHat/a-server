#!/usr/bin/env python3
"""
Complete End-to-End Quality Verification Workflow

This script orchestrates the entire AIO Server emulator validation process:
1. ROM acquisition from romsgames.net database
2. TAS file discovery from tasvideos.org
3. Comprehensive behavioral and visual testing
4. Quality audit with AAA design standards
5. Final verification report

Run this to get started with complete validation:
    python3 comprehensive_verification_workflow.py

Or follow the manual steps below for step-by-step guidance.
"""

import subprocess
import sys
from pathlib import Path

def run_command(cmd, description):
    """Run a command and report success/failure."""
    print(f"\n{'='*70}")
    print(f"{description}")
    print(f"{'='*70}")
    try:
        result = subprocess.run(cmd, shell=True)
        return result.returncode == 0
    except Exception as e:
        print(f"✗ Error: {e}")
        return False

def main():
    """Main workflow orchestrator."""
    workspace = Path("/Users/alexwaldmann/Desktop/AIO Server")
    scripts = workspace / "scripts"
    
    print(f"""
╔{'='*68}╗
║{'COMPREHENSIVE EMULATOR VALIDATION WORKFLOW':^68}║
║{'Complete Visual & Behavioral Quality Verification':^68}║
╚{'='*68}╝

This workflow will:
1. ✓ Verify AIO Server is built
2. ✓ Setup ROM/TAS directories
3. ✓ Guide you through ROM acquisition
4. ✓ Guide you through TAS file discovery
5. ✓ Run comprehensive validation suite
6. ✓ Generate quality audit report

""")
    
    # Step 1: Verify build
    print("STEP 1: Verify AIO Server is built...")
    aio_bin = workspace / "build" / "bin" / "AIOServer"
    if aio_bin.exists():
        print("✓ AIOServer binary found")
    else:
        print("✗ AIOServer binary not found!")
        print(f"  Run: cd {workspace} && make build")
        return 1
    
    # Step 2: Setup directories
    print("\nSTEP 2: Setup ROM/TAS directories...")
    if run_command(
        f"python3 {scripts}/acquire_test_roms.py --setup-dirs",
        "Initializing test directories"
    ):
        print("✓ Directories created successfully")
    else:
        print("✗ Failed to create directories")
        return 1
    
    # Step 3: Show ROM acquisition guide
    print("\nSTEP 3: ROM Acquisition Guide")
    print(f"\nNow you need to obtain test ROMs from: https://www.romsgames.net/")
    print("\nRecommended games (download one per system):")
    print("""
    NES:     Super Mario Bros (most documented, TAS widely available)
    Genesis: Sonic the Hedgehog 2 (good for sprite/scrolling testing)
    SNES:    Super Mario Bros. 3 (tests complex graphics)
    Game Boy: Tetris (deterministic, frame-perfect verification)
    """)
    
    print("\nAfter downloading, place ROMs in:")
    print(f"  test_roms/nes/Super Mario Bros.nes")
    print(f"  test_roms/genesis/Sonic 2.md")
    print(f"  test_roms/snes/Super Mario Bros 3.smc")
    print(f"  test_roms/gb/Tetris.gb")
    
    input("\n▶ Press Enter when you've placed ROM files in test_roms/...")
    
    # Step 4: Show TAS guide
    print("\nSTEP 4: TAS File Acquisition")
    print(f"\nNow download matching TAS files from: https://www.tasvideos.org/")
    print("""
    Search for each game (e.g., "Super Mario Bros NES TAS")
    Download the .fm2, .fm3, or .r08 file
    Place in: /tmp/test_tas/{system}/{game_name}.fm2
    """)
    
    input("\n▶ Press Enter when you've placed TAS files in /tmp/test_tas/...")
    
    # Step 5: Run validation
    print("\nSTEP 5: Running Comprehensive Validation Suite")
    print("\nThis will:")
    print("  • Discover all ROMs and TAS files")
    print("  • Convert TAS files to AIO input scripts")
    print("  • Run each ROM in headless mode with TAS inputs")
    print("  • Capture frames at multiple timepoints")
    print("  • Analyze visual quality per AAA standards")
    print("  • Generate audit reports")
    
    print("\nStarting validation suite...")
    
    if run_command(
        f"python3 {scripts}/comprehensive_validation.py --run",
        "COMPREHENSIVE VALIDATION SUITE"
    ):
        print("\n✓ Validation suite completed successfully")
    else:
        print("\n✗ Validation suite encountered errors")
        return 1
    
    # Step 6: Summary
    print(f"\n{'='*70}")
    print("WORKFLOW COMPLETE")
    print(f"{'='*70}")
    print("""
Next Steps:
1. Review the quality audit report above
2. Check test summary for any FAIL results
3. If quality score >= 90/100: Your emulators pass AAA standards
4. If quality score < 90/100: Address identified issues per recommendations

For advanced testing:
  • Run single system:  python3 scripts/comprehensive_validation.py --run --system nes
  • List ROMs/TAS:      python3 scripts/comprehensive_validation.py --list
  • Visual only:        python3 scripts/comprehensive_validation.py --visual-only
  • Behavioral only:    python3 scripts/comprehensive_validation.py --behavior-only

For detailed documentation, read:
  • COMPREHENSIVE_VALIDATION.md — Full workflow guide
  • TAS_TESTING_README.md — TAS validation (basic)
  • visual-audit.instructions.md — Quality standards
""")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
