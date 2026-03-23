# AIO Server Design System

Canonical token reference for AIO Server TV shell. Source of truth: `assets/qss/tv.qss` header comment block.

## Color Palette

| Token     | Value                    | Usage                     |
| --------- | ------------------------ | ------------------------- |
| bg        | `#0a0a0a`                | Page / window background  |
| surface-1 | `#121212`                | Cards, lists, inputs      |
| surface-2 | `#1a1a1a`                | Buttons (default)         |
| surface-3 | `#222222`                | Hover state surfaces      |
| elevated  | `#2a2a2a`                | Elevated surfaces         |
| border    | `rgba(255,255,255,0.06)` | Default borders           |
| text      | `#f0f0f0`                | Primary text              |
| text-2    | `#999999`                | Secondary / subtitle text |
| text-3    | `#666666`                | Tertiary / metadata text  |
| accent    | `#64b5f6`                | Primary accent (blue)     |
| store-accent    | `#d4a820` | Game Store tabs, install/play CTAs           |
| organize-accent | `#ffb74d` | Home Screen organize-mode highlight           |
| youtube-brand   | `#ff0000` | YouTube logo and active controls (youtube.qss only) |
| store-accent    | `#d4a820` | Game Store tabs, install/play CTAs           |
| organize-accent | `#ffb74d` | Home Screen organize-mode highlight           |
| youtube-brand   | `#ff0000` | YouTube logo and active controls (youtube.qss only) |

Additional semantic colors used in tv.qss:

- GBA accent: `#9c8cff` (border-left highlight)
- PS1 accent: `#64b5f6` (same as accent)
- Switch accent: `#ff6b6b`
- Danger: `rgba(220,50,50,0.16)` background / `rgba(220,50,50,0.32)` border

## Typography Scale

Font family: **Noto Sans** (loaded via `QFontDatabase` at startup). Walter.ttf for branding display only.

| Role    | Size | Weight | Usage                         |
| ------- | ---- | ------ | ----------------------------- |
| display | 48px | 800    | Hero / splash headings        |
| heading | 32px | 700    | Page titles                   |
| section | 20px | 600    | Section headings, nav buttons |
| body    | 16px | 500    | Default body text             |
| caption | 14px | 500    | Cards, list items, inputs     |
| meta    | 13px | 500    | Eyebrow labels, tool buttons  |
| small   | 12px | 500    | Badges, fine print            |

Additional value in use: 18px (subtitle) — secondary heading variant.

### C++ Font Usage

No `AIOFonts` helper class exists. Qt font sizing in paint code uses:

- `QFont("Noto Sans")` + `setPixelSize(value)` with scale values, OR
- Proportional sizing: `setPixelSize(static_cast<int>(widgetDimension * factor))` — permitted for paint-rendered components where size adapts to widget bounds.

## Spacing Scale

4 · 8 · 12 · 16 · 24 · 32 · 48 · 64 (px)

No intermediate values. Deviations require explicit justification in code comments.

## Border Radii

| Token          | Value | Applied to                        |
| -------------- | ----- | --------------------------------- |
| badge          | 6px   | Small status badges               |
| list-item      | 8px   | QToolButton, list item overrides  |
| button / input | 12px  | QPushButton, QLineEdit, QComboBox |
| card           | 16px  | Game cards, content containers    |
| chip           | 20px  | Filter chips, tag pills           |
