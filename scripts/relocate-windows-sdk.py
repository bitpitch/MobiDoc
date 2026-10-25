"""Repair installed SDK metadata after its parent directory has been moved.

This does not rewrite binaries or reuse old application build caches.
"""
import json
import os
import re
import sys
import zipfile
from pathlib import Path


old_root = r'D:\CODICE\WORK\okular\.build-windows'
new_root = Path(r'D:\CODICE\SDK\KDE-WINDOWS')
if Path(sys.argv[1]).resolve() != new_root.resolve():
    raise SystemExit('Unexpected SDK destination')
prefix = new_root / 'craft'
if not (prefix / 'lib/cmake/Qt6/Qt6Config.cmake').is_file():
    raise SystemExit('The migrated Qt SDK was not found')

extensions = {
    '.cmake', '.ini', '.json', '.conf', '.cfg', '.bat', '.cmd', '.ps1', '.psm1',
    '.py', '.pri', '.prl', '.pro', '.pc', '.la', '.txt', '.pth', '.sh', '.pl',
    '.h', '.hpp', '.xml', '.yaml', '.yml', '.properties', '.config', '.ninja',
}
skip = {'.git', '__pycache__', 'build', 'download', 'tmp', 'short', 'relocation'}
pattern = re.compile(rb'D:[/\\]+CODICE[/\\]+WORK[/\\]+okular[/\\]+\.build-windows', re.I)
changed = []
backup_dir = new_root / 'relocation'
backup_dir.mkdir(exist_ok=True)
backup_path = backup_dir / 'original-metadata.zip'
if backup_path.exists():
    try:
        with zipfile.ZipFile(backup_path) as previous:
            previous.testzip()
    except zipfile.BadZipFile:
        partial = backup_dir / 'original-metadata.partial.zip'
        if partial.exists():
            raise SystemExit(f'Preserved partial backup already exists: {partial}')
        backup_path.rename(partial)
    else:
        raise SystemExit('A relocation backup already exists; inspect it before repeating migration')

with zipfile.ZipFile(backup_path, 'w', zipfile.ZIP_DEFLATED) as backup:
    for directory, dirs, files in os.walk(prefix, followlinks=False):
        dirs[:] = [name for name in dirs if name not in skip and not (Path(directory) / name).is_junction()]
        for name in files:
            path = Path(directory) / name
            if path.suffix.lower() not in extensions and name not in {'qmake', 'qtpaths', 'cmake'}:
                continue
            original = path.read_bytes()
            if b'\x00' in original:
                continue
            def replacement(match):
                raw = match.group()
                separator = b'/' if b'/' in raw else (b'\\\\' if b'\\\\' in raw else b'\\')
                return str(new_root).encode().replace(b'\\', separator)
            updated = pattern.sub(replacement, original)
            if updated != original:
                relative = path.relative_to(new_root).as_posix()
                backup.writestr(relative, original)
                path.write_bytes(updated)
                changed.append(relative)

    # Qt executables must use the moved prefix rather than their compiled path.
    qt_conf = prefix / 'bin/qt.conf'
    if qt_conf.exists():
        backup.writestr('craft/bin/qt.conf', qt_conf.read_bytes())
    qt_conf.write_text('[Paths]\nPrefix=..\n', encoding='ascii')

# Cached executable locations are disposable and still refer to the old tree.
cache = prefix / 'etc/cache.pickle'
if cache.exists():
    cache.rename(backup_dir / 'cache.pre-relocation.pickle')
(backup_dir / 'manifest.json').write_text(json.dumps({
    'old_root': old_root, 'new_root': str(new_root),
    'patched_files': changed,
    'note': 'Old application build caches are preserved as history, not reusable build directories.',
}, indent=2), encoding='utf-8')
print(f'SDK metadata updated: {len(changed)} files; originals in {backup_path}')
