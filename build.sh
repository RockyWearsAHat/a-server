#!/usr/bin/env bash
# Build script that works from any directory in the project

# Find project root by looking for the cmake directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# If script is not in project root, search upwards
if [ ! -d "$PROJECT_ROOT/cmake" ]; then
    CURRENT_DIR="$(pwd)"
    while [ "$CURRENT_DIR" != "/" ]; do
        if [ -d "$CURRENT_DIR/cmake" ]; then
            PROJECT_ROOT="$CURRENT_DIR"
            break
        fi
        CURRENT_DIR="$(dirname "$CURRENT_DIR")"
    done
fi

if [ ! -d "$PROJECT_ROOT/cmake" ]; then
    echo "Error: Could not find project root (looking for cmake/ directory)"
    exit 1
fi

cd "$PROJECT_ROOT/cmake" && make "$@"
