---
name: research-methodology
description: "Parallel research coordination, source priority, synthesis, and knowledge caching for the R&D Lead agent."
user-invocable: false
---

# Research Methodology

## How to Lead Research

When a research question arrives:

1. **Decompose** into every sub-question the implementation team will need answered — architecture, protocol details, edge cases, error handling, data formats, auth flows.
2. **Dispatch in parallel** — launch many Explore agents and your own research tools simultaneously. Don't serialize independent work.
3. **Synthesize** — combine findings into a unified, complete picture.
4. **Gap-check** — "Could an engineer implement this end-to-end from my deliverable alone?" If no, research the gaps.
5. **Deliver** — comprehensive design document with every question answered and every source cited.

## Parallel Dispatch at Scale

Use as many Explore subagents as the problem requires. No artificial limit.

**Example pattern** (AirPlay screen mirroring):

Batch 1 (all at once):
- Explore → read current codebase implementation
- Explore → read cached knowledge from `.github/knowledge/`
- Explore → read `reference-sources.md` for known doc URLs
- Explore → search codebase for related usage patterns
- Yourself → `search_web` for official developer documentation
- Yourself → `search_web` for protocol specifications

Batch 2 (after batch 1, targeting gaps):
- Explore → read specific source files found in batch 1
- Explore → cross-reference codebase against spec requirements
- Yourself → `fetch_pages` on official doc URLs found in batch 1

Batch 3 (if needed):
- Explore → verify implementation details against spec findings
- Explore → write knowledge doc to `.github/knowledge/`

## Research Workflow

### Step 1: Parallel Initial Sweep

Launch ALL simultaneously:

- **Reference sources**: Read `.github/knowledge/reference-sources.md` for known-good URLs. Fetch those pages.
- **Knowledge cache**: Dispatch Explore to read existing research in `.github/knowledge/`.
- **Codebase understanding**: Dispatch multiple Explore agents to read the current implementation.
- **Official doc search**: Run `search_web` for official developer documentation on every major sub-topic.

### Step 2: Deep-Dive on Gaps

1. List every remaining unknown.
2. For each gap: official docs available → fetch them. Need codebase cross-refs → dispatch Explore. Need more docs → search then fetch. Multiple gaps → research ALL in parallel.

**Search-before-fetch rule**: Call `search_web` or `google_search` at least once BEFORE calling `fetch_pages` on any URL not from `reference-sources.md`.

### Step 3: Synthesize

Combine all research threads. Gap-check: "Could someone implement this from only this document?" If no, go to Step 2.

### Step 4: Write Back to Knowledge Cache

Use MCP write tools to create or refresh notes in `.github/knowledge/`:

- `write_knowledge_note` — create new or overwrite stale
- `update_knowledge_note` — replace a section by heading
- `append_to_knowledge_note` — add to existing note

This is mandatory. Include source tiers on every claim.

## Context Management

- **Write findings to knowledge docs early**: After each batch, write to `.github/knowledge/` BEFORE the next batch. Moves information out of conversation context.
- **Synthesize from docs, not memory**: Read back from knowledge docs for the final deliverable.
- **One checkpoint file**: `/memories/session/rd-checkpoint.md`, overwritten each step. Content: questions answered, remaining, doc locations.

## What to Avoid

- Blog posts, Medium articles, or tutorials as primary evidence — they may be outdated or incorrect.
- Stack Overflow answers without verification — may be years old with deprecated APIs.
- Stopping at first relevant search result — official docs may be on page 2.
- Community sources when official sources exist for the same information.
- Proposing hardware verification — find the official spec instead.
- Other emulator implementations as authoritative sources — they may be wrong.

## Deliverable Format

1. **Architecture overview** — How the system/protocol/feature works
2. **Detailed design** — Components, data flows, state machines, protocol exchanges
3. **Current codebase state** — What exists, works, is broken, is missing (from actual code, not comments)
4. **Gap analysis** — What needs to change from current to correct
5. **Spec compliance notes** — Deviations from official spec, with citations
6. **Edge cases and error handling** — From the spec
7. **Source citations** — Every claim tied to a source with tier level
8. **Confidence assessment** — Certain (tier 1-2) vs. inferred or uncertain
