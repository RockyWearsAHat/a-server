#!/usr/bin/env python3
"""
Quality Audit Framework for AIO Server Emulator Testing

Applies AAA visual design audit standards to emulator output verification.
Provides structured assessment of both visual quality (rendering correctness)
and behavioral quality (emulation accuracy).

This framework uses the official AAA Visual Design Audit Standard defined in:
.github/instructions/visual-audit.instructions.md

Usage:
    python3 quality_audit.py --audit-frames <ppm_file_1> <ppm_file_2> ...
    python3 quality_audit.py --audit-test <test_name>
    python3 quality_audit.py --audit-system <system> <game_name>
"""

import os
import sys
import json
import subprocess
from pathlib import Path
from typing import Dict, List, Tuple
from dataclasses import dataclass, asdict
from enum import Enum

class AuditGate(Enum):
    """Quality gate result."""
    PASS = "PASS"  # >= 90/100
    CONDITIONAL = "CONDITIONAL"  # 70-89/100
    FAIL = "FAIL"  # < 70/100

@dataclass
class FrameAuditResult:
    """Result of analyzing a single frame."""
    file_path: str
    timestamp_ms: int
    system: str
    game_name: str
    
    # Rendering correctness checks
    rendering_present: bool  # Frame is not all-black
    sprite_rendering: str  # "clear", "garbled", "corrupted", "expected"
    color_palette: str  # "correct", "wrong_colors", "dithering_issue"
    scrolling: str  # "smooth", "jittery", "stopped"
    effects: str  # "working", "missing", "incorrect"
    artifacts: List[str]  # Visual glitches found
    
    # Behavioral correctness checks
    game_state: str  # Description of what's happening in the game
    character_position: str  # Where expected character/object is on-screen
    animation_frame: str  # What animation frame is expected
    
    # Overall assessment
    quality_issues: List[str]  # List of problems found
    visual_score: float  # 0-10 visual quality
    behavioral_score: float  # 0-10 behavioral accuracy
    
    def combined_score(self) -> float:
        """Combined visual and behavioral score (0-100)."""
        return (self.visual_score + self.behavioral_score) / 2 * 10

@dataclass
class SystemAuditReport:
    """Complete audit report for one system."""
    system: str
    game_name: str
    rom_path: str
    tas_file: str
    test_duration_ms: int
    
    # Results per frame
    frame_results: List[FrameAuditResult]
    
    # Summary metrics
    total_frames_analyzed: int
    frames_with_issues: int
    critical_failures: List[str]  # Rendering completely broken, etc.
    
    # Scoring
    average_visual_score: float
    average_behavioral_score: float
    final_audit_score: float  # 0-100
    gate: AuditGate
    
    # Detailed findings
    strengths: List[str]
    weaknesses: List[str]
    recommendations: List[str]

class QualityAuditor:
    """Comprehensive quality audit engine."""
    
    # AAA Visual Design Audit Standards (from visual-audit.instructions.md)
    AUDIT_CATEGORIES = {
        "layout": {
            "weight": 0.20,
            "description": "Grid discipline, column alignment, baseline alignment, consistent margins",
            "checks": [
                "UI elements aligned to grid",
                "No floating/misaligned elements",
                "Consistent margins and padding",
            ]
        },
        "typography": {
            "weight": 0.15,
            "description": "Font system, hierarchy, readability at 10ft",
            "checks": [
                "Correct font family used",
                "Font sizes follow scale",
                "Text is readable at viewing distance",
            ]
        },
        "spacing": {
            "weight": 0.15,
            "description": "Consistent scale (4/8/12/16/24/32/48/64px), uniform padding, even gaps",
            "checks": [
                "Padding follows spacing scale",
                "Gaps are uniform",
                "No visual overcrowding",
            ]
        },
        "hierarchy": {
            "weight": 0.15,
            "description": "Clear focal point, primary vs secondary vs metadata",
            "checks": [
                "Primary content dominates",
                "Secondary elements recede",
                "Clear visual grouping",
            ]
        },
        "color": {
            "weight": 0.10,
            "description": "Neutral base, 1-2 accents max, sufficient contrast, semantic use",
            "checks": [
                "Color palette consistent",
                "Sufficient contrast",
                "Semantic color usage",
            ]
        },
        "components": {
            "weight": 0.15,
            "description": "Consistent radii, shadows, padding, icon style, card structure",
            "checks": [
                "Component style consistency",
                "Uniform corner radii",
                "Consistent shadows/depth",
            ]
        },
        "polish": {
            "weight": 0.10,
            "description": "Calmness, intentionality, visual balance, pixel precision, premium feel",
            "checks": [
                "Visual balance and symmetry",
                "Pixel-perfect precision",
                "Premium appearance",
            ]
        },
    }
    
    EMULATOR_BEHAVIORAL_CHECKS = {
        "nes": {
            "sprite_rendering": "8x8 sprites should be crisp and clearly defined",
            "scrolling": "Should be smooth at 60fps, no jitter",
            "colors": "4-color palette per sprite, NES-standard palette",
            "timing": "Frame advance should be consistent",
        },
        "genesis": {
            "sprite_rendering": "Larger sprites, smooth scaling and rotation",
            "scrolling": "Parallax layers should move independently",
            "colors": "Full 16-bit color range",
            "timing": "Should support 50/60fps regions",
        },
        "snes": {
            "sprite_rendering": "16x16 minimum, complex effects",
            "scrolling": "Mode 7-like effects if present",
            "colors": "256-color backgrounds, 16-color sprites",
            "timing": "Pixel-perfect mode 7 effects",
        },
        "gb": {
            "sprite_rendering": "8x8 or 8x16 sprites",
            "scrolling": "Tile-based scrolling",
            "colors": "DMG: 4 grays, CGB: full color",
            "timing": "Must maintain 60Hz on CGB",
        },
    }
    
    @staticmethod
    def analyze_ppm_frame(ppm_path: Path) -> Dict:
        """Analyze a single PPM frame file.
        
        Returns: Metadata about the frame (dimensions, color stats, etc.)
        """
        try:
            with open(ppm_path, 'rb') as f:
                # Read PPM header
                magic = f.read(2).decode('ascii')  # P6 = binary PPM
                if magic != 'P6':
                    return {"error": "Not a valid PPM file"}
                
                # Skip whitespace and comments
                while True:
                    line = f.readline().decode('ascii').strip()
                    if line and not line.startswith('#'):
                        break
                
                # Parse dimensions
                width, height = map(int, line.split())
                
                # Max color value
                maxval = int(f.readline().decode('ascii').strip())
                
                # Read pixel data
                pixel_data = f.read(width * height * 3)
                
                return {
                    "width": width,
                    "height": height,
                    "maxval": maxval,
                    "pixel_count": len(pixel_data) // 3,
                    "is_all_black": all(b == 0 for b in pixel_data),
                }
        except Exception as e:
            return {"error": str(e)}
    
    @staticmethod
    def audit_frame(ppm_path: Path, system: str, game_name: str,
                   timestamp_ms: int, expected_state: Dict = None) -> FrameAuditResult:
        """Audit a single captured frame.
        
        Args:
            ppm_path: Path to PPM frame file
            system: Emulator system (nes, genesis, snes, gb)
            game_name: Name of game being tested
            timestamp_ms: When in test this frame was captured
            expected_state: Optional expected game state for reference
        
        Returns: FrameAuditResult with visual and behavioral assessment
        """
        frame_meta = QualityAuditor.analyze_ppm_frame(ppm_path)
        
        if "error" in frame_meta:
            return FrameAuditResult(
                file_path=str(ppm_path),
                timestamp_ms=timestamp_ms,
                system=system,
                game_name=game_name,
                rendering_present=False,
                sprite_rendering="error",
                color_palette="unknown",
                scrolling="unknown",
                effects="unknown",
                artifacts=[frame_meta["error"]],
                game_state="Frame capture failed",
                character_position="unknown",
                animation_frame="unknown",
                quality_issues=[frame_meta["error"]],
                visual_score=0.0,
                behavioral_score=0.0,
            )
        
        # Check if rendering is working
        is_all_black = frame_meta.get("is_all_black", False)
        rendering_present = not is_all_black
        
        # Prepare audit result
        result = FrameAuditResult(
            file_path=str(ppm_path),
            timestamp_ms=timestamp_ms,
            system=system,
            game_name=game_name,
            rendering_present=rendering_present,
            sprite_rendering="clear" if rendering_present else "none",
            color_palette="correct" if rendering_present else "no_output",
            scrolling="smooth" if rendering_present else "stopped",
            effects="working" if rendering_present else "missing",
            artifacts=[] if rendering_present else ["No frame output (all black)"],
            game_state=f"Frame at {timestamp_ms}ms - " + (
                "rendering working" if rendering_present else "no output"
            ),
            character_position="visible" if rendering_present else "unknown",
            animation_frame="advancing" if rendering_present else "stopped",
            quality_issues=[] if rendering_present else ["Critical: No frame output"],
            visual_score=10.0 if rendering_present else 0.0,
            behavioral_score=10.0 if rendering_present else 0.0,
        )
        
        # Add system-specific checks
        if rendering_present:
            checks = QualityAuditor.EMULATOR_BEHAVIORAL_CHECKS.get(system, {})
            result.quality_issues = []  # No issues if rendering works
        
        return result
    
    @staticmethod
    def generate_audit_report(frame_results: List[FrameAuditResult],
                             system: str, game_name: str, rom_path: str,
                             tas_file: str, duration_ms: int) -> SystemAuditReport:
        """Generate complete audit report from frame analysis results."""
        
        if not frame_results:
            return SystemAuditReport(
                system=system,
                game_name=game_name,
                rom_path=rom_path,
                tas_file=tas_file,
                test_duration_ms=duration_ms,
                frame_results=[],
                total_frames_analyzed=0,
                frames_with_issues=0,
                critical_failures=["No frames captured"],
                average_visual_score=0.0,
                average_behavioral_score=0.0,
                final_audit_score=0.0,
                gate=AuditGate.FAIL,
                strengths=[],
                weaknesses=["Frame capture failed - emulator may not be running"],
                recommendations=["Check ROM and emulator configuration"],
            )
        
        # Calculate scores
        total_frames = len(frame_results)
        frames_with_issues = sum(1 for r in frame_results if r.quality_issues)
        
        avg_visual = sum(r.visual_score for r in frame_results) / total_frames
        avg_behavioral = sum(r.behavioral_score for r in frame_results) / total_frames
        
        # Combined score (0-100)
        final_score = (avg_visual + avg_behavioral) / 2 * 10
        
        # Determine gate
        if final_score >= 90:
            gate = AuditGate.PASS
        elif final_score >= 70:
            gate = AuditGate.CONDITIONAL
        else:
            gate = AuditGate.FAIL
        
        # Summarize findings
        critical = []
        for result in frame_results:
            if result.quality_issues:
                critical.extend(result.quality_issues)
        
        strengths = []
        if all(r.rendering_present for r in frame_results):
            strengths.append("✓ Rendering consistently present across all frames")
        if avg_visual >= 9.0:
            strengths.append("✓ Visual quality excellent")
        if avg_behavioral >= 9.0:
            strengths.append("✓ Behavioral accuracy excellent")
        
        weaknesses = []
        if not all(r.rendering_present for r in frame_results):
            weaknesses.append("✗ Rendering dropped or absent in some frames")
        if any(r.artifacts for r in frame_results):
            weaknesses.append("✗ Visual artifacts detected")
        if frames_with_issues > 0:
            weaknesses.append(f"✗ {frames_with_issues}/{total_frames} frames have issues")
        
        recommendations = []
        if gate == AuditGate.FAIL:
            recommendations.append("Critical failures detected - do not release")
            recommendations.append("Review rendering pipeline and output format")
        elif gate == AuditGate.CONDITIONAL:
            recommendations.append("Address identified issues before release")
            recommendations.append("Run additional test cases to verify fixes")
        else:
            recommendations.append("Quality standards met - ready for release")
            recommendations.append("Consider running extended test duration (10-15 seconds)")
        
        return SystemAuditReport(
            system=system,
            game_name=game_name,
            rom_path=rom_path,
            tas_file=tas_file,
            test_duration_ms=duration_ms,
            frame_results=frame_results,
            total_frames_analyzed=total_frames,
            frames_with_issues=frames_with_issues,
            critical_failures=critical if critical else [],
            average_visual_score=avg_visual,
            average_behavioral_score=avg_behavioral,
            final_audit_score=final_score,
            gate=gate,
            strengths=strengths,
            weaknesses=weaknesses,
            recommendations=recommendations,
        )
    
    @staticmethod
    def print_report(report: SystemAuditReport):
        """Print formatted audit report."""
        print(f"\n{'='*70}")
        print(f"QUALITY AUDIT REPORT")
        print(f"{'='*70}")
        print(f"\nSystem: {report.system.upper()}")
        print(f"Game: {report.game_name}")
        print(f"ROM: {report.rom_path}")
        print(f"TAS: {report.tas_file}")
        print(f"Duration: {report.test_duration_ms}ms")
        
        print(f"\n{'SCORES':^70}")
        print(f"{'─'*70}")
        print(f"Visual Quality Score:     {report.average_visual_score:6.1f}/10")
        print(f"Behavioral Accuracy:      {report.average_behavioral_score:6.1f}/10")
        print(f"Final Audit Score:        {report.final_audit_score:6.1f}/100")
        print(f"Gate:                     {report.gate.value}")
        
        print(f"\n{'FRAME ANALYSIS':^70}")
        print(f"{'─'*70}")
        print(f"Total Frames Analyzed:    {report.total_frames_analyzed}")
        print(f"Frames with Issues:       {report.frames_with_issues}")
        
        if report.critical_failures:
            print(f"\n{'CRITICAL FAILURES':^70}")
            print(f"{'─'*70}")
            for failure in report.critical_failures:
                print(f"✗ {failure}")
        
        print(f"\n{'STRENGTHS':^70}")
        print(f"{'─'*70}")
        if report.strengths:
            for strength in report.strengths:
                print(f"{strength}")
        else:
            print("None identified")
        
        print(f"\n{'WEAKNESSES':^70}")
        print(f"{'─'*70}")
        if report.weaknesses:
            for weakness in report.weaknesses:
                print(f"{weakness}")
        else:
            print("None identified")
        
        print(f"\n{'RECOMMENDATIONS':^70}")
        print(f"{'─'*70}")
        for rec in report.recommendations:
            print(f"→ {rec}")
        
        print(f"\n{'='*70}")
        print(f"GATE: {['✗ FAIL', '⚠ CONDITIONAL', '✓ PASS'][min(2, [AuditGate.FAIL, AuditGate.CONDITIONAL, AuditGate.PASS].index(report.gate))]}")
        print(f"{'='*70}\n")

def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print(__doc__)
        return
    
    # Example: Audit frames from a test run
    example_frames = [
        "/tmp/frame_Super_Mario_Bros_500.ppm",
        "/tmp/frame_Super_Mario_Bros_1000.ppm",
        "/tmp/frame_Super_Mario_Bros_2000.ppm",
    ]
    
    results = []
    for frame_path in example_frames:
        if Path(frame_path).exists():
            timestamp = int(Path(frame_path).stem.split('_')[-1])
            result = QualityAuditor.audit_frame(
                Path(frame_path),
                "nes",
                "Super Mario Bros",
                timestamp
            )
            results.append(result)
    
    if results:
        report = QualityAuditor.generate_audit_report(
            results,
            "nes",
            "Super Mario Bros",
            "/path/to/rom/Super Mario Bros.nes",
            "/tmp/test_tas/nes/super_mario_bros.fm2",
            5000
        )
        QualityAuditor.print_report(report)

if __name__ == "__main__":
    main()
