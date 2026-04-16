# TV UI Design Patterns

## Scope

Design decisions observed in shipping products (Netflix 2025, Apple TV/tvOS,
PS5 dashboard, Xbox Series, Disney+), distilled into rules for AIO Server.
Every claim links to what a real product does and why. Cross-references
`design-system.md` for AIO token values.

## What "Professional" Means (From Products That Ship)

These aren't aspirational. They're the baseline that Netflix, Apple, and
Sony already clear:

1. **Snappy.** Netflix animates focus transitions in ~200ms. Apple's tvOS
   Focus Engine shortens animation duration automatically when the user
   swipes quickly through items. The rule: input → visual feedback within
   one frame (~16ms), full transition completes within 200ms.
2. **Crisp and clean.** PS5 dashboard uses exactly two background tones
   (dark gray home, light gray settings) — and users _complain_ about the
   inconsistency. Netflix 2025 uses one: near-black everywhere. Fewer
   surfaces = cleaner.
3. **Modern.** Every major TV UI (Netflix, Disney+, Apple TV, Xbox) is
   dark-background, content-forward. No product uses bright chrome. Apple
   HIG explicitly says: "favor muted colors, test on actual TVs."
4. **Alive.** tvOS applies parallax tilt + shadow + scale to _every_
   focused card automatically. Netflix 2025's focused carousel item
   expands to fill ~60% of screen width with metadata, description, and
   pills. Static UIs don't ship on any major platform.

## Layout Grid (What Products Actually Use)

| Parameter         | Value   | Source / Why                                |
| ----------------- | ------- | ------------------------------------------- |
| Grid columns      | 12      | Apple tvOS HIG; Netflix uses ~12 equivalent |
| Side margins      | 80px    | tvOS safe area; PS5 uses ~60-80px           |
| Top margin        | 48-60px | Netflix top menu bar height = ~56px         |
| Bottom margin     | 48px    | tvOS overscan safe zone                     |
| Column gutter     | 16px    | Standard across Disney+, Netflix shelf gaps |
| Row gap (shelves) | 48px    | Netflix/Disney+ inter-row; PS5 uses ~40px   |
| Card gap          | 12-16px | Netflix 2025: ~14px between carousel cards  |

### Content Shelves (The Netflix/Disney+ Model)

Shelves are the single most important layout primitive. Netflix 2025
redesigned theirs to be dramatically larger — the focused item now takes up
~60% of visible width with an attached metadata block.

- **Netflix 2025**: max 4 items visible per row. Focused item expands with
  description + "pills" (tags like "The Final Season Begins Now"). The
  sacrifice: you see fewer items at a glance. The gain: every focused item
  is a complete pitch to the user.
- **Disney+**: uses "Inline Hero" — a single collection gets a full-width
  treatment with logo, background, and title tiles. "Poster Row" uses
  large 2:3 portrait tiles to break visual fatigue from landscape rows.
- **Peek items**: show 0.3-0.5 of the next card at the row edge. Both
  Netflix and Disney+ do this. It signals scrollability without a scrollbar.
- **Poster aspect ratios**: 2:3 (portrait/movie — Disney+ Poster Row),
  16:9 (landscape/hero — Netflix default), 1:1 (square/album — Spotify TV).
  Pick one ratio per shelf — mixing kills alignment.
- **Cards per visible row**: Netflix 2025 reduced to 3-4 (focused) vs the
  old 5-7. Disney+ still shows 5-6. More visible items = more browsing
  speed. Fewer = stronger individual pitch. Choose based on content density.

## Typography at 10 Feet

Text must be legible at 2.5-4m viewing distance on a 40-65" screen.

| Minimum body size               | 16px (already in design-system.md)    |
| ------------------------------- | ------------------------------------- |
| Minimum touch target equivalent | 48px logical height                   |
| Line height                     | 1.3-1.5× font size                    |
| Max text width                  | ~60 characters per line               |
| Contrast ratio                  | 4.5:1 minimum (WCAG AA); aim for 7:1+ |

### Scale Discipline

No more than **5-6 type sizes** across the entire app. AIO Server's 7-tier
scale (display through small) is at the upper limit. Avoid inventing new
intermediate sizes.

### No Pure White

`#ffffff` on `#0a0a0a` is harsh on large screens in dark rooms.
AIO Server uses `#f0f0f0` — correct. Never deviate to pure white.

## Color Strategy

- **Dark background, muted palette.** Content artwork provides visual
  richness. The shell provides a calm dark stage.
- **Accent colors for interactive state only**: focus rings, progress bars,
  CTAs. Never as decoration.
- **No bright backgrounds** on content containers. Transparent or near-black
  (`rgba(18,18,18,0.40)` max).

Already codified in `design-system.md`. The key principle here is restraint:
the user's eyes should be drawn to content, not chrome.

## Focus and State Animation

This is what separates shipping TV UIs from prototypes.

### How Real Products Handle Focus

**Apple tvOS** (the gold standard for focus):

- Parallax tilt effect: card art responds to Siri Remote touch surface
  movement with subtle 3D rotation. Layered images (LSR format, 2-5 layers)
  create real depth perception.
- Scale up on focus (system default).
- Drop shadow deepens on focus.
- All three effects are _automatic_ for standard controls. Custom controls
  must opt in via `adjustsImageWhenAncestorIsFocused`.
- The Focus Engine _shortens animation duration_ when the user swipes
  quickly, so fast navigation never feels laggy.

**Netflix 2025**:

- Focused carousel item _expands dramatically_ — attached metadata block
  appears with description, action pills, and full artwork.
- Smooth OutCubic-style easing.
- Unfocused items shrink back; the transition is fluid, not stepped.

**PS5 dashboard**:

- Game artwork fills the entire background when selected on the home row.
- Focus on a game = the entire screen transforms to that game's branding.
- Settings/submenus use a more traditional highlight bar.

**Xbox Series**:

- Focus uses a bright white border ring against dark tiles.
- The Outer Worlds example (XAG 113): outline + block fill + font weight
  change + font color change — combining _multiple_ focus indicators.
- Fallout 76: yellow high-contrast backdrop + animated character icon
  beside the focused item.

**Xbox Accessibility Guideline 113** (Microsoft's official standard):

- Focus indicator must be visible on _all_ UI backgrounds.
- Must use _multiple methods combined_: border + fill + font change.
- Focus must never move to invisible/offscreen elements.
- When a dialog opens, focus transfers to its first control immediately.

### Focus technique matrix (AIO Server implementation)

| Technique          | QSS-capable? | C++ needed? | Who does it                |
| ------------------ | ------------ | ----------- | -------------------------- |
| Border ring        | Yes          | No          | Xbox, PS5 settings         |
| Background shift   | Yes          | No          | Xbox, Fallout 76           |
| Scale (5-8%)       | No           | Yes         | tvOS (auto), Disney+       |
| Drop shadow        | No           | Yes         | tvOS (auto), Netflix       |
| Parallax tilt      | No           | Yes         | tvOS only (layered images) |
| Metadata expansion | No           | Yes         | Netflix 2025 carousel      |

**AIO Server approach**: scale(5-8%) + shadow + accent ring. This matches
tvOS behavior without requiring layered image assets. For the Game Store
and Home Screen, consider Netflix-style metadata expansion on focus.

### Animation Timing

| Transition     | Duration  | Easing                   | Notes                  |
| -------------- | --------- | ------------------------ | ---------------------- |
| Focus in       | 200ms     | `QEasingCurve::OutCubic` | Fast response to input |
| Focus out      | 150ms     | `QEasingCurve::OutQuad`  | Slightly faster exit   |
| Page enter     | 250-300ms | `QEasingCurve::OutCubic` | Slide or fade in       |
| Page exit      | 200ms     | `QEasingCurve::OutQuad`  | Faster than enter      |
| Press feedback | 80-120ms  | `QEasingCurve::OutQuad`  | Immediate scale-down   |
| Release        | 150ms     | `QEasingCurve::OutCubic` | Bounce back            |
| Shelf scroll   | 250ms     | `QEasingCurve::OutCubic` | Per-card snap          |
| Fade overlay   | 200ms     | `QEasingCurve::Linear`   | Opacity transitions    |

**Rule**: exit animations are always faster than enter animations.
The user should never wait for an animation to complete before seeing the
next thing.

### Press Feedback

When the user presses confirm on a focused element:

1. Scale down 2-3% (80ms, OutQuad) — the "press dip"
2. On release: scale back up (150ms, OutCubic)
3. Then execute the action

This micro-interaction gives tactile feedback through the remote.

## Navigation Patterns (What Shipped Products Chose)

### Sidebar vs Top Menu: The Netflix 2025 Switch

Netflix just switched _from_ sidebar _to_ top menu in their 2025 redesign.
This is the most significant navigation pattern change in streaming in years.

**Old Netflix (sidebar)**:

- Left-side vertical menu, accessible by navigating left from any row.
- Always reachable, didn't push content down.
- Matched Apple TV's native pattern.

**New Netflix 2025 (top menu)**:

- Horizontal menu bar at the top of the screen.
- Items: Home, Shows, Movies, Games, My Netflix.
- Access: scroll to top OR press the _Back button_ (a shortcut most users
  won't discover without guidance).
- Gives the page a clear beginning and end — no more infinite vertical scroll.
- With larger carousel items, the sidebar would have competed for space.

**Disney+**: uses brand tiles (Disney, Pixar, Marvel, Star Wars, NatGeo)
as a horizontal bar — effectively a top menu.

**PS5**: horizontal game row at top, vertical menus in Control Center.

**AIO Server decision**: sidebar is correct for our use case. We have more
nav categories than Netflix (emulators, store, NAS, streaming, settings,
youtube) and don't have Netflix's problem of carousel items needing 60%
screen width. Sidebar keeps all destinations one D-pad press away.

### D-pad Navigation Rules (from Xbox XAG 113 + tvOS Focus Engine)

- Every focusable element must be reachable via D-pad alone.
- Focus movement must be predictable: left/right within a row, up/down
  between rows.
- **tvOS Focus Guides**: when no element is directly aligned
  horizontally/vertically, an invisible "focus guide" redirects focus to
  the nearest logical target. AIO Server equivalent: NavigationController's
  index tracking must handle non-grid layouts.
- **Focus memory**: when returning to a page, restore the last focused
  element. Both tvOS and PS5 do this.
- **Quick-scroll behavior**: tvOS shortens focus animations when the user
  swipes rapidly. AIO Server should reduce animation duration during fast
  D-pad repeat (UIActionMapper's directional repeat already detects this).
- **Back button = menu access**: Netflix 2025's best hidden feature. AIO
  Server: pressing Back from any page already returns to prior page via
  NavigationAdapter. This is correct.

## Content Cards (Product Comparison)

### Netflix 2025 Cards

- **Unfocused**: small landscape thumbnail, no title text visible,
  artwork-only. Maximum density.
- **Focused**: dramatic expansion. Attached metadata block appears beside
  the card with: show title (rendered into poster image, not text), text
  description, action "pills" ("The Final Season Begins Now"), and
  sometimes a play/resume button.
- **Trade-off**: you see only 3-4 items per row. Netflix has the data to
  know their algorithm's first picks are usually right.

### Disney+ Cards

- **Standard row**: landscape 16:9 tiles, 5-6 visible. Title overlaid on
  artwork via gradient scrim.
- **Poster Row**: large 2:3 portrait tiles. Used to break visual monotony
  and highlight new releases. Feels like a movie theater lobby.
- **Inline Hero**: full-width treatment for a single collection. Logo +
  background + featured title tiles + CTA button.

### tvOS Cards

- **Unfocused**: flat, no shadow, small.
- **Focused**: scale up + parallax tilt (responds to touch surface) +
  deepened drop shadow. Layered images create genuine 3D depth. Labels
  fade in below the card only on focus.
- **Top Shelf**: when an app in the top row is focused, the entire top
  half of the screen becomes a showcase for that app's content.

### PS5 Cards

- **Home row**: game icons in a horizontal strip. Selecting a game fills
  the entire background with game artwork. It's more like "full-screen
  preview" than "card focus."
- **Store/Library**: more traditional card grid with highlight borders.

### AIO Server Card Rules (derived from above)

**Resting State (Unfocused)**:

- No visible border (`border: 1px solid transparent`) — matches tvOS, Netflix
- Artwork fills >70% of card area — matches all products
- Title below artwork, 1-2 lines max — tvOS pattern
- Metadata in `text-3` color (#666666) — subdued like Disney+
- Rounded corners 16px — per design-system.md

**Focused State**:

- Scale up 5-8% — matches tvOS system behavior
- Drop shadow (painted in C++, not QGraphicsEffect) — matches tvOS
- Accent ring (2px #64b5f6 border) — matches Xbox pattern
- Title/metadata brightens to `text` color (#f0f0f0)
- For Game Store: consider Netflix-style metadata expansion

**Pressed State**:

- Scale down 2-3% from focused size (the "press dip") — 80-120ms
- This micro-interaction doesn't exist in Netflix/Disney+ (they use
  immediate page transitions) but is standard in game console UIs (Xbox,
  PS5 button press feedback)

## Performance Constraints

TV UIs must maintain 60fps at all times. Any frame drop is visible.

- **Animate with `QPropertyAnimation`**, not by repainting the whole widget.
- **Don't allocate** during animation callbacks.
- **Pre-render** scaled/blurred artwork variants rather than computing on focus.
- **Limit simultaneous animations**: max 3-4 concurrent property animations.
- **Use `QGraphicsEffect` sparingly** — each one adds a full offscreen
  render pass. For drop shadows on cards, paint them manually in
  `paintEvent()` instead.

## PS5 Design Trade-offs (and What AIO Server Takes Away)

PS5 made specific choices that suit its identity. Some sparked user debate;
all of them are worth understanding.

1. **Multiple background tones**: home is dark gray, settings is cream-ish,
   store is near-black. Sony uses this to give each surface a distinct
   character. Some users find it cohesive; others find it fragmented.
   → AIO Server choice: single background (`#0a0a0a`) for unity across
   many different app surfaces (emulators, store, NAS, streaming).

2. **Deep trophy path**: trophies moved from PS4's quick-menu shortcut to
   a longer profile → trophies → game → card-strip journey. Sony
   prioritized the game-centric home row over quick access to secondary
   features.
   → AIO Server choice: keep every major destination ≤2 interactions from
   home, since we don't have a single dominant content type.

3. **Cards for everything**: PS5 uses horizontal card strips even for
   trophies and activity feeds. This keeps the visual language unified
   but trades scan speed for lists of reference data.
   → AIO Server choice: cards for browsable content (games, movies), lists
   for settings and metadata where scan speed matters.

4. **No folders at launch**: PS5 shipped without game folders, adding them
   later in a system update.
   → AIO Server: Home Screen organize mode is already implemented.

## Netflix 2025 Design Trade-offs (and What AIO Server Takes Away)

Netflix's 2025 redesign makes strong bets. Worth studying the trade-offs:

1. **Same carousel format everywhere**: games, movies, shows — identical
   card format, same expansion behavior, same interaction model. Learning
   the UI once teaches all of it. The trade-off: less surface-specific
   optimization.
2. **Information density on focus**: pills, descriptions, and artwork
   together give the user everything needed to decide without opening a
   detail page. The trade-off: only 3-4 items visible per row.
3. **Back button as menu shortcut**: the top menu is reachable from
   anywhere without scrolling up. Relies on user discovery.
4. **Fewer visible items per row**: Netflix's recommendation engine is
   strong enough that showing fewer items works — the top picks are
   usually relevant. AIO Server's Game Store may benefit from showing
   more items since we don't have that level of algorithmic curation.

## Retrieval Hints

- Load when implementing or reviewing any visible AIO Server surface.
- Cross-reference with `design-system.md` for token values.
- Key terms: 10-foot, TV UI, focus, animation, shelf, card, grid, D-pad,
  scale, shadow, easing, snappy, professional, Netflix, tvOS, PS5, Xbox.

## Source Products Analyzed

| Product       | What we studied                                      | Key insight for AIO Server                      |
| ------------- | ---------------------------------------------------- | ----------------------------------------------- |
| Netflix 2025  | Carousel redesign, top menu switch, pills, density   | Unified experience > feature variety            |
| tvOS/Apple TV | Focus Engine, parallax, layered images, HIG          | System-level focus animation is the gold std    |
| PS5 dashboard | Background variation, deep navigation, card-based UI | Distinct surface identity vs unified background |
| Xbox Series   | XAG 113 focus handling, multi-indicator focus        | Combine border + fill + font for accessibility  |
| Disney+       | Inline Hero, Poster Row, brand screens, motion       | Visual variety within consistent structure      |

## Verification Basis

Synthesized from: Netflix TV UI breakdown (Matthijs Langendijk, Medium
Jul 2025), Apple tvOS HIG Focus and Selection docs + BPXL HIG summary,
Brightec tvOS Focus Engine deep-dive, Disney Streaming Blog "Latest UX
Enhancements" (Nov 2021), Xbox XAG 113 Focus Handling (Microsoft Learn),
Pixelrater PS5 UX analysis, Smashing Magazine "Designing for TV",
and live product observation.
