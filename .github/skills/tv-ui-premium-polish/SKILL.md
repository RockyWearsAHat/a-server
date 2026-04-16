---
name: tv-ui-premium-polish
description: "Premium TV UI polish principles derived from AAA streaming app analysis (Disney+, Netflix). Apply when any AIO Server surface needs to feel like a $200M streaming product — content-first, editorial, borderless, and inviting."
user-invocable: false
---

# TV UI Premium Polish

Content-first, chrome-last. The user should feel like they are browsing a
curated magazine, not operating software. Every rule below is grounded in
what Netflix, Disney+, Apple TV, or Xbox actually ship — see
`.github/knowledge/tv-ui-design-patterns.md` for full product analysis.

## The 10 Rules

1. **Borderless resting state.** Unfocused cards: `border: none` or `border: 1px solid transparent`. Focus adds the ring. _(tvOS default; Netflix 2025 zero-border unfocused items.)_
2. **Editorial section headers.** Labels feel curated ("Watch Now", "Discover Something New"), not technical ("STREAMING", "APPS & MEDIA"). _(Netflix: algorithm-derived labels; Disney+: "New to Disney+".)_
3. **Artwork dominance.** Artwork fills >70% of card area. Gradient scrims for text over images, not opaque containers. _(Netflix 2025 renders titles into poster images — no text labels at all.)_
4. **Focus = scale + shadow + ring.** Scale 5-8% + drop shadow + subtle ring. _(tvOS: parallax + scale + shadow, automatic. Xbox XAG 113: combine border + fill + font change.)_
5. **Generous row spacing.** Inter-row: 48px min. Card gap: 12-16px. Section header → row: 8-12px.
6. **Icon-first navigation.** Labels appear on focus only. Nav < 10% screen width.
7. **Color restraint.** Accent colors only for focus, progress, CTAs. _(Apple HIG: "favor muted colors." PS5 was criticized for 3 background tones across surfaces — inconsistency = cheap.)_
8. **3 typography levels max per viewport.** Section header, card title, metadata.
9. **Everything animates.** 200-300ms, OutCubic/OutQuad. `QPropertyAnimation` in C++. _(tvOS shortens durations during fast swipes. Netflix transitions ~200ms.)_
10. **Unified experience across all surfaces.** Same card format, same interaction, same timing for games, movies, streaming, emulators. _(Netflix 2025's biggest win: identical carousel for all content types. PS5's biggest failure: every surface looks different.)_ This is the #1 rule for AIO Server.

## QSS Anti-Patterns

| Anti-Pattern                                                  | Fix                                                       |
| ------------------------------------------------------------- | --------------------------------------------------------- |
| `border: 1px solid rgba(255,255,255,0.06)` on unfocused cards | `border: 1px solid transparent`                           |
| Visible container chrome around scroll areas                  | `border: none; background: transparent`                   |
| `letter-spacing: 2.0px` on all headers                        | `1.2-1.6px` max; heavy tracking only for eyebrow labels   |
| ALL CAPS everywhere                                           | Title Case for editorial; ALL CAPS only for true eyebrows |
| `background-color: rgba(18,18,18,0.80)` on content containers | `transparent` or `rgba(18,18,18,0.40)`                    |
| Hard-coded hover colors without transition                    | Pair with C++ property animations                         |

## Quality Gate Checklist

- [ ] Unfocused cards borderless
- [ ] Focus uses scale + shadow + ring
- [ ] Section headers editorial, not technical
- [ ] Artwork >70% of card area
- [ ] Max 3 typography levels
- [ ] Inter-row spacing >= 48px
- [ ] Neutral dark background — content provides color
- [ ] State transitions animate (200-300ms, ease-out)
- [ ] No unnecessary chrome visible by default
- [ ] CTAs are high-contrast pills
