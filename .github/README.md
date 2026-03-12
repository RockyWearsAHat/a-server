# GitHub Copilot Workspace Customization

This directory contains the repository-local Copilot customization for AIO Server. Keep files here factual, concise, and limited to workflows that actually exist in this repository.

## Current Layout

```
.github/
├── agents/                  # Repo-local custom agents for AIO Server workflows
├── instructions/            # Path-specific instructions for this codebase
├── skills/                  # Repo-local workflow skills loaded by agents
├── prompts/                 # Thin repo-local prompt entry points for human-invoked workflows
├── README.md
└── copilot-instructions.md
```

## Active Customization Surfaces

- `.github/copilot-instructions.md` is the always-on workspace guidance.
- `.github/instructions/*.instructions.md` contains path-specific rules.
- `.github/agents/*.agent.md` contains repo-local custom agents.
- `.github/skills/*/SKILL.md` contains reusable repo-local workflows that agents load when needed.
- `.github/prompts/*.prompt.md` contains thin human-invoked entry points that route to repo-local agents.

## Active Agents

- `expert-cpp-software-engineer.agent.md` for C++, Qt, emulator, and test work.
- `visual-dev-tester.agent.md` for automated evidence capture, `debug.log` inspection, media artifacts, and user-facing verification workflows.
- `visual-dev-loop.agent.md` for host-driven boot, navigation, screenshot capture, multimodal image inspection, and definitive automated screen-state judgments.

## Active Prompts

- `visual-dev-tester.prompt.md` routes capture-oriented verification requests to `Visual Development Tester`.
- `visual-dev-loop.prompt.md` routes definitive screen-state judgment requests to `Visual Development Loop`.

## Visual Loop Runtime Tooling

- `tools/aioserver-vision-tool` is a workspace-local VS Code extension that contributes `aioserver-inspect-screenshot`.
- `tools/aioserver-vision-tool` also contributes the `@aioserver-vision` chat participant for direct screenshot inspection in normal Copilot Chat.
- `aioserver-inspect-screenshot` reads a captured `.png` or `.jpg`, attaches the real image bytes to a vision-capable Copilot model, and returns the model's analysis to the Visual Development Loop agent.
- This extension exists because `.agent.md` and skill files alone cannot force binary screenshots to be attached as image context during execution.
- The active VS Code window must have that extension loaded before a custom agent can invoke the tool without unknown-tool diagnostics.
- Install or refresh the tool from the repository root with `make install-vision-tool` or `make reinstall-vision-tool`.

Use `Visual Development Tester` when the task is to collect artifacts, inspect logs, or sanity-check UI, video, or audio output without claiming direct sight or hearing.

Use `Visual Development Loop` when the build is current and the task needs automated host interaction plus a yes/no judgment about a specific rendered screen state from a captured screenshot or frame.

## Maintenance

- Keep repo-local customization aligned with the files and workflows that are actually checked in.
- Do not add root `AGENTS.md` files or copies of user-profile customization unless the repository workflow explicitly needs them.
- When build, test, logging, agent-routing, or Qt/QSS workflow details change, update this file and `.github/copilot-instructions.md` together.
