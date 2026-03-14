---
description: "Qt widget, QSS, and rendered-output workflow for AIO Server UI work."
applyTo: "src/gui/**/*.cpp,include/gui/**/*.h,include/gui/**/*.hpp,assets/qss/**/*.qss,tests/QssValidator.cpp"
---

# Qt UI Workflow

- Keep widget object names, dynamic properties, and matching QSS selectors synchronized.
- If you edit `assets/qss/*.qss`, validate with a successful build because `QssValidator` runs during the build.
- Keep YouTube-specific styling in `assets/qss/youtube.qss` and shared TV-shell styling in `assets/qss/tv.qss`.
- Visual verification from screenshots is valid only after navigating the app to the exact target state. If the screenshot does not show the expected view, do not judge it — re-navigate and recapture. Once at the correct state, make a definitive pass/fail judgment.
