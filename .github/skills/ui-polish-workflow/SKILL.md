---
name: ui-polish-workflow
description: "Design-brief-to-verified-output workflow for polishing AIO Server UI screens. Translates user requests into token-specific changes, applies fixes, and verifies through the visual audit loop."
user-invocable: false
---

# UI Polish Workflow

Use this skill when a screen or component needs visual improvement, design refinement, or AAA quality compliance.

## Workflow Phases

### Phase 1: Parse the Brief

Convert the user request into a structured design brief using the template in `artifacts/design-brief-template.md`.

1. Identify the target screen/page and affected elements.
2. Map the request to specific design tokens from `design-system.instructions.md`.
3. Define concrete success criteria — which token values, which properties, which elements.
4. If the request is vague (e.g., "make it look better"), inspect the current code and screenshots to identify the highest-impact token deviations, then create the brief yourself.

### Phase 2: Inspect Current State

1. Read the relevant QSS file (`tv.qss` or `youtube.qss`) and any C++ widget code for the target screen.
2. Compare current property values against the design token tables.
3. List every deviation: `file → selector → property → current value → target value`.
4. If visual capture is needed, use `visual-development-loop` to boot, navigate, and screenshot.

### Phase 3: Build the Punch List

Use the template in `artifacts/punch-list-template.md`. Prioritize fixes by visual impact:

1. **Critical** — Token violations (wrong colors, off-scale spacing, non-scale typography)
2. **High** — Layout/alignment issues visible at 10ft
3. **Medium** — Component inconsistencies, missing focus states
4. **Low** — Polish items (micro-spacing, transition refinement)

### Phase 4: Implement Fixes

Apply changes using the common fix patterns below. Work through the punch list in priority order.

### Phase 5: Capture and Judge

1. Build: `make build`
2. Boot: `python3 scripts/visual_dev_loop.py boot --no-focus`
3. Navigate to target screen, screenshot, judge against the AAA Visual Audit Standard.
4. If FAIL: generate fix-feedback list (file → selector → property → current → target), return to Phase 4.
5. If PASS (≥ 90/100): report the score and stop.

Capture at milestones (every 5–10 changes), not after every tweak.

## Common QSS Fix Patterns

### Color Token Replacement

```qss
/* Wrong — hardcoded hex not in palette */
color: #aaaaaa;
/* Right — use text-2 token */
color: #999999;

/* Wrong — invented background */
background-color: #1e1e1e;
/* Right — use surface-2 token */
background-color: #1a1a1a;
```

### Spacing Normalization

```qss
/* Wrong — off-scale value */
padding: 10px;
/* Right — nearest scale value */
padding: 12px;

/* Wrong — off-scale margin */
margin: 20px;
/* Right — nearest scale value */
margin: 24px;
```

### Typography Correction

```qss
/* Wrong — off-scale font size */
font-size: 15px;
/* Right — nearest scale value */
font-size: 16px;

/* Wrong — off-scale font size */
font-size: 22px;
/* Right — nearest scale value */
font-size: 20px;
```

### Border Radius Alignment

```qss
/* Wrong — non-standard radius */
border-radius: 10px;
/* Right — button/input token */
border-radius: 12px;

/* Wrong — non-standard radius */
border-radius: 14px;
/* Right — card token */
border-radius: 16px;
```

### Focus State Addition

```qss
/* Every interactive element needs a visible focus state */
QToolButton:focus {
    border: 2px solid #64b5f6;  /* accent token */
    border-radius: 8px;         /* list-item token */
}
```

## Rules

- Aesthetic direction comes from the user request or design brief, not this skill. This skill provides the mechanical process.
- ALL values MUST come from the token tables in `design-system.instructions.md`. No invented values.
- When the nearest scale value is ambiguous, prefer the smaller value to maintain tightness.
- TV.qss changes go in `assets/qss/tv.qss`. YouTube changes go in `assets/qss/youtube.qss`.
- Reference `artifacts/focus-and-behavior-checklist.md` for TV-specific interaction requirements.
