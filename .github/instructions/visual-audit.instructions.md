---
description: "AAA visual design audit standard for the AIO Server TV operating shell."
applyTo: "src/gui/**/*.cpp,include/gui/**/*.h,include/gui/**/*.hpp,assets/qss/**/*.qss,.github/agents/visual-engineer.agent.md"
---

# AAA Visual Design Audit Standard

AIO Server is a home-theater operating shell (Fire Stick replacement) hosting emulators, streaming services, NAS browser, and media apps. Non-emulation UI must match the production quality of Amazon Fire TV, Nintendo Switch, Xbox Dashboard, and Apple TV.

## Quality Gate

Score >= 90/100 to pass. Below 90 = below AAA professional standards.

## Scoring (7 categories, weighted)

| Category            | Weight | Evaluate                                                                         |
| ------------------- | ------ | -------------------------------------------------------------------------------- |
| Layout & Alignment  | 20%    | Grid discipline, column alignment, baseline alignment, consistent margins        |
| Typography          | 15%    | Font system, hierarchy (display/heading/body/caption/metadata), 10ft readability |
| Spacing & Rhythm    | 15%    | Consistent scale (4/8/12/16/24/32/48/64px), uniform padding, even gaps           |
| Visual Hierarchy    | 15%    | Clear focal point, primary vs secondary vs metadata, logical grouping            |
| Color & Contrast    | 10%    | Neutral base, 1-2 accents max, sufficient contrast, semantic color use           |
| Component Quality   | 15%    | Consistent radii, shadows, padding, icon style, card structure                   |
| Professional Polish | 10%    | Calmness, intentionality, visual balance, pixel precision, premium feel          |

Each category: 0–10. Final = Σ(category × weight × 10).
90–100 = AAA Production. 80–89 = Professional. 70–79 = Semi-Pro. <70 = Fail.

## Stage 0: Token Compliance Pre-Check

Before scoring any stage, read `.github/knowledge/design-system.md`. Audit the changed files for token violations:

**Deductions (cumulative, applied before Stage 1):**

- −5 per hardcoded hex color value not in the palette (e.g., `#1e1e1e`, `color: #fff`)
- −5 per `setPixelSize()` call in C++ using a fixed pixel value not from the typography scale (proportional sizing is exempt)
- −3 per spacing or margin value not on the spacing scale (4/8/12/16/24/32/48/64px)

**Score ceiling:** If cumulative Stage 0 deductions exceed 20, the maximum achievable score for this review is 70 (failing the 90/100 gate — return FAIL immediately and list all violations).

Stage 0 violations must be listed explicitly in the report, not aggregated.

## Stage 1: Critical Failures (instant fail, cap at 75)

Any of these → automatic failure regardless of category scores.

**Alignment** — Elements sharing a logical row/column don't share an axis (tolerance ≤2px). Cards in a row have different heights. Icons not centered in containers. Text baselines misaligned.

**Spacing** — Padding differs between identical components. Margins don't follow a consistent increment scale. Spacing looks visually random anywhere.

**Typography** — More than 2 font families visible. Font sizes random (no clear hierarchy). Text unreadable at 10-foot viewing distance. Inconsistent font weight usage.

**Color** — More than 3-4 distinct hues. Text-to-background contrast too low. Accent colors scattered randomly instead of semantically.

**Components** — Identical element types styled differently. Mixed corner radii on same-level components. Inconsistent shadows or icon language.

**Clutter** — Multiple elements competing equally for attention. Sections not clearly separated. Content overwhelms the interface chrome.

## Stage 2: Design System Validation

- **Component reuse**: Cards, buttons, nav, thumbnails look identical across instances. Differences = state only (focused/unfocused/active/disabled).
- **Structural consistency**: Layout zones (navigation, content, utility) recognizable and predictable.
- **Pattern repetition**: All rows share structure. All list items share layout. All dialogs follow same hierarchy.
- **Hierarchy clarity**: Primary > secondary > metadata visually distinct everywhere.

## Stage 3: TV Platform Rules (non-negotiable)

### Focus Navigation

- Every interactive element needs a clear, visible focus state.
- Focus obvious from 10 feet — scale increase, glow, outline, border, or shadow change.
- Unclear focus state → fail.

### D-pad / Remote Navigation

- Directional input produces predictable movement. No dead ends. No focus traps.
- Standard: horizontal row browsing + vertical category navigation (Netflix/Fire TV pattern).

### Content Dominance

- Thumbnails and media content dominate — chrome recedes.
- Consistent aspect ratios, clean cropping, high-res thumbnails.

### 10-Foot Readability

- Titles 32–48px, section labels 18–24px, body 14–16px, metadata 12–13px.
- If text is strained to read at typical TV distance → fail.

### Sub-App Integration

- Sub-apps adopt platform fonts, colors, and navigation style.
- Seamless entry/exit — user always knows how to return home.
- Platform overlays (settings, notifications) work above any sub-app.

## Stage 4: Professional Polish

The difference between amateur and AAA UI is **systematic discipline**, not features.

**Precision** — Pixel-perfect alignment. Consistent spacing system with no exceptions. Uniform component proportions.

**Calmness** — Dark neutral base (#000, #111, #1A1A1A, #222). 1 primary accent + 1 optional highlight max. UI feels calm, spacious, intentional — never busy or noisy.

**Visual Balance** — Even distribution of visual weight. No heavy clustering. Whitespace balanced — not cramped, not wastefully empty.

**Iconography** — Same stroke weight, same visual language. No mixed styles (outline vs filled, thick vs thin). Sharp, crisp rendering.

**Intentionality** — Every element appears deliberately placed. No stray pixels, orphan labels, or accidental artifacts. If you can ask "was this deliberate?" — it's probably not polished enough.

## Stage 5: Emulation Views (emulator sub-app only)

Emulation must look and behave exactly like original hardware as defined by official technical documentation. Non-negotiable.

- Pixel-accurate rendering to original console output per official specs.
- Timing and behavior matching documented hardware behavior.
- Any visual or behavioral discrepancy from documented specs = immediate failure. Find the actual cause and fix every part of the problem.
- Reference official technical documentation (developer manuals, hardware specs) — not other emulators, not hardware we don't have access to.

## Required Output Format

```
UI QUALITY AUDIT REPORT

Overall Score: XX / 100
Rating: [AAA Production / Professional / Semi-Professional / Fail]

Category Scores:
  Layout & Alignment: X/10
  Typography: X/10
  Spacing & Rhythm: X/10
  Visual Hierarchy: X/10
  Color & Contrast: X/10
  Component Quality: X/10
  Professional Polish: X/10

Critical Failures: [none | list]
Design System: [strong | moderate | weak]
Focus Navigation: [clear | unclear | N/A]

Issues (prioritized):
1. [element] — [what's wrong] — [exact fix]
2. ...

Verdict: [Matches AAA quality / Below AAA standards]
```

## Fix Mapping

When a scoring category is below 8/10, check these specific QSS/C++ properties against the design token tables in `design-system.instructions.md`:

| Category            | QSS Properties to Check                                             | Token Table         |
| ------------------- | ------------------------------------------------------------------- | ------------------- |
| Layout & Alignment  | `margin`, `padding`, `min-height`, `max-width`, `qproperty-*`       | Spacing Scale       |
| Typography          | `font-size`, `font-weight`, `font-family`                           | Typography Scale    |
| Spacing & Rhythm    | `padding`, `margin`, `spacing`, `gap` (C++ layout `setSpacing`)     | Spacing Scale       |
| Visual Hierarchy    | `font-size`, `font-weight`, `color`, `opacity`                      | Typography + Colors |
| Color & Contrast    | `color`, `background-color`, `border-color`                         | Color Palette       |
| Component Quality   | `border-radius`, `border`, `padding`, `min-height`                  | Radii + Spacing     |
| Professional Polish | All of the above — look for inconsistencies across similar elements | All token tables    |

For each issue, generate a fix instruction in the format: `file → selector → property → current value → target value`.

## Benchmark References

Compare against: Nintendo Switch home, Xbox Series X dashboard, Amazon Fire TV, Android TV, Apple TV. The UI must feel like it could have been shipped by one of these companies.
