---
description: "AIO Server design token compliance rules for UI code."
applyTo: "src/gui/**/*.cpp,include/gui/**/*.h,include/gui/**/*.hpp,assets/qss/**/*.qss"
---

# Design System Compliance

Canonical source of truth: `.github/knowledge/design-system.md`. This file inlines the complete token inventory for in-context enforcement. ALL values used in UI code MUST come from the tables below. No other values are permitted.

## Color Palette

ONLY these colors are legal. Any hex value not in this table is a violation.

| Token           | Value                    | Usage                                          |
| --------------- | ------------------------ | ---------------------------------------------- |
| bg              | `#0a0a0a`                | Page / window background                       |
| surface-1       | `#121212`                | Cards, lists, inputs                           |
| surface-2       | `#1a1a1a`                | Buttons (default)                              |
| surface-3       | `#222222`                | Hover state surfaces                           |
| elevated        | `#2a2a2a`                | Elevated surfaces                              |
| border          | `rgba(255,255,255,0.06)` | Default borders                                |
| text            | `#f0f0f0`                | Primary text                                   |
| text-2          | `#999999`                | Secondary / subtitle text                      |
| text-3          | `#666666`                | Tertiary / metadata text                       |
| accent          | `#64b5f6`                | Primary accent (blue)                          |
| store-accent    | `#d4a820`                | Game Store tabs, install/play CTAs             |
| organize-accent | `#ffb74d`                | Home Screen organize-mode highlight            |
| youtube-brand   | `#ff0000`                | YouTube logo and active controls (youtube.qss) |
| gba-accent      | `#9c8cff`                | GBA border-left highlight                      |
| switch-accent   | `#ff6b6b`                | Switch accent color                            |
| danger-bg       | `rgba(220,50,50,0.16)`   | Danger background                              |
| danger-border   | `rgba(220,50,50,0.32)`   | Danger border                                  |

- Semantic alpha variants (e.g., `rgba(100, 181, 246, 0.22)`) are permitted ONLY when the base color is a palette color above.
- Do not introduce new colors. All additions require an update to `knowledge/design-system.md` first.

## Typography Scale

ONLY these font sizes are legal. Font family: "Noto Sans" everywhere. "Walter.ttf" for branding display only.

| Role     | Size | Weight | Usage                         |
| -------- | ---- | ------ | ----------------------------- |
| display  | 48px | 800    | Hero / splash headings        |
| heading  | 32px | 700    | Page titles                   |
| section  | 20px | 600    | Section headings, nav buttons |
| subtitle | 18px | 500    | Secondary heading variant     |
| body     | 16px | 500    | Default body text             |
| caption  | 14px | 500    | Cards, list items, inputs     |
| meta     | 13px | 500    | Eyebrow labels, tool buttons  |
| small    | 12px | 500    | Badges, fine print            |

- QSS `font-size` MUST use only: 12, 13, 14, 16, 18, 20, 32, 48px.
- C++ `setPixelSize()` MUST use a scale value or proportional sizing (`dimension * factor`). No arbitrary fixed pixels.

## Spacing Scale

ONLY these spacing values are legal: **4 · 8 · 12 · 16 · 24 · 32 · 48 · 64 px**

- ALL `padding`, `margin`, and `spacing` values MUST use the scale above.
- No intermediate values (e.g., `7px`, `10px`, `20px` are violations).

## Border Radii

ONLY these radii are legal:

| Token          | Value | Applied to                        |
| -------------- | ----- | --------------------------------- |
| badge          | 6px   | Small status badges               |
| list-item      | 8px   | QToolButton, list item overrides  |
| button / input | 12px  | QPushButton, QLineEdit, QComboBox |
| card           | 16px  | Game cards, content containers    |
| chip           | 20px  | Filter chips, tag pills           |

- No other radius values are permitted.

## QSS File Ownership

- Shared TV-shell styling belongs in `assets/qss/tv.qss`.
- YouTube-specific styling belongs in `assets/qss/youtube.qss`.
- Do not add cross-app styling to the wrong file.
