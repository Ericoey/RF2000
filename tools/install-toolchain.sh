#!/usr/bin/env bash
# Rootless Debian AVR tools. APT verifies signed metadata and package hashes.
set -euo pipefail
rf_tools="${RF2000_TOOL_ROOT:-/workspace/.tools}"
rf_apt="$rf_tools/apt"
rf_avr="$rf_tools/avr"
if [[ ! -x "$rf_avr/usr/bin/avr-gcc" || ! -f "$rf_avr/usr/lib/x86_64-linux-gnu/libsimavr.so" ]]; then
  mkdir -p "$rf_apt/etc/parts" "$rf_apt/etc/preferences.d" "$rf_apt/etc/sources.list.d" \
    "$rf_apt/state/lists/partial" "$rf_apt/cache/archives/partial" "$rf_apt/downloads" "$rf_avr"
  cat > "$rf_apt/apt.conf" <<EOF
Dir::Etc::main "/dev/null";
Dir::Etc::parts "$rf_apt/etc/parts";
Dir::Etc::sourcelist "$rf_apt/etc/sources.list";
Dir::Etc::sourceparts "$rf_apt/etc/sources.list.d";
Dir::Etc::preferences "/dev/null";
Dir::Etc::preferencesparts "$rf_apt/etc/preferences.d";
Dir::State "$rf_apt/state";
Dir::State::status "/var/lib/dpkg/status";
Dir::Cache "$rf_apt/cache";
APT::Sandbox::User "$(id -un)";
EOF
  cat > "$rf_apt/etc/sources.list" <<'EOF'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.pgp] https://deb.debian.org/debian trixie main
EOF
  export APT_CONFIG="$rf_apt/apt.conf"
  apt-get update
  (
    cd "$rf_apt/downloads"
    apt-get download gcc-avr=1:14.2.0-2 binutils-avr=2.43.50.20250108-1 avr-libc=1:2.2.1-1 \
      simavr=1.6+dfsg-3+b3 libsimavr-dev=1.6+dfsg-3+b3 \
      libsimavr2=1.6+dfsg-3+b3 libsimavrparts1=1.6+dfsg-3+b3
    for rf_deb in ./*.deb; do dpkg-deb -x "$rf_deb" "$rf_avr"; done
    sha256sum ./*.deb > SHA256SUMS
  )
fi
"$rf_avr/usr/bin/avr-gcc" --version | head -n 1
rf_core="$rf_tools/repetier/ArduinoCore-avr"
rf_core_commit=42fa4a1ea1b1b11d1cc0a60298e529d37f9d14bd
if [[ ! -d "$rf_core" ]]; then
  mkdir -p "$(dirname "$rf_core")"
  git clone --depth 1 --branch 1.8.6 https://github.com/arduino/ArduinoCore-avr.git "$rf_core"
fi
[[ "$(git -C "$rf_core" rev-parse HEAD)" == "$rf_core_commit" ]] || {
  echo 'Unexpected Arduino core revision; preserving the existing checkout.' >&2; exit 1;
}
[[ -z "$(git -C "$rf_core" status --porcelain)" ]] || {
  echo 'Arduino core has local changes; preserving them for review.' >&2; exit 1;
}
echo 'AVR tools and pinned Arduino AVR core available. No device was flashed.'
