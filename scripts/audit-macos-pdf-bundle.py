#!/usr/bin/env python3
"""Check retained PDF components and reject excluded playback/PS dependencies."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import subprocess
import sys


MACHO_MAGIC = {
    b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe',
    b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe',
    b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
    b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca',
}
EXCLUDED_LIBRARY = re.compile(
    r'^(?:libgs(?:\.|$)|libspectre(?:\.|$)|'
    r'libav(?:codec|device|filter|format|util)(?:\.|$)|'
    r'libsw(?:resample|scale)(?:\.|$)|libpostproc(?:\.|$)|'
    r'libphonon|(?:libQt6|Qt)(?:Multimedia[A-Za-z0-9]*|TextToSpeech)(?:\.|$)|'
    r'libPrisonScanner)',
    re.IGNORECASE,
)
EXCLUDED_EXECUTABLE = re.compile(r'^(?:gs|gswin\d*c?|ffmpeg|ffprobe|ffplay|ps2pdf\d*|pdf2ps)$')


def excluded(path):
    value = str(path).replace('\\', '/')
    return bool(
        EXCLUDED_LIBRARY.match(Path(value).name)
        or EXCLUDED_EXECUTABLE.match(Path(value).name)
        or '/plugins/multimedia/' in value.lower()
        or '/plugins/texttospeech/' in value.lower()
        or '/qml/QtMultimedia/' in value
        or '/qml/org/kde/prison/scanner/' in value
    )


def inspect(binary):
    result = subprocess.run(['/usr/bin/otool', '-L', str(binary)],
                            capture_output=True, text=True, check=True)
    identity = subprocess.run(['/usr/bin/otool', '-D', str(binary)],
                              capture_output=True, text=True, check=True)
    identities = {line.strip() for line in identity.stdout.splitlines()[1:]}
    return binary.resolve(), [line.strip().split(' (', 1)[0]
                              for line in result.stdout.splitlines()[1:]
                              if line[:1].isspace()
                              and line.strip().split(' (', 1)[0] not in identities]


def audit(app, roots_only=False, engines_only=False):
    app = app.resolve()
    contents = app / 'Contents'
    files = [p for p in contents.rglob('*') if p.is_file() and not p.is_symlink()]
    binaries = []
    for path in files:
        with path.open('rb') as stream:
            if stream.read(4) in MACHO_MAGIC:
                binaries.append(path)
    with ThreadPoolExecutor(max_workers=8) as executor:
        dependencies = dict(executor.map(inspect, binaries))

    roots = [] if engines_only else [contents / 'MacOS/MobiDoc']
    patterns = ['Frameworks/libpoppler.*.dylib',
                'Frameworks/libpoppler-qt6.*.dylib', 'Frameworks/libqpdf.*.dylib']
    if not engines_only:
        patterns += ['Frameworks/libOkular6Core.*.dylib',
                     'PlugIns/kf6/parts/okularpart.*',
                     'PlugIns/okular_generators/okularGenerator_poppler.*']
    for pattern in patterns:
        matches = [p.resolve() for p in contents.glob(pattern) if p.is_file()]
        if not matches:
            raise RuntimeError(f'Missing retained PDF component: {pattern}')
        roots.extend(matches)

    def resolve(reference, binary):
        if reference.startswith(('/usr/lib/', '/System/Library/')):
            return None
        if reference.startswith('@executable_path/'):
            candidate = contents / 'MacOS' / reference[len('@executable_path/'):]
        elif reference.startswith('@loader_path/'):
            candidate = binary.parent / reference[len('@loader_path/'):]
        elif reference.startswith('@rpath/'):
            candidate = contents / 'Frameworks' / reference[len('@rpath/'):]
        else:
            candidate = Path(reference)
        if not candidate.is_file():
            raise RuntimeError(f'Missing dependency: {binary.relative_to(app)} -> {reference}')
        if not candidate.resolve().is_relative_to(contents):
            raise RuntimeError(f'Dependency outside the bundle: {binary.relative_to(app)} -> {reference}')
        return candidate.resolve()

    pending = list(dict.fromkeys(p.resolve() for p in roots))
    visited = set()
    while pending:
        binary = pending.pop()
        if binary in visited:
            continue
        visited.add(binary)
        if binary not in dependencies:
            raise RuntimeError(f'Not a Mach-O PDF component: {binary}')
        for reference in dependencies[binary]:
            if excluded(reference):
                raise RuntimeError(f'Excluded dependency required by {binary.relative_to(app)}: {reference}')
            target = resolve(reference, binary)
            if target is not None:
                pending.append(target)

    if not (roots_only or engines_only):
        leftovers = [str(p.relative_to(app)) for p in files if excluded(p)]
        if leftovers:
            raise RuntimeError('Excluded components still packaged:\n  ' + '\n  '.join(leftovers))
        for binary, references in dependencies.items():
            for reference in references:
                if excluded(reference):
                    raise RuntimeError(f'Packaged component still requires excluded library: {binary.relative_to(app)} -> {reference}')
                resolve(reference, binary)
    scope = 'PDF engines only' if engines_only else ('PDF roots only' if roots_only else 'complete bundle checked')
    print(f'PDF dependency audit passed: {len(visited)} linked binaries; {scope}.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--roots-only', action='store_true',
                      help='Inspect retained PDF dependency closure in an older bundle.')
    mode.add_argument('--engines-only', action='store_true',
                      help='Inspect Poppler/Qt and QPDF dependency closure independently of the GUI.')
    args = parser.parse_args()
    try:
        audit(args.app, args.roots_only, args.engines_only)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f'PDF dependency audit failed: {error}', file=sys.stderr)
        sys.exit(1)
