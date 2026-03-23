---
description: "Qt widget, QSS, and rendered-output workflow for AIO Server UI work."
applyTo: "src/gui/**/*.cpp,include/gui/**/*.h,include/gui/**/*.hpp,assets/qss/**/*.qss,tests/QssValidator.cpp"
---

# Qt UI Workflow

- Architecture references: `.github/knowledge/gui-architecture.md` (shell/navigation), `.github/knowledge/youtube-streaming-architecture.md` (YouTube), `.github/knowledge/qt-webengine-streaming-integration.md` (streaming services), `.github/knowledge/airplay-nas-architecture.md` (AirPlay/NAS). Check the relevant doc before researching a UI subsystem.
- Keep widget object names, dynamic properties, and matching QSS selectors synchronized.
- If you edit `assets/qss/*.qss`, validate with a successful build because `QssValidator` runs during the build.
- Keep YouTube-specific styling in `assets/qss/youtube.qss` and shared TV-shell styling in `assets/qss/tv.qss`.
- Visual verification from screenshots is valid only after navigating the app to the exact target state. If the screenshot does not show the expected view, do not judge it — re-navigate and recapture. Once at the correct state, make a definitive pass/fail judgment.

## Design System Compliance

Read `.github/knowledge/design-system.md` before writing any UI code. Rules are enforced by `.github/instructions/design-system.instructions.md`.

- Use font sizes from the typography scale. In C++ paint code, prefer proportional sizing (`r.height() * factor`) for adaptive layouts; use scale values for fixed-size UI elements.
- Use only palette colors, spacing scale values, and defined border radii.
- Do not duplicate design system rules here — cross-reference only.
