#!/bin/bash
# Stop hook: verify build passes before the agent can stop.
# Prevents agents from declaring "done" with broken code.
INPUT=$(cat)
ALREADY_ACTIVE=$(echo "$INPUT" | jq -r '.stop_hook_active // false')
if [ "$ALREADY_ACTIVE" = "true" ]; then echo '{}'; exit 0; fi

cd "$(git rev-parse --show-toplevel 2>/dev/null || echo .)"
if make build > /dev/null 2>&1; then
  echo '{}'
else
  echo '{"hookSpecificOutput":{"hookEventName":"Stop","decision":"block","reason":"Build failed. Run make build, fix errors, then try again."}}'
fi
exit 0
