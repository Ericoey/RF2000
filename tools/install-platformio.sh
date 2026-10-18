#!/usr/bin/env bash
set -euo pipefail
rf_repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
rf_tools="${RF2000_TOOL_ROOT:-/workspace/.tools}"
rf_venv="$rf_tools/platformio-venv"
if [[ ! -x "$rf_venv/bin/python" ]]; then
  python3 -m venv "$rf_venv"
fi
if ! "$rf_venv/bin/python" -c 'import platformio; assert platformio.__version__ == "6.1.19"' 2>/dev/null; then
  "$rf_venv/bin/python" -m pip install --disable-pip-version-check 'platformio==6.1.19'
fi
cd "$rf_repo"
tools/platformio.sh --version
tools/platformio.sh pkg install -e rf2000_grbl
