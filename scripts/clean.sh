#!/usr/bin/env zsh

set -euo pipefail

projroot="$(cd "$(dirname "$0")"/.. && pwd)"
cd "$projroot"

usage() {
	cat <<'EOF'
Usage: ./scripts/clean.sh [options]

Default behavior (no options):
  - Removes logs and common editor backup files
  - Removes build outputs under build/{generated,bin,lib}
  - DOES NOT delete .sav/.state nor dumps* folders unless requested

Options:
  --all            Remove logs, backups, build outputs, saves, and dumps
  --saves          Remove emulator runtime files (*.sav, *.state, etc.)
  --dumps          Remove diagnostic dump folders (dumps*) and *.ppm files
  --no-build       Skip cleaning build outputs
  -h, --help       Show this help

Env toggles (equivalent to flags):
  AIO_CLEAN_SAVES=1    Same as --saves
  AIO_CLEAN_DUMPS=1    Same as --dumps
EOF
}

clean_build=1
clean_saves=0
clean_dumps=0

for arg in "$@"; do
	case "$arg" in
		--all)
			clean_saves=1
			clean_dumps=1
			;;
		--saves)
			clean_saves=1
			;;
		--dumps)
			clean_dumps=1
			;;
		--no-build)
			clean_build=0
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown option: $arg" >&2
			usage >&2
			exit 2
			;;
	esac
done

if [[ "${AIO_CLEAN_SAVES:-0}" == "1" ]]; then
	clean_saves=1
fi
if [[ "${AIO_CLEAN_DUMPS:-0}" == "1" ]]; then
	clean_dumps=1
fi

echo "Cleaning transient files in $projroot"

# Logs
rm -f debug.log
find . -type f -name "*.log" -delete || true

# macOS
find . -type f -name ".DS_Store" -delete || true

# Backup and scratch files
find . -type f \( -name "*.bak" -o -name "*.tmp" -o -name "*.swp" -o -name "*.orig" -o -name "*.old" \) -delete || true

# Scratch files (keep real docs; only remove obvious scratch/temp patterns)
find . -maxdepth 1 -type f \( -name "scratch*.txt" -o -name "tmp*.txt" \) -delete || true

if [[ "$clean_saves" == "1" ]]; then
	echo "Cleaning emulator runtime save/state files"
	find . -type f \( -name "*.sav" -o -name "*.sav.*" -o -name "*.state" -o -name "*.srm" \) -delete || true
else
	echo "Keeping emulator runtime save/state files (use --saves or AIO_CLEAN_SAVES=1 to remove)"
fi

if [[ "$clean_dumps" == "1" ]]; then
	echo "Cleaning diagnostic dumps (dumps*) and *.ppm"
	# Root-level dump folders
	for d in dumps*; do
		if [[ -d "$d" ]]; then
			rm -rf "$d"
		fi
	done
	# Any stray PPM frames
	find . -type f -name "*.ppm" -delete || true
else
	echo "Keeping diagnostic dumps (use --dumps or AIO_CLEAN_DUMPS=1 to remove)"
fi

if [[ "$clean_build" == "1" ]]; then
	echo "Cleaning build artifacts"
	rm -rf build/generated/*
	rm -rf build/bin/*
	rm -rf build/lib/*
else
	echo "Skipping build cleanup (--no-build)"
fi

echo "Done."