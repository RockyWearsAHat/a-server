# Release Safety And Feature Decisions

> Last researched: 2026-03-23

## Scope

Release and feature-decision patterns that reduce blast radius for risky changes and weird production-only behavior.

## Key facts

- Default-off, short-lived release flags are the safest pattern for risky in-progress behavior.
- Feature decisions should be centralized near the boundary of the request or workflow, not scattered through deep code paths.
- Local evaluation with cached configuration and safe defaults is safer than remote per-request dependency on the flag provider.
- Canary rollout is safer when compared against a live control, not before/after aggregates.
- Cleanup is part of release safety, not optional housekeeping.

## Good patterns

1. Default-off temporary release flags.
2. Centralized decision functions or strategy injection.
3. Sticky targeting and stable cohorts.
4. Local cached evaluation with graceful fallback.
5. Small explicit canary phases with verify gates.
6. Fast exposure rollback separate from code rollback.
7. Scheduled cleanup and ownership for long-lived operational flags.

## Anti-patterns

- Raw flag checks everywhere.
- Flags used as permanent configuration or secret storage.
- Remote real-time evaluation on every request.
- No live control during canary.
- Leaving temporary flags in place after full rollout.
- Treating rollback as code revert only.

## Testing implications

- Test both enabled and disabled paths while the flag exists.
- Test degraded mode: stale cache, provider outage, fallback path, kill switch path.
- Validate cohort consistency and exclusion rules.
- Treat cleanup as testable work.

## Sources

- Tier 1: Google SRE workbook on canarying releases.
- Tier 1: Google Cloud Deploy canary strategy docs.
- Tier 1: GitLab feature flag development docs.
- Tier 1: Microsoft .NET feature management docs.
- Tier 2: LaunchDarkly flag debt guidance.
- Tier 2: Unleash feature flag best practices.
- Tier 2: OpenFeature concepts and evaluation model.
