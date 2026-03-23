# Agent Knowledge Flow

## Scope

How durable knowledge is created, refreshed, and consumed across the AIO Server Copilot agent stack.

## Key Facts

- `R&D Lead` owns external documentation research. It MUST check `search_knowledge_cache` before any external research.
- `Explore` owns durable repo knowledge capture and refresh in `.github/knowledge/`.
- `Project Lead` should check the knowledge cache as the first step of Phase 1 research, before dispatching Explore or R&D Lead.
- `Senior Engineer` MUST load the relevant architecture doc from `.github/knowledge/` before dispatching any subagent. In debug mode, this is the first step — before exploring code. In standard mode, use Explore to check cached facts before escalating to Project Lead.
- `Senior Engineer` MUST pass cached knowledge forward when dispatching `Code Engineer` and `Test Engineer`: relevant file paths, subsystem architecture, known patterns, and test scoping. Implementation agents should never start cold.
- `Code Engineer` should check `.github/knowledge/source-map.md` if Senior Engineer didn't provide full context. The cache exists to prevent blind exploration.
- `Test Engineer` should check `test-scoping.instructions.md` and the source map for test-to-subsystem mappings. Don't guess test scope — it's documented.
- When `R&D Lead` discovers new facts through external research, it MUST write them to the cache via `write_knowledge_note`, `update_knowledge_note`, or `append_to_knowledge_note`. This is not optional — every significant research result must be cached.
- If durable repo knowledge is missing or stale, any agent with MCP access can fix it directly using the write tools.

## Retrieval Path

There are two ways to read the knowledge cache, depending on the agent:

### MCP tools (for agents with MCP access: R&D Lead, Project Lead, Senior Engineer)

1. `search_knowledge_cache` → locate matching notes by keyword.
2. `read_knowledge_note` → read the cached note content.
3. If the cache answers the question and is recent: **stop**. Use the cached answer.
4. If the cache is partial or stale: use it as a starting point, research only the gap.
5. `search_web` and `fetch_pages` → only when the cache is missing, stale, or needs external verification.
6. After external research: always write back via `write_knowledge_note`, `update_knowledge_note`, or `append_to_knowledge_note`.

### Direct file reads (for implementation agents: Code Engineer, Test Engineer)

CE and TE have restricted tool sets and may not have MCP access. They read knowledge docs as regular files:

1. `read_file` on `.github/knowledge/source-map.md` to orient.
2. `read_file` on the relevant architecture doc (e.g., `.github/knowledge/ps1-emulator-architecture.md`).
3. Senior Engineer should pass the key facts in the dispatch prompt to minimize CE/TE needing to self-serve.

## Writing to the Knowledge Cache

The MCP server provides three write tools for direct cache updates — no Explore delegation needed:

| Tool                       | Use case                                                    |
| -------------------------- | ----------------------------------------------------------- |
| `write_knowledge_note`     | Create a new note or overwrite a stale one                  |
| `update_knowledge_note`    | Replace a specific section (by heading) in an existing note |
| `append_to_knowledge_note` | Append new findings to an existing note                     |

Any agent with MCP access (SE, R&D Lead, PL) can write directly. This is a single tool call (~200 tokens) vs. the old Explore delegation (~6K tokens).

Guidelines:

1. Preserve existing valid content — use `update_knowledge_note` or `append_to_knowledge_note` when the note partially exists.
2. Use `write_knowledge_note` only for new topics or full rewrites of stale notes.
3. Include a brief note about what was learned and from what source tier.

## Confidence and Staleness

- Cached knowledge is a **helpful starting point**, not guaranteed truth. Previous research may have been incomplete, based on non-authoritative sources, or may not capture the full picture.
- Notes that cite official developer documentation (tier 1-2) are higher confidence than notes citing community sources (tier 5).
- When a note's "Last Verified" date is old relative to the technology's pace of change, key claims should be spot-checked against current official docs.
- When updating a note, preserve what's still valid and mark what was corrected — don't discard an entire note just because one fact was wrong.
- The goal is progressive knowledge accumulation: each pass makes the cache more accurate and complete, reducing future research cost.

## Code-over-comments rule

Knowledge docs that describe internal subsystem state MUST be sourced from **code behavior**, not from code **comments**. Comments are written by developers (and agents) and can be wrong, stale, or aspirational. The code body is the only source of truth for what a feature actually does.

- When writing or updating a knowledge doc, verify claims by reading method bodies — not headers, not TODOs, not comments.
- A method that exists but is empty, returns early, or never gets called is NOT evidence that a feature works.
- A signal declared but never emitted, an endpoint that returns a stub response, or a handler wired to nothing are all "not yet implemented" — never "working".
- If an agent reports a feature as working based on comments alone, that is a workflow failure. The reviewing agent (Senior Engineer, Quality Auditor) must catch and correct it.

## Verification Basis

- Repo-local orchestration files under `.github/`
- Research MCP server under `tools/aioserver-research-tool/`
