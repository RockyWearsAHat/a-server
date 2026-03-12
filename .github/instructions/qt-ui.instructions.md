---
description: "Qt widget, QSS, and rendered-output workflow for AIO Server UI work."
applyTo: "src/gui/**/*.cpp,include/gui/**/*.h,include/gui/**/*.hpp,assets/qss/**/*.qss,tests/QssValidator.cpp"
---

# Qt UI Workflow

- Keep widget object names, dynamic properties, and matching QSS selectors synchronized.
- If you edit `assets/qss/*.qss`, validate with a successful build because `QssValidator` runs during the build.
- Keep YouTube-specific styling in `assets/qss/youtube.qss` and shared TV-shell styling in `assets/qss/tv.qss`.
- Do not claim visual verification from screenshots, recordings, or frame dumps. Report automated checks and ask the user to confirm rendered output.
