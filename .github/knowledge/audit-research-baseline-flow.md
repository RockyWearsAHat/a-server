# Audit Research Baseline Flow

> Last researched: 2026-03-23

## Scope

How research-oriented agents should load existing knowledge before external research, and how they should write reusable findings back into cache.

## Key facts

- Baseline-first is the correct order: load reusable local context first, then shared cache/context packs, then do live external research only for gaps, freshness checks, and conflict resolution.
- Cached knowledge is a starting point, not a substitute for current authoritative sources.
- Progressive loading matters. Reusable instructions, skills, and knowledge notes reduce repeated exploration and keep high-value context from being rewritten into every prompt.
- Durable cache writes should store normalized conclusions, not raw browsing transcripts.
- Every reusable cached claim should carry provenance: source URL, source tier, verified date, scope, and whether it is new, revalidated, stale, or superseded.

## Recommended sequence

1. Load repo-local baseline.
   Read relevant instructions, skills, durable knowledge notes, and any handed project context.
2. Load shared audit baseline.
   Read the studybase and community cache snapshot or other maintained reference packs.
3. Identify unknowns.
   Write down what the baseline does not answer or what may be stale.
4. Research only the gaps.
   Prefer official docs and release notes first, then strong product-team guidance, then public examples.
5. Normalize the result.
   Separate current rules, illustrative patterns, rejected stale guidance, and unresolved questions.
6. Write back durable findings.
   Store concise, privacy-safe notes with freshness metadata.

## Failure modes

- Starting with web search and only checking cache later.
- Treating a cached note or one public repository as the final truth.
- Reusing stale guidance without a verified date.
- Copying the same rule into prompts, agents, and instructions instead of referencing the existing source.
- Writing raw notes or transcripts into cache without normalization.
- Publishing local repo details into a shared cache.

## Good wording for agents and skills

- Load the existing baseline for this workflow before any external research.
- Treat cached knowledge as a starting point, not the final authority.
- Use external research only for gaps, freshness checks, and conflict resolution.
- Cache only normalized, source-backed conclusions.
- Mark stale or superseded guidance explicitly.

## Sources

- Tier 1: VS Code customization overview, custom instructions, agent skills, prompt files, and custom agents docs.
- Tier 1: GitHub Copilot docs on repository instructions and Spaces.
- Tier 3: Anthropic skills repository patterns for progressive loading and reusable references.
