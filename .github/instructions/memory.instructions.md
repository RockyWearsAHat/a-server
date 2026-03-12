---
description: "Durable facts for repository-local Copilot customization files."
applyTo: ".github/**"
---

# Repository Memory

- Treat `.github/README.md` and `.github/copilot-instructions.md` as the durable snapshot for repository-local Copilot workflow facts.
- When repository-local Copilot build, test, logging, agent-routing, or Qt/QSS guidance changes, update those two files together.
- Keep `.github/` customization factual and limited to workflows and files that actually exist in this repository.
