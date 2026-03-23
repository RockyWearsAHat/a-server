# Knowledge Cache

This directory is the repo-local durable knowledge cache for Copilot workflows in AIO Server.

Ownership:

- `Explore` owns creating and refreshing notes here.
- `R&D Lead` researches external sources, then hands reusable findings to `Explore` so they become shared cache.
- `Project Lead` and `Senior Engineer` consume this cache and route missing knowledge to `Explore` instead of doing broad discovery themselves.

What belongs here:

- Reusable subsystem notes that future agents are likely to need again.
- Verified workflow facts, routing rules, and implementation constraints that are too specific to keep re-discovering.
- Concise summaries of external research after the evidence has been checked.
- Source tier annotations (official docs, spec, source code, community) so readers know how much to trust each claim.

What does not belong here:

- One-off chat summaries.
- Speculation, guesses, or low-confidence findings.
- Large copied source dumps.
- Claims derived from code comments rather than code behavior. A comment saying "handles X" is not evidence that X works — only the code body is evidence.

Preferred note shape:

- Title
- Scope
- Key facts (with source tier when based on external research)
- Sources or verification basis
- Last verified date when it matters

Confidence rules:

- Notes are **helpful starting points**, not gospel truth. Previous research may have been incomplete or based on non-authoritative sources.
- Notes citing official developer documentation (tier 1-2) are higher confidence than notes citing only community sources (tier 5).
- When a note's "Last Verified" date is old relative to the technology's pace of change, spot-check key claims before relying on them.
- When updating a note, preserve what's still valid and correct what's wrong. Don't discard everything — build incrementally.
- The goal is progressive accuracy: each research pass makes the cache better and reduces future research cost.

## Code-over-comments rule (MANDATORY)

When documenting what a subsystem does or whether a feature works:

- **Read the code body**, not the comments above it. A function with a descriptive comment but an empty body does NOT work.
- **Trace the full pipeline**. A signal declared in a header, connected in a manager, but never emitted is NOT a working feature.
- **Never describe stubs, placeholders, or scaffolding as functional**. If the code path ends at a TODO, an empty handler, or a hardcoded stub response, document it as "not yet implemented" — not "working".
- **Comments are documentation aids, not truth**. Good comments help explain working code. Bad comments make false claims about non-working code. When comments and code disagree, the code is the truth and the comment is a bug.

Use the research MCP tools to query this cache:

- `search_knowledge_cache`
- `read_knowledge_note`
