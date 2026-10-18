#!/usr/bin/env python3
"""Reject images exceeding ATmega2560 flash (8 KiB bootloader reserved) or SRAM."""
import os
from pathlib import Path
import re
import subprocess
import sys

tool = sys.argv[2] if len(sys.argv) > 2 else Path(os.environ.get('TOOLCHAIN_ROOT', '/workspace/.tools/avr')) / 'usr/bin/avr-size'
result = subprocess.run([str(tool), '-A', '-d', sys.argv[1]], check=True, capture_output=True, text=True)
sections = {}
section_row = re.compile(r'^\s*(\.[^\s]+)\s+([0-9]+)\s+([0-9]+)\s*$')
for line in result.stdout.splitlines():
    # A relative ELF filename may itself start with '.', as in
    # '.pio/build/rf2000_grbl/firmware.elf :'. Only accept section rows with
    # numeric size AND address, never the filename or column headings.
    row = section_row.fullmatch(line)
    if row:
        sections[row.group(1)] = int(row.group(2))
# New AVR binutils' Berkeley summary includes initialized data in its text
# column, while hiding it from data. Use actual sections to count SRAM too.
flash = sections.get('.text', 0) + sections.get('.data', 0)
ram = sum(sections.get(name, 0) for name in ('.data', '.bss', '.noinit'))
assert flash > 0, 'missing firmware text section'
assert flash <= 253952, 'image overlaps reserved Mega bootloader region'
assert ram <= 8192, 'static SRAM overflow'
print(f'Flash {flash}/253952 bytes; static SRAM {ram}/8192 bytes')
