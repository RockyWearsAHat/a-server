# Path-Dependent Bug Observability

> Last researched: 2026-03-23

## Scope

Instrumentation patterns that make weird execution paths reconstructable after the fact.

## Key facts

- The goal is not more log volume. The goal is preserved causality and enough context to explain one odd execution path.
- Stable correlation across boundaries matters more than local message spam.
- Structured events at branch points, retries, fallbacks, enqueue/dequeue/process boundaries, and state transitions expose the path that actually ran.
- Release version, canary/control population, and feature decision state are part of the debug context for production-only bugs.

## Capture this

1. Trace or correlation IDs across services, queues, and async work.
2. Structured events with typed attributes.
3. Branch-point and transition markers.
4. Dependency targets, timing, and success/failure.
5. Exception details plus request-scoped context.
6. Small stable request attributes: route, operation, cohort, version, feature variation.
7. Host/runtime health signals for load-sensitive failures.

## Avoid this

- Unstructured text logs as the primary production debugging tool.
- Logging everything and hiding the signal in noise.
- Omitting rollout context from telemetry.
- Breaking trace continuity at queue or async boundaries.
- Comparing canary to yesterday instead of to a live control.

## Practical rollout implications

- Segment failures by canary vs control, version, and feature cohort.
- Ensure rare failing traces are retained by error-aware sampling.
- For background work, propagate correlation through the message payload or metadata.

## Sources

- Tier 1: OpenTelemetry traces, logs, data model, context propagation, baggage, and sampling docs.
- Tier 1: W3C Trace Context.
- Tier 1: Google SRE workbook on canarying releases.
- Tier 1: Azure Monitor / Application Insights distributed tracing guidance.
- Tier 2: LaunchDarkly testing guidance for feature-flagged code.
