# End-to-End Emulator Validation with Visual & Behavioral Quality Standards

## Overview

This comprehensive testing framework combines three pillars of emulator verification:

1. **Behavioral Testing** — Ensures emulation produces correct state transitions, frame counts, and exit codes
2. **Visual Verification** — Captures rendered output frames and verifies graphics are rendering correctly
3. **Quality Auditing** — Applies AAA visual design standards to assess rendering quality (0-100 score)

The framework uses:
- **Real ROMs** from https://www.romsgames.net/ (a community ROM database)
- **Real TAS files** from https://www.tasvideos.org (verified tool-assisted speedruns)
- **AIO Server's headless mode** for deterministic, verifiable emulation
- **Frame capture & analysis** for visual quality assessment

## System Requirements

- AIO Server must be built: `make build`
- Python 3.7+
- All required Python modules (standard library only — no external dependencies)

## Quick Start (15 minutes)

### 1. Setup ROM/TAS directories

```bash
python3 scripts/acquire_test_roms.py --setup-dirs
```

Creates:
- `test_roms/{nes,genesis,snes,gb}/` — For your ROM files
- `/tmp/test_tas/{nes,genesis,snes,gb}/` — For TAS files

### 2. Obtain ROMs

Visit https://www.romsgames.net/ and download one representative game per system:

| System | Recommended | Why |
|--------|------------|-----|
| NES | Super Mario Bros | Well-documented, TAS files abundant |
| Genesis | Sonic the Hedgehog 2 | Tests sprite rendering, scrolling |
| SNES | Super Mario Bros. 3 | Complex graphics, mode effects |
| Game Boy | Tetris | Simple, deterministic, frame-accurate |

Place in: `test_roms/{system}/Game Name.ext`

### 3. Obtain TAS files

Visit https://www.tasvideos.org and search for your game + "TAS":
- Search: "Super Mario Bros TAS"
- Download: Super Mario Bros. NES (FM2 format)
- Place in: `/tmp/test_tas/nes/Super Mario Bros.fm2`

Do this for each ROM you downloaded.

### 4. Run validation suite

```bash
python3 scripts/comprehensive_validation.py --run
```

The framework will:
- Discover all ROMs and TAS files
- Convert TAS files to AIO input script format
- Run each ROM in headless mode with TAS inputs
- Capture frames at 500ms, 1000ms, 2000ms, 3000ms, 5000ms markers
- Analyze frames for rendering correctness
- Generate quality audit report per AAA standards

## Detailed Workflow

### Phase 1: ROM Acquisition

**Option A: Manual Download**

1. Visit https://www.romsgames.net/
2. Search for your desired game
3. Download ROM file
4. Move to `test_roms/{system}/` directory with correct extension

**Option B: Scripted Guidance**

```bash
python3 scripts/acquire_test_roms.py --systems
python3 scripts/acquire_test_roms.py --guide
```

Shows detailed instructions, recommended games, and sourcing guidelines.

### Phase 2: TAS File Discovery

1. Go to https://www.tasvideos.org/
2. Select system (NES, Genesis, SNES, Game Boy)
3. Browse or search for games matching your ROMs
4. Download `.fm2`, `.fm3`, or `.r08` file
5. Move to `/tmp/test_tas/{system}/` directory

**Example:**
- ROM: `test_roms/nes/Super Mario Bros.nes`
- TAS: `/tmp/test_tas/nes/Super Mario Bros.fm2`

### Phase 3: Behavioral Testing

The comprehensive validator runs each ROM with TAS inputs in headless mode:

```bash
./build/bin/AIOServer --headless \
  --rom test_roms/nes/Super\ Mario\ Bros.nes \
  --input-script /tmp/inputs.script \
  --headless-max-ms 5000 \
  --headless-assert-nonblack
```

**Checks:**
- ✓ Exit code is 0 (no crashes)
- ✓ Exit code is non-zero with specific reason (intended error)
- ✓ Output is not all-black (rendering working)
- ✓ Frame count advances correctly
- ✓ No segmentation faults or assertion failures
- ✓ Emulation runs for full requested duration

### Phase 4: Visual Verification

At 5 timepoints during emulation (500ms, 1000ms, 2000ms, 3000ms, 5000ms), the framework:

1. **Captures a frame** (PPM format)
   ```bash
   ./build/bin/AIOServer --headless \
     --rom <rom> \
     --input-script <script> \
     --headless-dump-ppm /tmp/frame_500.ppm \
     --headless-dump-ms 500
   ```

2. **Analyzes the PPM file** to verify:
   - Frame is not all-black (output exists)
   - Dimensions match expected (e.g., 256×224 for NES)
   - Color range is used (not monochrome)
   - Visual artifacts are absent

3. **Compares to expected game state**:
   - Sprite positioning (character should be visible)
   - Animation frames (should be advancing with input)
   - Scroll position (parallax should work)
   - UI elements (score, lives, etc.)

### Phase 5: Quality Audit

The audit framework applies **AAA Visual Design Audit Standards** (from `.github/instructions/visual-audit.instructions.md`):

**7 Weighted Categories:**

| Category | Weight | Assessed By |
|----------|--------|-------------|
| Layout & Alignment | 20% | Grid discipline, element positioning |
| Typography | 15% | Font hierarchy, readability |
| Spacing & Rhythm | 15% | Consistent padding, visual balance |
| Visual Hierarchy | 15% | Primary vs secondary content |
| Color & Contrast | 10% | Palette consistency, contrast ratios |
| Component Quality | 15% | Sprite consistency, effects quality |
| Professional Polish | 10% | Calmness, intentionality, precision |

**Scoring:**
- **90-100**: ✓ AAA Production Quality
- **80-89**: Professional Grade
- **70-79**: Semi-Professional (conditional pass)
- **< 70**: ✗ Fail (critical issues)

**Example Output:**
```
QUALITY AUDIT REPORT
System: NES
Game: Super Mario Bros
ROM: test_roms/nes/Super Mario Bros.nes
TAS: /tmp/test_tas/nes/Super Mario Bros.fm2

Visual Quality Score:     9.8/10
Behavioral Accuracy:      9.9/10
Final Audit Score:        97.2/100
Gate:                     ✓ PASS

Frames Analyzed: 5
Critical Failures: None

Strengths:
✓ Rendering consistently present across all frames
✓ Visual quality excellent
✓ Behavioral accuracy excellent

Recommendations:
→ Quality standards met - ready for release
→ Consider running extended test duration (10-15 seconds)
```

## Running the Complete Suite

### Command Reference

```bash
# List available ROMs and TAS files
python3 scripts/comprehensive_validation.py --list

# Run all tests
python3 scripts/comprehensive_validation.py --run

# Run tests for specific system only
python3 scripts/comprehensive_validation.py --run --system nes

# Run with custom workspace path
python3 scripts/comprehensive_validation.py --run --workspace /path/to/workspace
```

### Sample Output

```
==============================================================================
COMPREHENSIVE EMULATOR VALIDATION SUITE
==============================================================================

Discovered: 4 ROMs available for testing
  • NES:     1 ROMs
  • GENESIS: 1 ROMs
  • SNES:    1 ROMs
  • GB:      1 ROMs

==============================================================================

NES TESTS
──────────────────────────────────────────────────────────────────────────

Testing:  NES    | Super Mario Bros
ROM: test_roms/nes/Super Mario Bros.nes
TAS: /tmp/test_tas/nes/Super Mario Bros.fm2
======================================================================
  [1/4] Converting TAS file...
  ✓ TAS converted to input script
  [2/4] Running behavioral test (headless)...
  ✓ Behavioral test passed
  [3/4] Running visual verification (frame capture)...
  ✓ Visual test captured 5 frames
  [4/4] Result: PASS (Score: 97.2/100)

[Additional systems follow...]

==============================================================================
TEST SUMMARY
==============================================================================

✓  NES       Super Mario Bros                 (Behavior: PASS, Visual: PASS, Score: 97.2/100)
✓  GENESIS   Sonic the Hedgehog 2             (Behavior: PASS, Visual: PASS, Score: 95.1/100)
✓  SNES      Super Mario Bros. 3              (Behavior: PASS, Visual: PASS, Score: 93.7/100)
✓  GB        Tetris                           (Behavior: PASS, Visual: PASS, Score: 91.5/100)

==============================================================================
Total Tests: 4
Passed: 4 (100%)
Failed: 0 (0%)
Average Quality Score: 94.4/100
==============================================================================

✓ QUALITY GATE: PASS (Score >= 90/100)
```

## Understanding Test Results

### Behavioral Test Passes When:
- Exit code is 0 (no crashes)
- Emulation ran for requested duration without hanging
- No all-black frame output
- No assertion failures or errors

### Visual Test Passes When:
- Frames were successfully captured at specified timestamps
- Frames are not all-black (graphics are rendering)
- Frame dimensions match expected (no resolution issues)
- Visual artifacts are minimal

### Quality Audit Passes When:
- Final score >= 90/100
- Both visual quality and behavioral accuracy are high
- No critical failures (rendering broken, corrupted output, etc.)

### Common Failures & Solutions

| Failure | Cause | Solution |
|---------|-------|----------|
| "All-black frames" | Graphics not rendering | Check GPU/headless flag support |
| "Exit code non-zero" | Emulator crash | Check ROM file integrity, encoding |
| "Black assert" | Headless mode needs GPU | Verify `--headless-assert-nonblack` output |
| "Frame capture failed" | Timing issue | Increase `--headless-max-ms` buffer |
| "TAS conversion failed" | Bad format | Verify TAS file from TASVideos.org |

## Frame Output Analysis

When visual verification runs, it produces PPM (Portable PixMap) files:

```
/tmp/frame_{game}_{timestamp}.ppm
```

Example: `/tmp/frame_Super_Mario_Bros_1000.ppm` = Mario Bros at 1000ms mark

These can be:
1. **Viewed directly** in any image viewer (PPM is uncompressed RGB)
2. **Analyzed programmatically** for color stats, dimensions, artifacts
3. **Compared** to reference expectations for that game/timestamp

The comprehensive_validation.py script analyzes them automatically.

## Advanced Usage

### Running Single ROM Test

```bash
python3 scripts/comprehensive_validation.py --test-rom test_roms/nes/Super\ Mario\ Bros.nes
```

### Custom Test Duration

Modify `TEST_DURATION_MS` in comprehensive_validation.py or use:

```python
validator.TEST_DURATION_MS["nes"] = 10000  # 10 seconds instead of 5
```

### Behavioral-Only or Visual-Only Tests

```bash
# Behavioral verification only (no frame capture)
python3 scripts/comprehensive_validation.py --behavior-only

# Visual verification only (assumes input scripts already converted)
python3 scripts/comprehensive_validation.py --visual-only
```

### Manual TAS Conversion

If you want to manually convert a TAS file:

```bash
python3 scripts/tas_converter.py /tmp/test_tas/nes/game.fm2 fm2 60 > /tmp/inputs.script
cat /tmp/inputs.script
```

Output:
```
# Game: NES TAS
# Converted by tas_converter.py
# Frame rate: 60 Hz

0 A DOWN
250 B DOWN
333 RIGHT DOWN
500 START DOWN
583 A UP
666 UP DOWN
750 LEFT DOWN
...
```

## Quality Standards Reference

The audit framework uses the same standards as AIO Server's official UI design system:

- **Precision**: Pixel-perfect alignment, no anti-aliasing artifacts
- **System Consistency**: All emulators follow same rendering pipeline
- **Performance**: Frame capture doesn't block emulation
- **Determinism**: Same ROM + TAS input = same frame output every time

For full specification, see: `.github/instructions/visual-audit.instructions.md`

## Troubleshooting

### ROMs Not Found
```bash
python3 scripts/acquire_test_roms.py --setup-dirs
```
Then manually place ROM files in the directories it creates.

### TAS Files Not Converted
```bash
python3 scripts/tas_converter.py /tmp/test.fm2
```
Verify TAS file is from TASVideos.org (format must be exact).

### Frames All Black
1. Verify ROM runs in non-headless mode: `./build/bin/AIOServer`
2. Check headless GPU support on your platform
3. Try `--headless-assert-nonblack` to see exact error

### Tests Timeout
Increase `TEST_DURATION_MS` or `--headless-max-ms` value.

### Memory Issues
Reduce number of frame captures or test duration.

## Next Steps

1. ✅ Build AIO Server: `make build`
2. ✅ Setup directories: `python3 scripts/acquire_test_roms.py --setup-dirs`
3. ✅ Acquire ROMs from https://www.romsgames.net/
4. ✅ Acquire TAS from https://www.tasvideos.org
5. ✅ Run validation: `python3 scripts/comprehensive_validation.py --run`
6. ✅ Review audit report
7. ✅ Fix any critical failures based on recommendations

## Documentation Files

- **TAS_TESTING_README.md** — Basic TAS validation (simpler workflow)
- **TAS_VALIDATION_IMPLEMENTATION.md** — Technical architecture details
- **COMPREHENSIVE_VALIDATION.md** — This file (full visual + behavioral)
- **quality_audit.py** — AAA audit standard implementation
- **comprehensive_validation.py** — Main orchestrator
- **acquire_test_roms.py** — ROM sourcing and setup

## References

- TAS Files: https://www.tasvideos.org/
- ROM Database: https://www.romsgames.net/
- AAA Design Standards: `.github/instructions/visual-audit.instructions.md`
- Emulator Architecture: `.github/knowledge/gba-emulator-architecture.md`, etc.

---

**Last Updated**: 2026-04-16
**Framework Version**: 2.0 (Visual + Behavioral + Quality Audit)
