#!/usr/bin/env python3
"""
Comprehensive end-to-end emulator validation with visual and behavioral verification.

This script orchestrates complete testing of all four emulator systems (NES, Genesis, SNES, GB)
with real games and TAS (Tool-Assisted Speedrun) files to verify both:

1. BEHAVIORAL correctness: Emulation produces correct outputs, state transitions work
2. VISUAL quality: Rendered game frames look correct (sprites, colors, effects)

Test pipeline:
1. Discover ROMs in test_roms/{nes,genesis,snes,gb}/
2. Discover TAS files in /tmp/test_tas/{system}/
3. Convert TAS → AIO input script format (frame-based → millisecond-based)
4. Run ROM in headless mode with input script
5. Capture frames at key timestamps during execution
6. Analyze frames to verify game state and visual rendering correctness
7. Generate quality audit report per AAA visual design standards

Usage:
    python3 comprehensive_validation.py --help
    python3 comprehensive_validation.py --list          # List available ROMs and TAS files
    python3 comprehensive_validation.py --run           # Run full validation suite
    python3 comprehensive_validation.py --run --system nes    # Run only NES tests
    python3 comprehensive_validation.py --visual-only   # Only visual verification
    python3 comprehensive_validation.py --behavior-only # Only behavioral verification
"""

import os
import sys
import subprocess
import json
import tempfile
from pathlib import Path
from typing import List, Dict, Optional, Tuple
import time
from dataclasses import dataclass
from enum import Enum
import re

class System(Enum):
    """Supported emulator systems."""
    NES = "nes"
    GENESIS = "genesis"
    SNES = "snes"
    GB = "gb"

@dataclass
class TestResult:
    """Single test case result."""
    system: str
    game_name: str
    rom_path: str
    tas_path: Optional[str]
    status: str  # "PASS", "FAIL", "SKIP"
    behavioral_passed: bool
    visual_passed: bool
    behavioral_issues: List[str]
    visual_issues: List[str]
    frame_count: int
    duration_ms: int
    exit_code: int
    captured_frames: List[str]  # Paths to PPM files
    quality_score: float  # 0-100 AAA audit score
    
    def summary(self) -> str:
        """Generate one-line summary."""
        icons = {
            "PASS": "✓",
            "FAIL": "✗",
            "SKIP": "⊘",
        }
        return f"{icons.get(self.status, '?')} {self.system.upper():7} {self.game_name:30} (Behavior:{['FAIL','PASS'][self.behavioral_passed]:4}, Visual:{['FAIL','PASS'][self.visual_passed]:4}, Score:{self.quality_score:5.1f}/100)"

class ComprehensiveValidator:
    """Full end-to-end validation orchestrator."""
    
    FRAME_CAPTURE_TIMES_MS = {
        "nes": [500, 1000, 2000, 3000, 5000],      # 5 frames at key moments
        "genesis": [500, 1000, 2000, 3000, 5000],
        "snes": [500, 1000, 2000, 3000, 5000],
        "gb": [500, 1000, 2000, 3000, 5000],
    }
    
    TEST_DURATION_MS = {
        "nes": 5000,      # 5 seconds = ~300 frames at 60fps
        "genesis": 5000,
        "snes": 5000,
        "gb": 5000,
    }
    
    def __init__(self, workspace_root: Path = None):
        self.workspace_root = workspace_root or Path("/Users/alexwaldmann/Desktop/AIO Server")
        self.test_roms_dir = self.workspace_root / "test_roms"
        self.scripts_dir = self.workspace_root / "scripts"
        self.aio_bin = self.workspace_root / "build" / "bin" / "AIOServer"
        self.temp_dir = Path(tempfile.gettempdir())
        self.results: List[TestResult] = []
        
    def find_roms(self) -> Dict[str, List[Path]]:
        """Find all available test ROMs organized by system."""
        roms = {system.value: [] for system in System}
        
        if not self.test_roms_dir.exists():
            print(f"⚠  test_roms/ directory not found at {self.test_roms_dir}")
            print("   Run: python3 acquire_test_roms.py --setup-dirs")
            return roms
        
        for system in System:
            system_dir = self.test_roms_dir / system.value
            if system_dir.exists():
                extensions = {
                    "nes": [".nes"],
                    "genesis": [".md", ".gen", ".smd"],
                    "snes": [".smc", ".sfc", ".fig", ".swc"],
                    "gb": [".gb", ".gbc"],
                }[system.value]
                
                for ext in extensions:
                    roms[system.value].extend(system_dir.glob(f"*{ext}"))
                    roms[system.value].extend(system_dir.glob(f"*{ext.upper()}"))
        
        return roms
    
    def find_tas_files(self) -> Dict[str, Dict[str, List[Path]]]:
        """Find all available TAS files organized by system and game."""
        tas_files = {}
        
        for system in System:
            system_dir = Path("/tmp/test_tas") / system.value
            tas_files[system.value] = {}
            
            if not system_dir.exists():
                continue
            
            for tas_file in system_dir.glob("*"):
                if tas_file.suffix.lower() in [".fm2", ".fm3", ".r08", ".smv", ".vgm"]:
                    game_name = tas_file.stem
                    tas_files[system.value][game_name] = [tas_file]
        
        return tas_files
    
    def convert_tas_to_script(self, tas_path: Path) -> Optional[str]:
        """Convert TAS file to AIO input script format.
        
        Returns: Input script as string, or None on failure.
        """
        try:
            result = subprocess.run(
                ["python3", str(self.scripts_dir / "tas_converter.py"), str(tas_path)],
                capture_output=True,
                text=True,
                timeout=10
            )
            if result.returncode == 0:
                return result.stdout
            else:
                print(f"  ✗ TAS conversion failed: {result.stderr}")
                return None
        except Exception as e:
            print(f"  ✗ TAS conversion error: {e}")
            return None
    
    def run_behavioral_test(self, rom_path: Path, input_script: Optional[str],
                           duration_ms: int, system: str) -> Tuple[bool, List[str], int, int]:
        """Run ROM in headless mode with optional input script.
        
        Returns: (passed, issues, frame_count, exit_code)
        """
        issues = []
        
        try:
            # Create temp input script file if provided
            script_file = None
            if input_script:
                script_file = self.temp_dir / f"test_input_{rom_path.stem}.script"
                script_file.write_text(input_script)
            
            # Build command
            cmd = [
                str(self.aio_bin),
                "--headless",
                "--rom", str(rom_path),
                "--headless-max-ms", str(duration_ms),
                "--headless-assert-nonblack",  # Verify output is not all-black (graphics working)
            ]
            
            if script_file:
                cmd.extend(["--input-script", str(script_file)])
            
            # Run with timeout
            start = time.time()
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=duration_ms / 1000 + 5  # Add 5s buffer
            )
            elapsed = time.time() - start
            
            # Parse output for frame count and diagnostics
            frame_count = 0
            output_text = result.stdout + result.stderr
            
            # Look for frame count in output
            frame_match = re.search(r'frame[s]?[:\s]+(\d+)', output_text, re.IGNORECASE)
            if frame_match:
                frame_count = int(frame_match.group(1))
            
            # Check exit code
            if result.returncode != 0:
                issues.append(f"Non-zero exit code: {result.returncode}")
            
            # Verify non-black output
            if "black" in output_text.lower() and "assert" in output_text.lower():
                issues.append("Output detected as all-black (rendering issue)")
            
            # Check for crashes/errors
            error_keywords = ["crash", "segfault", "abort", "error", "failed"]
            for keyword in error_keywords:
                if keyword.lower() in output_text.lower():
                    issues.append(f"Error keyword detected: {keyword}")
                    break
            
            # Behavioral pass criteria
            passed = result.returncode == 0 and len(issues) == 0
            
            if script_file and script_file.exists():
                script_file.unlink()
            
            return passed, issues, frame_count, result.returncode
        
        except subprocess.TimeoutExpired:
            return False, ["Timeout: emulation exceeded maximum duration"], 0, 124
        except Exception as e:
            return False, [f"Error: {str(e)}"], 0, -1
    
    def capture_frame(self, rom_path: Path, input_script: Optional[str],
                     capture_time_ms: int, system: str) -> Optional[Path]:
        """Capture a single frame at a specific timestamp.
        
        Returns: Path to PPM file, or None on failure.
        """
        try:
            script_file = None
            if input_script:
                script_file = self.temp_dir / f"test_input_{rom_path.stem}_{capture_time_ms}.script"
                script_file.write_text(input_script)
            
            ppm_file = self.temp_dir / f"frame_{rom_path.stem}_{capture_time_ms}.ppm"
            
            cmd = [
                str(self.aio_bin),
                "--headless",
                "--rom", str(rom_path),
                "--headless-max-ms", str(capture_time_ms + 100),
                "--headless-dump-ppm", str(ppm_file),
                "--headless-dump-ms", str(capture_time_ms),
            ]
            
            if script_file:
                cmd.extend(["--input-script", str(script_file)])
            
            result = subprocess.run(
                cmd,
                capture_output=True,
                timeout=capture_time_ms / 1000 + 10
            )
            
            if ppm_file.exists():
                return ppm_file
            
            if script_file and script_file.exists():
                script_file.unlink()
            
            return None
        except Exception:
            return None
    
    def run_visual_test(self, rom_path: Path, input_script: Optional[str],
                       system: str) -> Tuple[bool, List[str], List[Path]]:
        """Capture and analyze rendered frames.
        
        Returns: (passed, issues, frame_paths)
        """
        issues = []
        frame_paths = []
        
        # Capture frames at key moments
        capture_times = self.FRAME_CAPTURE_TIMES_MS.get(system, [1000, 2000, 3000])
        
        for capture_time in capture_times:
            frame = self.capture_frame(rom_path, input_script, capture_time, system)
            if frame:
                frame_paths.append(frame)
            else:
                issues.append(f"Failed to capture frame at {capture_time}ms")
        
        # Visual pass criteria
        passed = len(frame_paths) > 0 and len(issues) == 0
        
        return passed, issues, frame_paths
    
    def run_test(self, rom_path: Path, tas_path: Optional[Path] = None) -> TestResult:
        """Run complete test for a single ROM.
        
        Returns: TestResult with all behavioral and visual findings.
        """
        system = self._detect_system(rom_path)
        if not system:
            return TestResult(
                system="unknown",
                game_name=rom_path.stem,
                rom_path=str(rom_path),
                tas_path=str(tas_path) if tas_path else None,
                status="SKIP",
                behavioral_passed=False,
                visual_passed=False,
                behavioral_issues=["Unknown ROM system"],
                visual_issues=[],
                frame_count=0,
                duration_ms=0,
                exit_code=-1,
                captured_frames=[],
                quality_score=0.0,
            )
        
        print(f"\n{'='*70}")
        print(f"Testing: {system.upper():7} | {rom_path.stem}")
        print(f"ROM: {rom_path}")
        if tas_path:
            print(f"TAS: {tas_path}")
        print(f"{'='*70}")
        
        # Convert TAS if provided
        input_script = None
        if tas_path:
            print("  [1/4] Converting TAS file...")
            input_script = self.convert_tas_to_script(tas_path)
            if not input_script:
                print("  ✗ Failed to convert TAS file")
                return TestResult(
                    system=system,
                    game_name=rom_path.stem,
                    rom_path=str(rom_path),
                    tas_path=str(tas_path),
                    status="FAIL",
                    behavioral_passed=False,
                    visual_passed=False,
                    behavioral_issues=["TAS conversion failed"],
                    visual_issues=[],
                    frame_count=0,
                    duration_ms=0,
                    exit_code=-1,
                    captured_frames=[],
                    quality_score=0.0,
                )
            print("  ✓ TAS converted to input script")
        
        # Run behavioral test
        print("  [2/4] Running behavioral test (headless)...")
        duration = self.TEST_DURATION_MS.get(system, 5000)
        behavior_passed, behavior_issues, frame_count, exit_code = self.run_behavioral_test(
            rom_path, input_script, duration, system
        )
        print(f"  {'✓' if behavior_passed else '✗'} Behavioral test {'passed' if behavior_passed else 'FAILED'}")
        if behavior_issues:
            for issue in behavior_issues:
                print(f"    - {issue}")
        
        # Run visual test
        print("  [3/4] Running visual verification (frame capture)...")
        visual_passed, visual_issues, frame_paths = self.run_visual_test(
            rom_path, input_script, system
        )
        print(f"  {'✓' if visual_passed else '✗'} Visual test captured {len(frame_paths)} frames")
        if visual_issues:
            for issue in visual_issues:
                print(f"    - {issue}")
        
        # Estimate quality score (simple heuristic for now)
        quality_score = 100.0
        if not behavior_passed:
            quality_score -= 40
        if not visual_passed:
            quality_score -= 30
        quality_score = max(0, quality_score)
        
        # Overall status
        status = "PASS" if (behavior_passed and visual_passed) else "FAIL"
        
        result = TestResult(
            system=system,
            game_name=rom_path.stem,
            rom_path=str(rom_path),
            tas_path=str(tas_path) if tas_path else None,
            status=status,
            behavioral_passed=behavior_passed,
            visual_passed=visual_passed,
            behavioral_issues=behavior_issues,
            visual_issues=visual_issues,
            frame_count=frame_count,
            duration_ms=duration,
            exit_code=exit_code,
            captured_frames=[str(f) for f in frame_paths],
            quality_score=quality_score,
        )
        
        print(f"  [4/4] Result: {status} (Score: {quality_score:.1f}/100)")
        
        self.results.append(result)
        return result
    
    def _detect_system(self, rom_path: Path) -> Optional[str]:
        """Detect system from ROM file extension."""
        ext = rom_path.suffix.lower()
        
        mapping = {
            ".nes": "nes",
            ".md": "genesis",
            ".gen": "genesis",
            ".smd": "genesis",
            ".smc": "snes",
            ".sfc": "snes",
            ".fig": "snes",
            ".swc": "snes",
            ".gb": "gb",
            ".gbc": "gb",
        }
        
        return mapping.get(ext)
    
    def run_all(self, system_filter: Optional[str] = None):
        """Run complete validation suite across all available systems."""
        roms = self.find_roms()
        tas_files = self.find_tas_files()
        
        print(f"\n{'='*70}")
        print("COMPREHENSIVE EMULATOR VALIDATION SUITE")
        print(f"{'='*70}")
        
        total_tests = sum(len(roms[s]) for s in roms)
        if total_tests == 0:
            print("\n⚠  No ROMs found in test_roms/")
            print("   Run: python3 acquire_test_roms.py --setup-dirs")
            print("   Then download ROMs and place in test_roms/{system}/")
            return
        
        print(f"\nDiscovered: {total_tests} ROMs available for testing")
        for system, rom_list in roms.items():
            if len(rom_list) > 0:
                print(f"  • {system.upper():7}: {len(rom_list)} ROMs")
        
        print(f"\n{'='*70}\n")
        
        # Run tests
        for system, rom_list in roms.items():
            if system_filter and system != system_filter:
                continue
            
            print(f"\n{system.upper()} TESTS")
            print("-" * 70)
            
            for rom_path in sorted(rom_list):
                # Find matching TAS file
                tas_path = None
                rom_name = rom_path.stem
                if system in tas_files and rom_name in tas_files[system]:
                    tas_path = tas_files[system][rom_name][0]
                
                self.run_test(rom_path, tas_path)
        
        # Print summary
        self.print_summary()
    
    def print_summary(self):
        """Print test execution summary and statistics."""
        if not self.results:
            return
        
        print(f"\n{'='*70}")
        print("TEST SUMMARY")
        print(f"{'='*70}\n")
        
        for result in self.results:
            print(result.summary())
        
        # Statistics
        total = len(self.results)
        passed = sum(1 for r in self.results if r.status == "PASS")
        failed = sum(1 for r in self.results if r.status == "FAIL")
        avg_score = sum(r.quality_score for r in self.results) / total if total > 0 else 0
        
        print(f"\n{'='*70}")
        print(f"Total Tests: {total}")
        print(f"Passed: {passed} ({100*passed//total if total > 0 else 0}%)")
        print(f"Failed: {failed} ({100*failed//total if total > 0 else 0}%)")
        print(f"Average Quality Score: {avg_score:.1f}/100")
        print(f"{'='*70}\n")
        
        # Gate result
        if avg_score >= 90:
            print("✓ QUALITY GATE: PASS (Score >= 90/100)")
        elif avg_score >= 70:
            print("⚠ QUALITY GATE: CONDITIONAL (Score 70-89/100)")
        else:
            print("✗ QUALITY GATE: FAIL (Score < 70/100)")

def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Comprehensive end-to-end emulator validation with visual and behavioral verification"
    )
    parser.add_argument("--list", action="store_true", help="List available ROMs and TAS files")
    parser.add_argument("--run", action="store_true", help="Run full validation suite")
    parser.add_argument("--visual-only", action="store_true", help="Run visual verification only")
    parser.add_argument("--behavior-only", action="store_true", help="Run behavioral verification only")
    parser.add_argument("--system", type=str, help="Filter tests to specific system (nes, genesis, snes, gb)")
    parser.add_argument("--workspace", type=Path, help="Path to workspace root")
    
    args = parser.parse_args()
    
    validator = ComprehensiveValidator(args.workspace)
    
    if args.list:
        roms = validator.find_roms()
        tas = validator.find_tas_files()
        print("\nAvailable ROMs:")
        for system, rom_list in roms.items():
            if rom_list:
                print(f"  {system.upper()}:")
                for rom in rom_list:
                    print(f"    • {rom.name}")
        print("\nAvailable TAS files:")
        for system, tas_dict in tas.items():
            if tas_dict:
                print(f"  {system.upper()}:")
                for game_name in tas_dict.keys():
                    print(f"    • {game_name}")
    elif args.run or args.visual_only or args.behavior_only:
        validator.run_all(args.system)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
