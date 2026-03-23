# GitHub Copilot Workspace Customization

Repository-local Copilot customization for AIO Server. Three-phase delivery: **Planning → Development → Testing**.

## Layout

```
.github/
├── agents/                  # Managers, engineers, and QA specialists
├── hooks/                   # Deterministic lifecycle enforcement
│   ├── build-gate.json      # Stop hook — blocks completion if build fails
│   └── scripts/
│       └── build-gate.sh
├── instructions/            # Path-specific rules (loaded only when matching files are open)
├── knowledge/               # Searchable durable repo knowledge maintained by Explore
├── skills/                  # On-demand procedures (loaded only when agents need them)
├── prompts/                 # Human-invoked entry points
├── README.md
└── copilot-instructions.md  # Always-on product overview and core constraints
```

## Agent Hierarchy

```
Main Agent (coordinator — delegates only, context reserved for user iteration)
  ├── [debug-fix] Senior Engineer (direct dispatch — iterative fix loop)
  │     ├── Code Engineer(s) (implementation)
  │     ├── Test Engineer (verification)
  │     └── Visual Engineer (verification)
  └── [implement/research] Project Lead (planner / approver — researches, plans, reviews, no implementation)
        ├── R&D Lead (consulted during planning phase)
        ├── Senior Engineer (development manager — manages Code Engineers, reviews work)
        │     ├── Code Engineer(s) (implementation)
        │     ├── Test Engineer (verification)
        │     └── Visual Engineer (verification)
        └── Quality Auditor (final standards check)
```

## Three-Phase Delivery

1. **Planning** — Project Lead researches (R&D Lead, Explore), produces concrete plan.
2. **Development** — Senior Engineer manages Code Engineer(s), reviews output, confirms build.
3. **Testing & QA** — Senior Engineer runs Test Engineer and Visual Engineer internally. Project Lead delegates to Quality Auditor for final standards check. Failures loop back to Phase 2.

### Agents

| Agent           | File                       | Role                                                          | Phase       |
| --------------- | -------------------------- | ------------------------------------------------------------- | ----------- |
| Project Lead    | `project-lead.agent.md`    | Head planner and approval authority. Owns the full lifecycle. | All         |
| R&D Lead        | `rd-lead.agent.md`         | Research specialist for design questions and external docs.   | Planning    |
| Senior Engineer | `senior-engineer.agent.md` | Development manager. Manages Code Engineers, reviews work.    | Development |
| Code Engineer   | `code-engineer.agent.md`   | Implementation worker. Edits code, runs targeted builds.      | Development |
| Test Engineer   | `test-engineer.agent.md`   | QA specialist. Runs targeted tests, interprets failures.      | Testing     |
| Visual Engineer | `visual-engineer.agent.md` | QA specialist. Screenshots + AAA visual audit.                | Testing     |
| Quality Auditor | `quality-auditor.agent.md` | QA specialist. Code quality and standards compliance.         | Testing     |

### Design principles

- **Parallel dispatch.** When a request spans multiple domains, the main agent dispatches ALL Project Leads simultaneously. Sequential dispatch is a workflow bug.
- **Product knowledge up, implementation detail down.** The main agent knows what features exist and their status. Project Leads discover implementation details through their hierarchy. Code Engineers read source files.
- **Delegate by default.** Main agent coordinates; Project Lead(s) manage. No agent reads files or edits code above its level.
- **Delegation is mandatory.** Project Lead MUST delegate — never edits files, runs builds, or does visual checks itself. If subagent calls fail, stop and report immediately.
- **Tool restrictions enforce roles.** Delegating agents include `agent` in their `tools:` list to enable subagent calls, plus only the tool categories their role requires. This prevents agents from doing work they should delegate.
- **Methodology lives in skills, not agents.** Agent files define identity, tools, and delegation boundaries. Skills contain step-by-step procedures. This prevents agents from self-executing when they should be loading a skill.
- **Hooks enforce, instructions suggest.** The build-gate Stop hook deterministically blocks completion when the build fails. Instructions are probabilistic guidance.
- **Parse the full request.** Before delegating, identify EVERY distinct feature/fix requested — not just the one mentioned most.
- **Three clean phases.** Planning → Development → Testing. No mixing.
- **Compact handoffs.** Goal, domain scope, constraints, expected outcome. No transcript dumps or file paths in briefs.

## Prompts

- `visual-dev-tester.prompt.md` — direct entry to visual verification routed through Project Lead, which then delegates to Visual Engineer when rendered-output judgment is required.

## Hooks (deterministic lifecycle enforcement)

- `build-gate.json` — **Stop hook**: runs `make build` when the agent tries to complete. Blocks if build fails. Prevents agents from declaring "done" with broken code. Includes `stop_hook_active` guard to prevent infinite retry loops.

## Instructions (path-matched, auto-loaded)

- `cmake-vcpkg.instructions.md` — build system facts
- `emulator-core.instructions.md` — emulator accuracy rules
- `qt-ui.instructions.md` — Qt/QSS synchronization rules
- `memory.instructions.md` — `.github/` maintenance rules
- `runtime-debugging.instructions.md` — crash diagnosis, debug.log, signal interpretation
- `visual-dev-loop.instructions.md` — visual loop essential facts
- `visual-audit.instructions.md` — AAA visual design audit standard (scoring, critical failures, TV platform rules)
- `test-scoping.instructions.md` — file-to-test scope map (targeted ctest runs)
- `system.instructions.md` — local development platform and execution environment

## Skills (on-demand, loaded by agents)

- `main-agent-routing/SKILL.md` — request parsing, delegation, brief writing, context management
- `project-lead-workflow/SKILL.md` — planning, research gate, dispatch, and review phases
- `senior-engineer-workflow/SKILL.md` — debug mode, diff review, verification, crash reference
- `research-methodology/SKILL.md` — parallel research coordination, source priority, synthesis
- `workflow-orchestration/SKILL.md` — shared delegation protocol, context budgets, stop conditions
- `visual-development-loop/SKILL.md` — full capture/verify procedure
- `visual-development-tester/SKILL.md` — lightweight dispatch layer for verification vs planning

## Visual Loop Tooling

- `tools/aioserver-vision-tool` — workspace-local VS Code extension exposing the `aioserver-vision/*` MCP tool namespace and `@aioserver-vision` chat participant for image analysis.
- Install: `make install-vision-tool` — Refresh: `make reinstall-vision-tool`
- `python3 scripts/visual_dev_loop.py` — host-driven boot, capture, session automation.

## Research Tooling

- `tools/aioserver-research-tool` — workspace-local MCP server exposing `aioserver-research/*` for web research plus repo-local knowledge-cache search.
- `search_web` is the default no-credential search path. `google_search` is optional legacy support for existing Google Programmable Search Engine setups.
- `search_knowledge_cache` searches `.github/knowledge/` so agents can pull durable repo facts without repeating broad codebase search.
- `read_knowledge_note` reads a specific cached knowledge note by relative path.
- `write_knowledge_note` creates or overwrites a knowledge note directly (no Explore delegation needed).
- `update_knowledge_note` replaces a specific section by heading in an existing note.
- `append_to_knowledge_note` appends content to an existing note.
- `GOOGLE_CUSTOM_SEARCH_API_KEY` and `GOOGLE_CUSTOM_SEARCH_ENGINE_ID` are required only for `google_search`.
- `make research-tool-check` verifies that the server starts and lists its tools.

### Google credential setup

- The search API key comes from Google Cloud Console: `APIs & Services` -> `Credentials` -> `Create credentials` -> `API key`. Put that value in `GOOGLE_CUSTOM_SEARCH_API_KEY`.
- The search engine ID comes from Google Programmable Search Engine at `programmablesearchengine.google.com`. Create or open your search engine and copy the `Search engine ID`. Put that value in `GOOGLE_CUSTOM_SEARCH_ENGINE_ID`.
- You also need the `Custom Search API` enabled for the same Google Cloud project that owns the API key.
- If you want broad web search rather than a single-site engine, configure the Programmable Search Engine to search the entire web and then use `site_filter` per query when you want narrower results.
- Google currently documents the `Custom Search JSON API` as closed to new customers, so for most new setups you should use `search_web` instead of trying to provision `google_search` from scratch.

## Knowledge Maintenance

- Use `R&D Lead` for external documentation, standards, platform behavior, and architecture questions that need evidence outside the repo.
- Use `Explore` to create and refresh durable repo knowledge in `.github/knowledge/` when the current source map or documentation is incomplete.
- When `R&D Lead` discovers reusable findings, it should hand them to `Explore` so the result becomes shared cache instead of one-off chat context.
- `Project Lead` and `Senior Engineer` should consume the cache and route missing knowledge to `Explore`, not do open-ended discovery themselves.
- Prefer updating durable references over repeated broad search. On this project, stale or missing internal documentation is a workflow bug.
