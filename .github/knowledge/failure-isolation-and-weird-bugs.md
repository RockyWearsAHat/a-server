# Failure Isolation And Weird Bugs

> Last researched: 2026-03-23

## Why this note exists

We do not want broad, impressive-sounding process language. We want specific ideas that help prevent the class of bugs people describe as:

- weird coupling
- only-broken-after-17-steps bugs
- impossible-state bugs
- one feature accidentally changing another
- "it only breaks when timing changes" bugs

The useful theme across good sources is simple: those bugs usually come from hidden shared state, unclear ownership, delayed work with missing context, or code paths that are impossible to see from the outside.

## The plain-English rules

1. Keep each subsystem responsible for one kind of work.
A class or module that mixes input, rendering, persistence, networking, and state transitions becomes a bug magnet because every change requires understanding too many domains at once.

2. Make state explicit.
If behavior depends on several booleans, timestamps, and "current mode" variables scattered around, the real state machine is already there, just hidden and fragile.

3. Test transitions, not just steady states.
A lot of nasty bugs are not "state A is wrong". They are "A -> B -> C with one interrupt in the middle".

4. When work is deferred, capture the needed facts at the moment the event happens.
Queued work often breaks because the consumer later reads current world state, not the original triggering context.

5. Instrument requests and flows so the software can explain itself.
If a bug requires guessing which path executed, the system is too opaque.

6. Prefer local boundaries over giant global buses.
A central bus can decouple callers, but it also creates invisible dependencies, feedback loops, and delayed-state bugs if used everywhere.

7. Use strong constraints where they help reasoning, but do not bake every business rule into hard-to-reverse infrastructure constraints.
Some rules belong in code and tests instead of schemas or protocols, because production systems eventually need exceptions, migrations, and recovery paths.

8. Roll risky behavior out gradually.
Some bugs only appear under real traffic, real timing, or real user sequences. Canary rollout is how you find them without burning everyone.

## Specific failure patterns to watch for

### 1. Boolean soup

Signs:
- multiple flags such as `isLoading`, `isOpen`, `isAnimating`, `hasFocus`, `pendingClose`
- code checks combinations of flags in many places
- adding one new mode breaks two existing ones

What to do:
- replace flag combinations with an explicit state enum or state machine when only one mode should be active at a time
- keep state-specific data with the state that owns it
- define valid transitions, entry actions, and exit actions

Why:
- Robert Nystrom's state-machine examples show how a few booleans quickly create invalid combinations and accidental behavior leaks.

## 2. Hidden cross-domain classes

Signs:
- one class touches rendering, input, physics, audio, storage, or networking together
- only one or two people feel safe editing it
- trivial changes have far-reaching side effects

What to do:
- split by domain boundaries
- keep a thin coordinator/container if needed
- let components communicate through narrow shared state, explicit interfaces, or carefully-scoped messages

Why:
- Cross-domain classes create the "hairball" effect: every edit requires full-system knowledge.

## 3. Deferred work with missing context

Signs:
- an event is queued, but the handler later looks up live objects to reconstruct what happened
- bugs depend on timing, frame order, or whether another system already mutated the world

What to do:
- snapshot the data needed by the future consumer when the event is emitted
- decide whether the queue is a request queue, broadcast event queue, or work queue
- avoid pushing everything through a single global queue by default

Why:
- Event queues solve timing and thread problems, but they also let "the world change under you" unless the original context travels with the message.

## 4. Invisible production behavior

Signs:
- reproducing requires adding ad-hoc logs after the fact
- the team cannot answer "which path ran for this user/request?"
- failures are explained with guesswork instead of evidence

What to do:
- emit structured events with request IDs, ordering, build/version context, feature state, and key domain identifiers
- keep raw events available long enough to ask new questions after the fact
- treat observability as a way to find where to debug, not a replacement for debugging

Why:
- Charity Majors' practical framing is useful: monitoring catches expected failures; observability helps with unknown failures by making code paths visible from the inside out.

## 5. Rules that are too rigid to recover from reality

Signs:
- schema or protocol rules make migrations or emergency recovery hard
- business exceptions force dangerous manual workarounds
- "the model says this cannot happen" but operations still need to handle it

What to do:
- keep core invariants, but decide carefully which rules must be hard constraints and which should be checked in application logic
- allow recovery and admin paths for exceptional situations
- distinguish "domain should usually forbid this" from "system can never represent this"

Why:
- Sean Goedecke's argument is a good counterweight to dogma: constraints improve reasoning, but overly hard constraints can make real production work harder, not safer.

## 6. Feature-flag sprawl and untestable branches

Signs:
- flag checks scattered across the codebase
- every module knows raw flag names
- old and new behavior both exist but nobody can say which combinations are exercised

What to do:
- centralize flag decisions
- inject the decision result or strategy instead of letting deep code ask the flag system directly
- test both current and fallback paths for changed flags
- remove short-lived flags aggressively

Why:
- Pete Hodgson's feature toggle guidance is strong here: toggles buy safety, but only if decision logic is centralized and flag inventory stays low.

## 7. Big bang rollout of risky changes

Signs:
- a risky change goes to everyone at once
- rollback means code revert only
- issues only surface under real load or real user behavior

What to do:
- ship behind a reversible gate when appropriate
- canary to a small cohort first
- monitor both technical and user-facing regression signals
- keep rollback to rerouting or disabling when possible

Why:
- Canary release is a practical way to detect weird interactions under reality while keeping blast radius small.

## What this means for AIO Server

For native UI and emulator work, weird bugs are likely to come from:

- hidden UI state spread across widget fields, timers, focus state, and async callbacks
- object names, dynamic properties, and QSS drifting out of sync
- cross-thread or queued work reading stale state
- one "manager" class quietly accumulating too many responsibilities
- features toggled or half-wired without a clear ownership boundary

When adding guidance, tests, or instrumentation here, prefer language like:

- "State changes must have a single owner"
- "If a queued callback needs old context, copy that context into the payload"
- "When a widget can be in one of several modes, model the mode explicitly"
- "Do not let rendering code infer domain state from incidental UI fields"
- "If two subsystems interact, define the handoff data instead of letting each read the other's internals"

Avoid vague guidance like:

- "keep things decoupled"
- "follow clean architecture"
- "ensure robust state handling"
- "write maintainable code"

## Source-backed takeaways

### Robert Nystrom, Game Programming Patterns

Useful for:
- boolean-soup to state-machine progression
- hidden coupling across game/UI domains
- risks of global event queues
- delayed work requiring captured context

Most useful practical takeaway:
- many bizarre bugs come from implicit ordering and mutable shared state, not from any single bad line.

### Charity Majors

Useful for:
- plain-English explanation of observability
- why unknown failures stay expensive without structured events
- why visibility should start early, not only at scale

Most useful practical takeaway:
- the system needs to tell you which path it took, with enough context to compare failed and successful runs.

### Pete Hodgson / Martin Fowler

Useful for:
- separating decision points from decision logic
- testing both fallback and intended paths
- managing flag carrying cost
- canary rollout for real-world regression detection

Most useful practical takeaway:
- release safety is not just "have a flag". It is centralize decisions, test both paths, and make rollback cheap.

### Sean Goedecke

Useful for:
- resisting absolutist design rules
- keeping room for migrations, exceptions, and operational recovery

Most useful practical takeaway:
- some invalid states should be representable so the system can survive reality.

### Chickensoft / Joanna May

Useful for:
- game-specific layering in plain language
- visual layer vs logic layer vs data layer
- state machines driving visuals
- isolation-oriented testing mindset

Most useful practical takeaway:
- if a unit cannot be tested in isolation, the architecture is probably hiding coupling.

## Guidance for future Copilot customization work

If we encode this into instructions or skills later, prefer narrow, concrete rules:

- tell the agent to look for flag soups and implicit state transitions
- tell the agent to ask where queued work gets its context
- tell the agent to identify classes spanning too many domains
- tell the agent to prefer tests that cover transition sequences and fallback paths
- tell the agent to add structured logging around request/transition identity when debugging path-dependent failures

Do not encode generic slogans unless they come with a concrete detection heuristic.

## Sources

- Game Programming Patterns, Robert Nystrom:
  - https://gameprogrammingpatterns.com/component.html
  - https://gameprogrammingpatterns.com/event-queue.html
  - https://gameprogrammingpatterns.com/state.html
- Charity Majors:
  - https://charity.wtf/2022/08/15/live-your-best-life-with-structured-events/
  - https://charity.wtf/2019/12/17/questionable-advice-how-do-i-get-my-team-into-observability/
- Martin Fowler / Thoughtworks:
  - https://martinfowler.com/articles/feature-toggles.html
  - https://martinfowler.com/bliki/CanaryRelease.html
- Joanna May, Chickensoft:
  - https://chickensoft.games/blog/game-architecture
- Sean Goedecke:
  - https://www.seangoedecke.com/invalid-states/