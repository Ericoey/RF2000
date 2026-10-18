#!/usr/bin/env bash
# Compile the uploaded Repetier community reference, never upload it.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export REPETIER_PROJECT_ROOT="$repo_dir"
export REPETIER_SOURCE="${REPETIER_SOURCE:-$repo_dir/references/repetier}"
if [[ ! -f "$REPETIER_SOURCE/Repetier/Repetier.ino" && -f /workspace/references/repetier/Repetier/Repetier.ino ]]; then
  export REPETIER_SOURCE=/workspace/references/repetier
fi
export ARDUINO_AVR_CORE="${ARDUINO_AVR_CORE:-/workspace/.tools/repetier/ArduinoCore-avr}"
export AVR_PREFIX="${AVR_PREFIX:-/workspace/.tools/avr/usr/bin/avr-}"
export REPETIER_BUILD_DIR="${REPETIER_BUILD_DIR:-$repo_dir/build/repetier}"
export REPETIER_ARTIFACT_DIR="${REPETIER_ARTIFACT_DIR:-$repo_dir/artifacts/repetier}"
# A stable build date; override intentionally for a dated reference image.
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1767225600}"
python3 - <<'PY'
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(os.environ["REPETIER_PROJECT_ROOT"])
original = Path(os.environ["REPETIER_SOURCE"])
core_root = Path(os.environ["ARDUINO_AVR_CORE"])
build = Path(os.environ["REPETIER_BUILD_DIR"])
out = Path(os.environ["REPETIER_ARTIFACT_DIR"])
prefix = os.environ["AVR_PREFIX"]
core_commit = "42fa4a1ea1b1b11d1cc0a60298e529d37f9d14bd"
if not (original / "Repetier/Repetier.ino").is_file():
    sys.exit("Repetier reference missing; set REPETIER_SOURCE to the extracted uploaded archive.")
if not (core_root / "cores/arduino/Arduino.h").is_file():
    sys.exit("Arduino AVR Core 1.8.6 missing. See docs/repetier-build.md for installation.")
if subprocess.check_output(["git", "-C", str(core_root), "rev-parse", "HEAD"], text=True).strip() != core_commit:
    sys.exit("Unexpected Arduino AVR Core revision; expected the pinned 1.8.6 commit.")
if subprocess.check_output(["git", "-C", str(core_root), "status", "--porcelain", "--untracked-files=no"], text=True).strip():
    sys.exit("Arduino AVR Core has tracked changes; use the unmodified pinned reference.")
for tool in ("gcc", "g++", "gcc-ar", "objcopy", "size"):
    if not shutil.which(prefix + tool):
        sys.exit(f"Missing {prefix + tool}; run the environment toolchain installer.")

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

source_manifest = {str(p.relative_to(original)): sha(p) for p in sorted(original.rglob("*")) if p.is_file()}
build.mkdir(parents=True, exist_ok=True)
out.mkdir(parents=True, exist_ok=True)
for filename in ("repetier-rf2000v2-reference.elf", "repetier-rf2000v2-reference.hex",
                 "repetier-rf2000v2-reference.eep", "repetier-rf2000v2-reference.map",
                 "size.txt", "build-manifest.json", "SHA256SUMS"):
    (out / filename).unlink(missing_ok=True)
src = build / "source/Repetier"
if src.exists():
    shutil.rmtree(src)
shutil.copytree(original / "Repetier", src)
# The supplied community version deliberately stops RF2000v2 builds until this
# acknowledgement is made. Change only the disposable build copy, never source.
configuration = src / "Configuration.h"
gate = "    #error This Mod-Firmware has not been massively tested on a RF2000v2 yet. But the firmware has been created with care. Please remove this message if you acknowledged this."
text = configuration.read_text()
if text.count(gate) != 1:
    sys.exit("RF2000v2 acknowledgement gate differs from the reviewed source; inspect it before building.")
configuration.write_text(text.replace(gate, "    // Build-only acknowledgement: community RF2000v2 is not hardware-validated.", 1))
(src / "Repetier.ino.cpp").write_bytes((src / "Repetier.ino").read_bytes())

includes = [src, core_root / "cores/arduino", core_root / "variants/mega",
            core_root / "libraries/SPI/src", core_root / "libraries/Wire/src",
            core_root / "libraries/Wire/src/utility"]
common = ["-g", "-Os", "-mmcu=atmega2560", "-DF_CPU=16000000L", "-DARDUINO=10819",
          "-DARDUINO_AVR_MEGA2560", "-DARDUINO_ARCH_AVR", "-DMOTHERBOARD=DEVICE_TYPE_RF2000v2",
          "-ffunction-sections", "-fdata-sections", "-flto", "-MMD"]
common += ["-I" + str(path) for path in includes]
cflags = ["-std=gnu11", "-fno-fat-lto-objects"]
cppflags = ["-std=gnu++11", "-fpermissive", "-fno-exceptions", "-fno-threadsafe-statics", "-Wno-error=narrowing"]

commands = []
def run(command):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.stdout:
        print(result.stdout, end="", flush=True)
    if result.returncode:
        raise subprocess.CalledProcessError(result.returncode, command)

def compile_file(path, category):
    obj = build / "objects" / category / (path.name + ".o")
    obj.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix == ".cpp":
        command = [prefix + "g++", "-c"] + common + cppflags
    elif path.suffix == ".S":
        command = [prefix + "gcc", "-c"] + common + ["-x", "assembler-with-cpp"]
    else:
        command = [prefix + "gcc", "-c"] + common + cflags
    command += [str(path), "-o", str(obj)]
    return command, obj

core_sources = [p for p in sorted((core_root / "cores/arduino").iterdir()) if p.suffix in (".c", ".cpp", ".S")]
firmware_sources = sorted(src.glob("*.cpp"))
library_sources = [core_root / "libraries/SPI/src/SPI.cpp", core_root / "libraries/Wire/src/Wire.cpp",
                   core_root / "libraries/Wire/src/utility/twi.c"]
core_jobs = [compile_file(p, "core") for p in core_sources]
firmware_jobs = [compile_file(p, "firmware") for p in firmware_sources]
library_jobs = [compile_file(p, "library") for p in library_sources]
jobs = min(4, max(1, int(os.environ.get("BUILD_JOBS", "2"))))
all_jobs = core_jobs + firmware_jobs + library_jobs
commands.extend(command for command, obj in all_jobs)
print(f"Building Repetier 1.45.01 RF2000v2 reference with {jobs} compiler workers.", flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
    list(pool.map(lambda job: run(job[0]), all_jobs))

archive = build / "core.a"
archive.unlink(missing_ok=True)
command = [prefix + "gcc-ar", "rcs", str(archive)] + [str(obj) for command, obj in core_jobs]
commands.append(command)
run(command)
elf = out / "repetier-rf2000v2-reference.elf"
hexfile = out / "repetier-rf2000v2-reference.hex"
eep = out / "repetier-rf2000v2-reference.eep"
mapfile = out / "repetier-rf2000v2-reference.map"
command = [prefix + "gcc", "-Os", "-g", "-flto", "-fuse-linker-plugin", "-mmcu=atmega2560",
           "-Wl,--gc-sections", "-Wl,-Map=" + str(mapfile), "-o", str(elf)]
command += [str(obj) for command, obj in firmware_jobs + library_jobs] + [str(archive), "-lm"]
commands.append(command)
run(command)
for args in (["-O", "ihex", "-R", ".eeprom", str(elf), str(hexfile)],
             ["-O", "ihex", "-j", ".eeprom", "--set-section-flags=.eeprom=alloc,load",
              "--no-change-warnings", "--change-section-lma", ".eeprom=0", str(elf), str(eep)]):
    command = [prefix + "objcopy"] + args
    commands.append(command)
    run(command)
size = subprocess.check_output([prefix + "size", "-A", str(elf)], text=True)
(out / "size.txt").write_text(size)
print(size)
# Mega2560 bootloader leaves 253952 bytes; reserve it explicitly.
sections = {line.split()[0]: int(line.split()[1]) for line in size.splitlines() if line.startswith(".")}
flash = sections.get(".text", 0) + sections.get(".data", 0)
ram = sections.get(".data", 0) + sections.get(".bss", 0) + sections.get(".noinit", 0)
if flash > 253952 or ram > 8192:
    sys.exit(f"Image does not fit: flash={flash}/253952, static RAM={ram}/8192")
(out / "build-manifest.json").write_text(json.dumps({
    "target": "RF2000v2", "firmware_version": "1.45.01", "source": str(original),
    "source_sha256": source_manifest, "arduino_avr_core": "1.8.6", "arduino_core_commit": core_commit,
    "compiler": subprocess.check_output([prefix + "g++", "--version"], text=True).splitlines()[0],
    "source_date_epoch": os.environ["SOURCE_DATE_EPOCH"], "flash_bytes": flash, "static_ram_bytes": ram,
    "build_copy_change": "Replace only RF2000v2 community-firmware #error acknowledgement gate with comment",
    "feature_milling_mode": 0, "hardware_validated": False, "commands": commands
}, indent=2) + "\n")
after_manifest = {str(p.relative_to(original)): sha(p) for p in sorted(original.rglob("*")) if p.is_file()}
if after_manifest != source_manifest:
    sys.exit("Reference changed during build; discard outputs and inspect the source.")
artifacts = [elf, hexfile, eep, mapfile, out / "size.txt", out / "build-manifest.json"]
(out / "SHA256SUMS").write_text("".join(f"{sha(p)}  {p.name}\n" for p in artifacts))
print(f"Reference image compiled: {flash}/253952 flash bytes; {ram}/8192 static RAM bytes.")
print("This reference retains printer heaters and other original functions. Do not use it as the CNC port.")
PY
