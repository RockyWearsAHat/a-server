---
description: "Durable facts for repository-local Copilot customization files."
applyTo: ".github/**"
---

# Repository Memory

- `.github/copilot-instructions.md` is the always-on runtime file. Keep it short, operational, and focused on routing, build/test, and repo facts that help every task.
- `.github/README.md` is durable human-facing documentation. Use it for layout and discoverability, not for long runtime workflow scripts.
- `.github/knowledge/` is the searchable repo-local durable knowledge cache. Keep notes concise, topic-based, and limited to facts that future agents should reuse.
- Do not force README and runtime instructions to duplicate the same detailed process text.
- Keep `.github/` customization factual and limited to workflows and files that actually exist in this repository.
