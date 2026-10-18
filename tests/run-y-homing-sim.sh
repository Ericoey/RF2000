#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
tool_root="${RF2000_AVR_ROOT:-/workspace/.tools/avr}"
sim_lib="$tool_root/usr/lib/x86_64-linux-gnu"
mkdir -p "$repo_root/build/tests"
cc -std=c11 -O2 -Wall -Wextra -Werror \
  -I"$tool_root/usr/include/simavr" \
  "$repo_root/tests/rf2000_y_homing_sim.c" -L"$sim_lib" \
  -Wl,-rpath,"$sim_lib" -lsimavr -o "$repo_root/build/tests/rf2000_y_homing_sim"
"$repo_root/build/tests/rf2000_y_homing_sim" "${1:-$repo_root/build/y-homing/main.elf}" "${@:2}"
