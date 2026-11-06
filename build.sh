#!/usr/bin/env bash
# Stable Linux/Wine build entry point. Paths resolve relative to this script.
set -euo pipefail
sixbox_root="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec python3 "$sixbox_root/.github/scripts/build-local-dll.py" "$@"
