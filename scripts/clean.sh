#!/usr/bin/env zsh

set -euo pipefail

projroot="$(cd "$(dirname "$0")"/.. && pwd)"
cd "$projroot"

echo "Cleaning logs and transient files in $projroot"

# Logs
rm -f debug.log
find . -type f -name "*.log" -delete || true

# Backup and scratch files
find . -type f \( -name "*.bak" -o -name "*.tmp" -o -name "*.swp" -o -name "*.orig" -o -name "*.old" \) -delete || true
find . -maxdepth 1 -type f -name "*.txt" -delete || true

# Save states (optional: comment out if you want to keep)
find . -type f \( -name "*.sav" -o -name "*.state" \) -delete || true

echo "Cleaning build artifacts"
rm -rf build/generated/*
rm -rf build/bin/*
rm -rf build/lib/*

echo "Done."