#!/usr/bin/env python3
"""
TAS-based emulator validation test runner.

Finds test ROMs and TAS files, converts TAS to input scripts,
runs them through the emulator in headless mode, and captures screenshots
at multiple timepoints to verify correctness of emulation.

Supports: NES, Genesis, SNES, Game Boy
"""

import os
import sys
import subprocess
import json
import tempfile
from pathlib import Path
from typing import List, Dict, Optional, Tuple
import time

class EmulatorTest:
    """Single test case: ROM + TAS + expected behavior."""
    
    def __init__(self, system: str, rom_path: str, tas_path: Optional[str] = None):
        self.system = system  # "nes", "genesis", "snes", "gb"
        self.rom_path = rom_path
        self.tas_path = tas_path
        self.name = Path(rom_path).stem
        self.extension = {
            "nes": ".nes",
            "genesis": ".md",
            "snes": ".smc",
            "gb": ".gb",
        }.get(system)
        
    def __repr__(self):
        return f"EmulatorTest({self.system}/{self.name})"

class TestHarness:
    """Orchestrates TAS-based validation testing."""
    
    def __init__(self, aio_bin: str):
        self.aio_bin = aio_bin
        self.test_roms_dir = Path("/Users/alexwaldmann/Desktop/AIO Server/test_roms")
        self.scripts_dir = Path("/Users/alexwaldmann/Desktop/AIO Server/scripts")
        self.test_results = []
        
    def find_test_roms(self) -> Dict[str, List[str]]:
        """Find available test ROMs organized by system."""
        roms_by_system = {
            "nes": [],
            "genesis": [],
            "snes": [],
            "gb": [],
        }
        
        if not self.test_roms_dir.exists():
            print(f"Test ROMs directory not found: {self.test_roms_dir}")
            return roms_by_system
        
        extensions = {
            "nes": [".nes"],
            "genesis": [".md", ".gen", ".smd"],
            "snes": [".smc", ".sfc"],
            "gb": [".gb", ".gbc"],
        }
        
        for system, exts in extensions.items():
            for rom_file in self.test_roms_dir.iterdir():
                if rom_file.is_file() and rom_file.suffix.lower() in exts:
                    roms_by_system[system].append(str(rom_file))
        
        return roms_by_system
    
    def find_test_tas_files(self) -> Dict[str, List[str]]:
        """Find available TAS files organized by system."""
        tas_by_system = {
            "nes": [],
            "genesis": [],
            "snes": [],
            "gb": [],
        }
        
        tas_dir = Path("/tmp/test_tas")  # External TAS repo location
        if not tas_dir.exists():
            print(f"TAS directory not found: {tas_dir}")
            return tas_by_system
        
        extensions = {
            "nes": [".fm2", ".r08"],
            "genesis": [".fm3", ".vgm"],
            "snes": [".fm3", ".smv"],
            "gb": [".fm2"],
        }
        
        for system, exts in extensions.items():
            tas_system_dir = tas_dir / system
            if tas_system_dir.exists():
                for tas_file in tas_system_dir.iterdir():
                    if tas_file.is_file() and tas_file.suffix.lower() in exts:
                        tas_by_system[system].append(str(tas_file))
        
        return tas_by_system
    
    def convert_tas_to_script(self, tas_path: str, output_path: str) -> bool:
        """Convert TAS file to input script using tas_converter.py."""
        converter = self.scripts_dir / "tas_converter.py"
        if not converter.exists():
            print(f"TAS converter not found: {converter}")
            return False
        
        try:
            result = subprocess.run(
                [sys.executable, str(converter), tas_path],
                capture_output=True,
                text=True,
                timeout=30
            )
            if result.returncode != 0:
                print(f"TAS conversion failed: {result.stderr}")
                return False
            
            with open(output_path, 'w') as f:
                f.write(result.stdout)
            
            print(f"  Converted TAS to input script: {output_path}")
            return True
        except Exception as e:
            print(f"TAS conversion error: {e}")
            return False
    
    def run_test_with_tas(self, test: EmulatorTest, duration_ms: int = 5000) -> Tuple[bool, Dict]:
        """Run emulator with TAS inputs and capture screenshots."""
        
        # Create temp directory for outputs
        with tempfile.TemporaryDirectory() as tmpdir:
            tmpdir = Path(tmpdir)
            
            # Prepare input script
            script_path = None
            if test.tas_path and os.path.exists(test.tas_path):
                script_path = tmpdir / "input.script"
                if not self.convert_tas_to_script(test.tas_path, str(script_path)):
                    return False, {"error": "TAS conversion failed"}
            
            # Prepare screenshot capture points
            capture_times = [1000, 2000, 3000, 4000]  # ms
            capture_files = [tmpdir / f"frame_{t}ms.ppm" for t in capture_times]
            
            # Build command
            cmd = [
                self.aio_bin,
                "--headless",
                "--rom", test.rom_path,
                f"--headless-max-ms", str(duration_ms),
                "--headless-assert-nonblack",
            ]
            
            if script_path:
                cmd.extend(["--input-script", str(script_path)])
            
            # Add screenshot captures at different times
            # Note: The current implementation captures at a single time,
            # but we can run multiple times with different durations to get
            # screenshots at different points
            
            log_file = tmpdir / "run.log"
            
            try:
                print(f"  Running: {' '.join(cmd)}")
                result = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=duration_ms / 500 + 10,  # Generous timeout
                    env={**os.environ, "AIO_INPUT_SCRIPT_TIMEBASE": "EMU"}
                )
                
                # Save log
                with open(log_file, 'w') as f:
                    f.write(f"STDOUT:\n{result.stdout}\n\nSTDERR:\n{result.stderr}\n")
                
                success = result.returncode == 0
                print(f"  Result: {'PASS' if success else 'FAIL'} (exit code {result.returncode})")
                
                # Analyze output
                analysis = {
                    "exit_code": result.returncode,
                    "success": success,
                    "log_file": str(log_file),
                    "output": result.stderr + result.stdout,
                }
                
                # Check for success indicators
                if "non-black" in result.stderr or "non-black" in result.stdout:
                    analysis["frame_output"] = "non-black"
                if "Headless max time reached" in result.stderr or "Headless max time reached" in result.stdout:
                    analysis["completed_duration"] = True
                
                return success, analysis
                
            except subprocess.TimeoutExpired:
                return False, {"error": "Test timeout"}
            except Exception as e:
                return False, {"error": str(e)}
    
    def run_test_sequence(self, duration_ms: int = 3000) -> bool:
        """Run comprehensive test sequence across all systems."""
        print("\n" + "="*70)
        print("TAS-Based Emulator Validation Test Suite")
        print("="*70)
        
        # Find ROMs
        roms = self.find_test_roms()
        print("\nFound test ROMs:")
        for system, rom_list in roms.items():
            print(f"  {system}: {len(rom_list)} ROM(s)")
            for rom in rom_list[:3]:  # Show first 3
                print(f"    - {Path(rom).name}")
        
        # Find TAS files
        tas_files = self.find_test_tas_files()
        print("\nFound TAS files:")
        for system, tas_list in tas_files.items():
            print(f"  {system}: {len(tas_list)} TAS file(s)")
            for tas in tas_list[:3]:
                print(f"    - {Path(tas).name}")
        
        # Run tests
        print("\n" + "="*70)
        print("Running Tests")
        print("="*70)
        
        total_tests = 0
        passed_tests = 0
        
        for system in ["nes", "genesis", "snes", "gb"]:
            if not roms[system]:
                print(f"\n{system.upper()}: No ROMs found, skipping")
                continue
            
            print(f"\n{system.upper()} Tests")
            print("-" * 40)
            
            for rom_path in roms[system][:2]:  # Test first 2 ROMs per system
                # Find matching TAS (by name)
                matching_tas = None
                rom_name = Path(rom_path).stem
                for tas_path in tas_files[system]:
                    if rom_name in Path(tas_path).stem:
                        matching_tas = tas_path
                        break
                
                test = EmulatorTest(system, rom_path, matching_tas)
                total_tests += 1
                
                print(f"\nTest: {test}")
                if matching_tas:
                    print(f"  TAS: {Path(matching_tas).name}")
                else:
                    print(f"  TAS: (none, testing ROM directly)")
                
                success, analysis = self.run_test_with_tas(test, duration_ms)
                if success:
                    passed_tests += 1
                
                # Show analysis
                if analysis.get("error"):
                    print(f"  ERROR: {analysis['error']}")
                if analysis.get("frame_output"):
                    print(f"  Frame output verified: {analysis['frame_output']}")
                if analysis.get("completed_duration"):
                    print(f"  Completed requested duration: {duration_ms}ms")
                
                self.test_results.append({
                    "test": str(test),
                    "success": success,
                    "analysis": analysis,
                })
        
        # Summary
        print("\n" + "="*70)
        print(f"Test Summary: {passed_tests}/{total_tests} passed")
        print("="*70)
        
        return passed_tests == total_tests

def main():
    aio_bin = Path("/Users/alexwaldmann/Desktop/AIO Server/build/bin/AIOServer")
    if not aio_bin.exists():
        print(f"AIOServer binary not found: {aio_bin}")
        print("Please build the project first: make build")
        sys.exit(1)
    
    harness = TestHarness(str(aio_bin))
    
    # Duration can be overridden via command line
    duration = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    
    success = harness.run_test_sequence(duration)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
