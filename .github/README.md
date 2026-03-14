# GitHub Copilot Workspace Customization

Repository-local Copilot customization for AIO Server. The setup is workflow-first: use the shortest correct path, delegate only for specialization, and avoid unnecessary coordinator hops.

## Layout

```
.github/
├── agents/                  # Thin coordinators and focused workers
├── instructions/            # Path-specific rules (loaded only when matching files are open)
├── skills/                  # On-demand procedures (loaded only when agents need them)
├── prompts/                 # Human-invoked entry points
├── README.md
├── SYSTEM.md
└── copilot-instructions.md  # Always-on runtime routing and repo facts
```

## Routing Model

- Default to one worker when one worker can finish the task safely.
- Use Project Lead only for multi-step work that benefits from routing.
- Use R&D Lead only for real design or external-doc questions.
- Use Senior Engineer only for unusually broad parallel delivery.
- Use Quality Auditor only when the workflow is churning, repeating, or drifting.

### Agents

| Agent           | File                       | Role                                                                            | Visible       |
| --------------- | -------------------------- | ------------------------------------------------------------------------------- | ------------- |
| Project Lead    | `project-lead.agent.md`    | Thin coordinator for multi-step tasks. Routes directly to the right specialist. | Yes           |
| R&D Lead        | `rd-lead.agent.md`         | Optional research specialist for design and documentation gaps.                 | No (subagent) |
| Senior Engineer | `senior-engineer.agent.md` | Optional coordinator for rare large parallel delivery tasks.                    | No (subagent) |
| Code Engineer   | `code-engineer.agent.md`   | Primary implementation worker for code changes and nearby tests.                | No (subagent) |
| Test Engineer   | `test-engineer.agent.md`   | Focused verifier for targeted tests and regression coverage.                    | No (subagent) |
| Visual Engineer | `visual-engineer.agent.md` | Visual verification specialist for rendered-output evidence.                    | No (subagent) |
| Quality Auditor | `quality-auditor.agent.md` | Occasional workflow monitor for churn, drift, and repeated failure.             | No (subagent) |

### Design principles

- **Workflow over identity.** Route based on the work needed, not on maintaining a hierarchy.
- **Direct path first.** Avoid serial delegation when one scoped worker can finish the task.
- **Compact handoffs.** Pass goal, files, constraints, and verification target only.
- **Parallel reads once.** Gather read-only context once, in parallel when possible, then act.
- **Cheap by default.** Use the workspace default or low-cost models unless stronger reasoning is actually needed.

## Prompts

- `visual-dev-tester.prompt.md` — direct entry to visual verification via Visual Engineer.

## Instructions (path-matched, auto-loaded)

- `cmake-vcpkg.instructions.md` — build system facts
- `emulator-core.instructions.md` — emulator accuracy rules
- `qt-ui.instructions.md` — Qt/QSS synchronization rules
- `memory.instructions.md` — `.github/` maintenance rules
- `visual-dev-loop.instructions.md` — visual loop essential facts

## Skills (on-demand, loaded by agents)

- `workflow-orchestration/SKILL.md` — shared triage, delegation, handoff, and stop-condition rules
- `visual-development-loop/SKILL.md` — full capture/verify procedure
- `visual-development-tester/SKILL.md` — lightweight dispatch layer for verification vs planning

## Visual Loop Tooling

- `tools/aioserver-vision-tool` — workspace-local VS Code extension: `aioserver-analyze-images` (unified, 1–10 images), `@aioserver-vision` chat participant.
- Install: `make install-vision-tool` — Refresh: `make reinstall-vision-tool`
- `python3 scripts/visual_dev_loop.py` — host-driven boot, capture, session automation.
