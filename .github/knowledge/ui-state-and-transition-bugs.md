# UI State And Transition Bugs

> Last researched: 2026-03-23

## Scope

Concrete practices that prevent hidden-state, invalid-transition, and weird-sequence bugs in large UI, game, and native codebases.

## Key facts

- The most common failure shape is duplicated state: the same fact exists in multiple places and drifts.
- Boolean soups create impossible combinations and accidental transitions.
- Sequence-sensitive bugs usually come from event ordering, async completions, or partial exits from a mode.
- One-off events are fragile when producer and consumer lifetimes differ.
- Flat state machines get rigid when they mix unrelated concerns into one enum.

## Practical rules

1. Give each important fact one owner.
2. Expose an immutable snapshot for render-relevant state.
3. Store the minimum state; derive the rest.
4. Model meaningful events, not scattered setters.
5. Guard transitions against current state.
6. Keep business state separate from short-lived UI behavior.
7. Let one external event settle to a stable snapshot before the next one runs.
8. Cancel or ignore stale async work when leaving the state that owns it.
9. Use hierarchy, history, or parallel regions before a flat machine turns brittle.

## When explicit state machines help

- Distinct modes have different entry/exit/update behavior.
- Some transitions are illegal and should be blocked by construction.
- Pause, resume, retry, or interrupt flows matter.
- Async work belongs to specific modes.

## When they are too much

- Very small, local UI behaviors.
- Data-heavy screens with little real mode logic.
- Cases where one flat machine is being forced to model several independent dimensions.

## Sources

- Tier 1: W3C SCXML.
- Tier 1: Qt state machine overview and C++ guide.
- Tier 1: Android UI layer, state holders, and UI events guidance.
- Tier 1: Apple GameplayKit state machine guide.
- Tier 2: Redux style guide for reducer-as-state-machine patterns.
