#!/usr/bin/env python3
"""Trim the English-only Widgets app while preserving its runtime dependency closure."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import importlib.util
from pathlib import Path
import shutil
import sys


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'bundle_audit', Path(__file__).with_name('audit-macos-pdf-bundle.py'))
bundle_audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle_audit)


DEVELOPMENT_TOOLS = {
    'moc', 'uic', 'rcc', 'syncqt', 'cmake_automoc_parser',
    'tracegen', 'tracepointgen', 'qvkgen', 'qlalr',
    'qmlcachegen', 'qmlimportscanner', 'qmltyperegistrar',
    'qmlaotstats', 'qmljsrootgen', 'kconfig_compiler_kf6',
    'kauth-policy-gen', 'kcmdesktopfilegenerator.app',
}


def remove(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def size(path):
    return sum(p.stat().st_size for p in path.rglob('*')
               if p.is_file() and not p.is_symlink())


def trim(app):
    app = app.resolve()
    contents = app / 'Contents'
    frameworks = contents / 'Frameworks'
    if not (contents / 'MacOS/MobiDoc').is_file() or not frameworks.is_dir():
        raise RuntimeError(f'Not a MobiDoc application bundle: {app}')
    before = size(contents)

    # MobiDoc's desktop UI is English-only. PDF fonts and Poppler character maps
    # remain: those describe document content, rather than UI translations.
    for relative in ('Resources/locale', 'Resources/Translations',
                     'Resources/common-lisp', 'Resources/gettext-1.0',
                     'Resources/kdevappwizard', 'MacOS/gettext', 'PlugIns/qmlls'):
        remove(contents / relative)

    # This Widgets viewer uses QJSEngine for PDF scripts, but has no QML UI.
    # Preserve QtQml itself through the dependency closure below.
    remove(contents / 'Resources/qml')
    # Resolve plugins from this bundle even when launched directly or moved to
    # a machine without the Craft development installation.
    (contents / 'Resources/qt.conf').write_text(
        '[Paths]\nPrefix = .\nPlugins = PlugIns\nLibraries = Frameworks\n'
        'LibraryExecutables = MacOS\nTranslations = Resources/Translations\n')
    for path in list((contents / 'MacOS').rglob('*')):
        if path.name in DEVELOPMENT_TOOLS and (path.is_file() or path.is_dir()):
            remove(path)

    binaries = []
    for path in contents.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                if stream.read(4) in bundle_audit.MACHO_MAGIC:
                    binaries.append(path)
    with ThreadPoolExecutor(max_workers=8) as executor:
        dependencies = dict(executor.map(bundle_audit.inspect, binaries))

    # Keep executable helpers and dynamically loaded plugins. Some plugin families
    # (notably QCA crypto providers) are packaged below Frameworks, outside a
    # .framework bundle, rather than below PlugIns.
    roots = []
    for binary in binaries:
        if not binary.is_relative_to(frameworks):
            roots.append(binary.resolve())
        else:
            family = binary.relative_to(frameworks).parts[0]
            if not family.endswith(('.framework', '.dylib')):
                roots.append(binary.resolve())
    # These libraries may be opened by name rather than a Mach-O load command.
    for pattern in ('libssl.*.dylib', 'libcrypto.*.dylib', 'libKF6BreezeIcons.*.dylib'):
        roots.extend(p.resolve() for p in frameworks.glob(pattern) if p.is_file())

    def resolve(reference, binary):
        if reference.startswith(('/usr/lib/', '/System/Library/')):
            return None
        if reference.startswith('@rpath/'):
            target = frameworks / reference[len('@rpath/'):]
        elif reference.startswith('@loader_path/'):
            target = binary.parent / reference[len('@loader_path/'):]
        elif reference.startswith('@executable_path/'):
            target = contents / 'MacOS' / reference[len('@executable_path/'):]
        else:
            target = Path(reference)
        if not target.is_file() or not target.resolve().is_relative_to(contents):
            raise RuntimeError(f'Unresolved runtime dependency: {binary} -> {reference}')
        return target.resolve()

    pending = roots[:]
    retained = set()
    while pending:
        binary = pending.pop()
        if binary in retained:
            continue
        retained.add(binary)
        if binary not in dependencies:
            raise RuntimeError(f'Uninspected runtime binary: {binary}')
        for reference in dependencies[binary]:
            target = resolve(reference, binary)
            if target is not None:
                pending.append(target)

    removed = []
    for path in list(frameworks.iterdir()):
        if path.suffix == '.framework' and path.is_dir():
            if not any(binary.is_relative_to(path.resolve()) for binary in retained):
                removed.append(path.name)
                remove(path)
        elif path.suffix == '.dylib' and path.resolve() not in retained:
            removed.append(path.name)
            remove(path)

    # A dangling version symlink can remain after its unused library was removed.
    for path in frameworks.iterdir():
        if path.is_symlink() and not path.exists():
            path.unlink()

    # Verify every retained binary and plugin, not just the main executable.
    bundle_audit.audit(app)
    after = size(contents)
    print('Removed unused frameworks/libraries: ' + ', '.join(sorted(removed)))
    print(f'MobiDoc bundle trimmed: {before / 1048576:.1f} -> {after / 1048576:.1f} MiB '
          f'({(before - after) / 1048576:.1f} MiB removed).')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    trim(parser.parse_args().app)
