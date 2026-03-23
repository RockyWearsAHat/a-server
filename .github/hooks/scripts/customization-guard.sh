#!/bin/bash
set -euo pipefail

INPUT_FILE="$(mktemp)"
cat > "$INPUT_FILE"

python3 - "$INPUT_FILE" <<'PY'
import json
import pathlib
import sys
import tempfile


MUTATING_TOOLS = {
    "editFiles",
    "createFile",
    "createDirectory",
    "editNotebook",
    "runInTerminal",
    "createAndRunTask",
}

PROTECTED_MARKERS = (
    ".github/copilot-instructions.md",
    ".github/README.md",
    ".github/agents/",
    ".github/instructions/",
    ".github/prompts/",
    ".github/skills/",
    ".github/hooks/",
    "/AGENTS.md",
    "\\AGENTS.md",
    "/CLAUDE.md",
    "\\CLAUDE.md",
)

ALWAYS_ALLOW_MARKERS = (
    ".github/knowledge/",
)


def normalize(value: str) -> str:
    return value.replace("\\", "/").strip().lower()


def iter_strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for nested in value.values():
            yield from iter_strings(nested)
    elif isinstance(value, list):
        for nested in value:
            yield from iter_strings(nested)


def targets_protected_surface(tool_input) -> bool:
    for raw in iter_strings(tool_input):
        candidate = normalize(raw)
        if any(marker in candidate for marker in ALWAYS_ALLOW_MARKERS):
            continue
        if any(marker.lower() in candidate for marker in PROTECTED_MARKERS):
            return True
    return False


data = json.loads(pathlib.Path(sys.argv[1]).read_text() or "{}")
tool_name = data.get("tool_name")
if tool_name not in MUTATING_TOOLS:
    print("{}")
    raise SystemExit(0)

tool_input = data.get("tool_input") or {}
if not targets_protected_surface(tool_input):
    print("{}")
    raise SystemExit(0)

session_id = data.get("sessionId")
state_dir = pathlib.Path(tempfile.gettempdir()) / "aio-copilot-hook-state"
state_file = state_dir / f"{session_id}.customization-allowed" if session_id else None
if state_file and state_file.exists():
    print("{}")
    raise SystemExit(0)

response = {
    "hookSpecificOutput": {
        "hookEventName": "PreToolUse",
        "permissionDecision": "deny",
        "permissionDecisionReason": "Editing Copilot customization files is blocked unless the user explicitly asked to modify the workspace customization layer.",
        "additionalContext": "The .github Copilot control surface is protected because instructions, agents, skills, prompts, and hooks define the agent's own operating rules. Treat these files as configuration work, not routine task output.",
    }
}
print(json.dumps(response))
PY

rm -f "$INPUT_FILE"