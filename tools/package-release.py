#!/usr/bin/env python3
"""Package reviewed sources, reference documents, and current build artifacts."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import difflib
import hashlib

root = Path(__file__).resolve().parent.parent
artifacts = root / 'artifacts'
upstream = Path('/workspace/references/grbl-upstream')
if upstream.is_dir():
    patch = []
    selected = [root / name for name in ('Makefile', '.gitignore', 'README.md', 'platformio.ini')]
    for directory in ('grbl', 'tools', 'tests', 'docs'):
        selected += [p for p in sorted((root / directory).rglob('*')) if p.is_file()]
    for current in selected:
        rel = current.relative_to(root)
        previous = upstream / rel
        old = previous.read_bytes().decode().splitlines(keepends=True) if previous.exists() else []
        new = current.read_bytes().decode().splitlines(keepends=True)
        patch.extend(difflib.unified_diff(old, new,
            fromfile=str(Path('a') / rel) if previous.exists() else '/dev/null',
            tofile=str(Path('b') / rel)))
    (artifacts / 'grbl/rf2000-port.patch').write_bytes(''.join(patch).encode())

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

for directory in ('grbl', 'y-homing', 'cnc', 'platformio-cnc'):
    build_out = artifacts / directory
    if build_out.is_dir():
        (build_out / 'SHA256SUMS').write_text(''.join(
            f'{sha(p)}  {p.name}\n' for p in sorted(build_out.iterdir())
            if p.is_file() and p.name != 'SHA256SUMS'))

files = []
for p in sorted(root.rglob('*')):
    if not p.is_file():
        continue
    rel = p.relative_to(root)
    if rel.parts[0] in ('.git', '.pio', 'build', 'artifacts') or p.name in ('.DS_Store', 'grbl.zip'):
        continue
    if p.suffix == '.log' or '__pycache__' in rel.parts:
        continue
    files.append(p)
for directory in ('grbl', 'repetier', 'y-homing', 'cnc', 'platformio-cnc'):
    files += [p for p in sorted((artifacts / directory).iterdir()) if p.is_file()]
for name in ('validation.log', 'grbl-repro-build.log', 'repetier-build.log',
             'y-homing-build.log', 'y-homing-simulation.log', 'panel-validation.log',
             'platformio-y-homing-build.log', 'cnc-build.log', 'cnc-simulation.log',
             'gsender-detection.log', 'menu-simulation.log', 'recovery-simulation.log',
             'speed-simulation.log', 'homing-wait-simulation.log',
             'speed-host-validation.log', 'speed-exploration.tsv',
             'adaptive-simulation.log', 'adaptive-platformio-simulation.log',
             'adaptive-host-validation.log', 'platformio-cnc-build.log'):
    p = artifacts / name
    if p.is_file():
        files.append(p)

manifest = ''.join(f'{sha(p)}  {p.relative_to(root)}\n' for p in files)
(artifacts / 'release-manifest.sha256').write_text(manifest)
package = artifacts / 'RF2000-GRBL-Testpaket.zip'
with ZipFile(package, 'w', compression=ZIP_DEFLATED, compresslevel=9) as archive:
    for p in files:
        archive.write(p, Path('RF2000') / p.relative_to(root))
    archive.writestr('RF2000/MANIFEST.sha256', manifest)
with ZipFile(package) as archive:
    assert archive.testzip() is None, 'archive integrity failure'
(artifacts / 'RF2000-GRBL-Testpaket.zip.sha256').write_text(f'{sha(package)}  {package.name}\n')
print(f'{package}: {len(files)} files, {package.stat().st_size} bytes, SHA256 {sha(package)}')
