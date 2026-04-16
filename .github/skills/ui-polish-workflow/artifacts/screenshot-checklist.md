# Screenshot Checklist

## Pre-Capture

- [ ] Build is current (`make build` succeeded after last code change)
- [ ] App is running (`boot --no-focus` or already booted)
- [ ] Correct page is visible (`poll-state --endpoint state` confirms target page)
- [ ] Expected focus state set (if testing focus, navigate to target element first)
- [ ] No transient overlays blocking content (dialogs, toasts dismissed)

## Post-Capture

- [ ] Screenshot shows the expected screen (not a transition frame or loading state)
- [ ] Resolution is representative (not a minimized window)
- [ ] If before/after comparison: both screenshots captured at same page and state

## Milestone Cadence

- Capture after every 5–10 code changes, not after every individual tweak.
- Always capture before starting a polish session (baseline) and after completing all punch-list items (result).
- Skip capture for mechanical single-property fixes (a color value, a spacing constant) — capture at the batch boundary instead.
