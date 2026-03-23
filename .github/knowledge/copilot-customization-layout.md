# Copilot Customization Layout

## Scope

Durable facts about the AIO Server repo-local Copilot customization layout, routing, and cache behavior.

## Key Facts

- Workspace `.github/` stays limited to repo-specific Copilot files: runtime instructions, README, path-matched instructions, agents, prompts, skills, and durable knowledge notes.
- `.github/copilot-instructions.md` is intentionally short and repo-wide; language and subsystem detail belongs in scoped instruction files.
- `Project Lead` is the user-facing coordinator for multi-step work.
- `R&D Lead` handles external documentation research.
- `Explore` is the durable repo knowledge owner and maintains `.github/knowledge/`.
- `Senior Engineer` manages development; `Code Engineer` handles scoped implementation.
- `Test Engineer`, `Visual Engineer`, and `Quality Auditor` own QA.
- Native implementation methodology lives in `.github/skills/native-cpp-workflow/SKILL.md` and is loaded for C++/Qt/CMake tasks.
- Native code conventions are layered through `.github/instructions/cpp-core.instructions.md`, `.github/instructions/qt-cpp.instructions.md`, and `.github/instructions/cmake-vcpkg.instructions.md`.
- `.github/` control-surface files (`copilot-instructions.md`, agents, instructions, prompts, skills, hooks, `.github/README.md`) are protected by hooks and should only be edited when the user explicitly asked for customization work.
- Mutable product or implementation status should live in `.github/knowledge/` or other human-facing docs, not in `copilot-instructions.md`.
- Worker agents that must be callable as subagents use `user-invocable: false` and must not use `disable-model-invocation: true`.
- The local `aioserver-research` Node server can search `.github/knowledge/`, but it cannot directly read `/memories/repo` as a normal filesystem path.
- `aioserver-research/*` is the research tool namespace. Default external-doc flow is `search_web`, then `fetch_pages`. `google_search` is optional legacy support.
- Durable internal retrieval flow is `search_knowledge_cache`, then `read_knowledge_note`.

## Retrieval Hints

- Search this note when questions involve agent routing, cache ownership, or research-tool expectations.
- Refresh this note when agent roles, tool namespaces, or cache paths change.

## Verification Basis

- `.github/copilot-instructions.md`
- `.github/README.md`
- `.github/agents/*.agent.md`
- `tools/aioserver-research-tool/`
