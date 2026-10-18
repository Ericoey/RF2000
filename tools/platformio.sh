#!/usr/bin/env bash
# PlatformIO CLI in the writable cloud tool directory.
set -euo pipefail
rf_tools="${RF2000_TOOL_ROOT:-/workspace/.tools}"
export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$rf_tools/platformio}"
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no
exec "$rf_tools/platformio-venv/bin/pio" "$@"
