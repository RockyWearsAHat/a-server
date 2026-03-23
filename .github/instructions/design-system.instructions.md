---
description: "AIO Server design token compliance rules for UI code."
applyTo: "src/gui/**/*.cpp,include/gui/**/*.h,include/gui/**/*.hpp,assets/qss/**/*.qss"
---

# Design System Compliance

Token reference: `.github/knowledge/design-system.md` — read before writing or reviewing any UI code.

## Colors

- Do not hardcode hex color values in C++ or QSS unless that exact value is listed in the palette in `knowledge/design-system.md`.
- Semantic alpha variants of palette colors (e.g., `rgba(100, 181, 246, 0.22)`) are permitted only when the base color is a palette color.
- Do not introduce new colors. All additions require an update to `knowledge/design-system.md` first.

## Typography

- QSS `font-size` values must use only values from the typography scale: 12, 13, 14, 16, 18, 20, 32, 48px.
- Do not use arbitrary font sizes (e.g., `font-size: 11px`, `font-size: 22px`).
- C++ paint code using `setPixelSize()` must use either a value from the typography scale or proportional sizing based on widget dimensions. Arbitrary fixed pixel values not in the scale are prohibited.
- Font family: "Noto Sans" everywhere. "Walter.ttf" is permitted for branding-specific display contexts only.

## Spacing

- Padding and margin values must use only the spacing scale: 4, 8, 12, 16, 24, 32, 48, 64px.
- Do not use arbitrary spacing values (e.g., `padding: 7px`, `margin: 10px`, `padding: 20px`).

## Border Radii

- Use only radii defined in `knowledge/design-system.md`: 6px (badge), 8px (list-item / tool-button), 12px (button / input), 16px (card / list container), 20px (chip).
- Do not introduce new radius values.

## QSS File Ownership

- Shared TV-shell styling belongs in `assets/qss/tv.qss`.
- YouTube-specific styling belongs in `assets/qss/youtube.qss`.
- Do not add cross-app styling to the wrong file.
