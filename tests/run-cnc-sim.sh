#!/usr/bin/env bash
# Current CNC validation uses phase-aware adaptive physical STEP accounting.
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$repo_root/tests/run-adaptive-sim.sh" "$@"
