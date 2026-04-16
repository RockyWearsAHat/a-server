# Focus & Behavior Checklist (TV Platform)

## Focus Navigation

- [ ] Every interactive element has a visible focus state (border, glow, scale, or outline change)
- [ ] Focus is obvious from 10 feet — not just a subtle color shift
- [ ] Focus indicator uses accent token (`#64b5f6`) or appropriate semantic color
- [ ] No focus traps — every focused element can be navigated away from via D-pad
- [ ] No dead ends — D-pad always moves focus to a logical neighbor

## D-pad / Remote Movement

- [ ] Horizontal input moves within a row
- [ ] Vertical input moves between rows/sections
- [ ] Movement is predictable — no focus jumps to unexpected elements
- [ ] Wrap behavior is consistent (rows wrap or stop, not mixed)
- [ ] First element of each row/grid is reachable from the navigation rail

## Content Behavior

- [ ] Thumbnails and media content dominate — chrome recedes
- [ ] Consistent aspect ratios across all cards in a row
- [ ] Long text truncates with ellipsis, never overflows container
- [ ] Loading states are present (not blank space) for async content

## Sub-App Integration

- [ ] Sub-app adopts platform font (Noto Sans) and color palette
- [ ] Back/home navigation returns to the correct parent screen
- [ ] Platform overlays (settings, notifications) render above sub-app content
