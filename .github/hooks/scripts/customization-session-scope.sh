#!/bin/bash
set -euo pipefail

INPUT_FILE="$(mktemp)"
cat > "$INPUT_FILE"

python3 - "$INPUT_FILE" <<'PY'
import json
import pathlib
import re
import sys
import tempfile

data = json.loads(pathlib.Path(sys.argv[1]).read_text() or "{}")
session_id = data.get("sessionId")
prompt = (data.get("prompt") or "").lower()

if not session_id:
    print("{}")
    raise SystemExit(0)

state_dir = pathlib.Path(tempfile.gettempdir()) / "aio-copilot-hook-state"
state_dir.mkdir(parents=True, exist_ok=True)
state_file = state_dir / f"{session_id}.customization-allowed"

patterns = [
    r"\bcopilot\b",
    r"\bcustomi[sz]ation\b",
    r"\binstructions?\b",
    r"\bagents?\b",
    r"\bskills?\b",
    r"\bprompts?\b",
    r"\bhooks?\b",
    r"\.github/",
    r"copilot-instructions\.md",
    r"\bagents\.md\b",
    r"\bclaude\.md\b",
]

allowed = any(re.search(pattern, prompt) for pattern in patterns)
if allowed:
    state_file.write_text("allow\n")
elif state_file.exists():
    state_file.unlink()

print("{}")
PY

rm -f "$INPUT_FILE"