#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
build_root="$project_root/.build-macos"
craft_prefix="$build_root/craft"
skip_deps=false
run_tests=false
for argument in "$@"; do
    case "$argument" in
        --skip-deps) skip_deps=true ;;
        --run-tests) run_tests=true ;;
        *) echo "Usage: $0 [--skip-deps] [--run-tests]" >&2; exit 2 ;;
    esac
done

if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
    echo "This script builds Okular for Apple Silicon macOS." >&2
    exit 1
fi
mkdir -p "$build_root"

python_for_craft=""
for candidate in "$craft_prefix/bin/python3" "${CRAFT_PYTHON_BIN:-}" "$(command -v python3 || true)"; do
    if [[ -n "$candidate" && -x "$candidate" ]] && "$candidate" -c 'import sys; sys.exit(sys.version_info < (3, 11))'; then
        python_for_craft="$candidate"
        break
    fi
done
if [[ -z "$python_for_craft" ]]; then
    echo "Python 3.11 or newer is required to bootstrap Craft (brew install python)." >&2
    exit 1
fi

if [[ ! -f "$craft_prefix/craft/craftenv.sh" ]]; then
    /usr/bin/curl --fail --location https://raw.githubusercontent.com/KDE/craft/master/setup/CraftBootstrap.py -o "$build_root/CraftBootstrap.py"
    "$python_for_craft" "$build_root/CraftBootstrap.py" --prefix "$craft_prefix" --use-defaults 2>&1 | tee "$build_root/bootstrap.log"
fi

export CRAFT_PYTHON_BIN="$python_for_craft"
# Craft's environment script changes the working directory and expects optional
# shell variables to be unset, so temporarily disable nounset while sourcing it.
set +u
source "$craft_prefix/craft/craftenv.sh"
set -u

"$python_for_craft" - "$craft_prefix" "$project_root" <<'PY'
from pathlib import Path
import os
import sys

prefix = Path(sys.argv[1])
project = Path(sys.argv[2])
# Unlike craft.py, a Python stdin script does not automatically include Craft's
# bin directory in sys.path. Initialize it here, independently of PYTHONPATH.
craft_modules = prefix / 'craft/bin'
if not (craft_modules / 'Blueprints/CraftDependencyPackage.py').is_file():
    raise SystemExit(f'Craft Python modules were not found in {craft_modules}')
sys.path.insert(0, str(craft_modules))
os.environ['craftRoot'] = str(prefix / 'craft')
blueprint = prefix / 'etc/blueprints/locations/craft-blueprints-kde/kde/applications/okular/okular.py'
text = blueprint.read_text()
dependency = '        self.runtimeDependencies["libs/qpdf"] = None\n'
if dependency.strip() not in text:
    anchor = '        self.runtimeDependencies["qt-libs/poppler"] = None\n'
    if anchor not in text:
        raise SystemExit('The Craft Okular blueprint changed; update the QPDF dependency insertion.')
    text = text.replace(anchor, anchor + dependency)

# Keep the Craft runtime dependency graph focused on PDF support. Generic Qt/KDE
# libraries remain; these packages only provide non-PDF document backends,
# image backends, or embedded-media playback.
pdf_only_dependencies = [
    '        self.runtimeDependencies["libs/tiff"] = None\n',
    '        self.runtimeDependencies["qt-libs/phonon"] = None\n',
    '        self.runtimeDependencies["kde/frameworks/tier1/kimageformats"] = None\n',
    '        self.runtimeDependencies["kde/kdegraphics/kdegraphics-mobipocket"] = None\n',
    '            self.buildDependencies["libs/chm"] = None\n',
    '            self.runtimeDependencies["libs/libspectre"] = None\n',
    '            self.runtimeDependencies["libs/discount"] = None\n',
    '            self.runtimeDependencies["libs/djvu"] = None\n',
    '            self.runtimeDependencies["libs/ebook-tools"] = None\n',
    '            self.runtimeDependencies["libs/ghostscript"] = None\n',
    '            self.runtimeDependencies["kde/applications/libkexiv2"] = None\n',
]
dependency_marker = '# MOBIDOC_PDF_ONLY_DEPENDENCIES\n'
if dependency_marker not in text:
    missing = [entry.strip() for entry in pdf_only_dependencies if entry not in text]
    if missing:
        raise SystemExit('The Craft Okular blueprint changed; missing PDF-only dependency entries: ' + ', '.join(missing))
    for entry in pdf_only_dependencies:
        text = text.replace(entry, '')
    anchor = '\n\n\nclass Package'
    if anchor not in text:
        raise SystemExit('The Craft Okular blueprint changed; unable to mark the PDF-only dependency set.')
    text = text.replace(anchor, '\n\n' + dependency_marker + '\nclass Package', 1)

# Purpose's QR-sharing support brings in Prison's camera scanner, Qt Multimedia
# and FFmpeg. PDF editing/saving does not use this optional sharing integration.
text = text.replace('            self.runtimeDependencies["kde/frameworks/tier3/purpose"] = None\n', '')

# The optional KWallet integration can block a standalone Finder launch while
# waiting for a DBus session. Password entry works without password storage.
lines = text.splitlines(keepends=True)
for index, line in enumerate(lines):
    if 'self.subinfo.options.configure.args +=' in line and ('-DCMAKE_DISABLE_FIND_PACKAGE_KF6Wallet=ON' in line or '-DFORCE_NOT_REQUIRED_DEPENDENCIES=LibSpectre' in line):
        indentation = line[:len(line) - len(line.lstrip())]
        options = [
            '-DFORCE_NOT_REQUIRED_DEPENDENCIES=KF6Wallet',
            '-DCMAKE_DISABLE_FIND_PACKAGE_KF6Wallet=ON',
            '-DCMAKE_DISABLE_FIND_PACKAGE_KF6Purpose=ON',
            '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6MultimediaWidgets=ON',
            '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6Multimedia=ON',
            '-DCMAKE_DISABLE_FIND_PACKAGE_LibSpectre=ON',
            '-DMOBIDOC_PDF_ONLY=ON',
            '-DOKULAR_UI=desktop',
        ]
        lines[index] = indentation + 'self.subinfo.options.configure.args += ' + repr(options) + '\n'
        break
else:
    raise SystemExit('The Craft macOS options changed; update the PDF-only CMake configuration.')
text = ''.join(lines)

# Teach Craft's macOS packager about the custom bundle name.
if 'self.defines["appname"] = "MobiDoc"' not in text:
    anchor = '        self.defines["mimetypes"] = ["application/pdf"]\n'
    if anchor not in text:
        raise SystemExit('The Craft Okular blueprint changed; update the MobiDoc app-name insertion.')
    text = text.replace(anchor, anchor + '        self.defines["appname"] = "MobiDoc"\n')
text = text.replace('self.addExecutableFilter(r"bin/(?!(okular|', 'self.addExecutableFilter(r"bin/(?!(MobiDoc|okular|')
file_types_anchor = 'self.defines["file_types"] = [".pdf", ".mobi", ".epub", ".tiff", ".djvu"]'
if file_types_anchor in text:
    text = text.replace(file_types_anchor, 'self.defines["file_types"] = [".pdf"]')
elif 'self.defines["file_types"] = [".pdf"]' not in text:
    raise SystemExit('The Craft Okular blueprint changed; update the PDF-only file type filter.')

packaging_marker = '        # MOBIDOC_PDF_ONLY_EXCLUDED_PACKAGES\n'
if packaging_marker not in text:
    anchor = '        return super().createPackage()'
    if anchor not in text:
        raise SystemExit('The Craft Okular packaging entry point changed.')
    excluded_packages = [
        'libs/ghostscript', 'libs/libspectre', 'libs/ffmpeg',
        'libs/qt/qtmultimedia', 'libs/qt6/qtmultimedia',
        'qt-libs/phonon', 'qt-libs/phonon-vlc',
        'kde/frameworks/tier3/purpose', 'kde/frameworks/tier1/prison',
    ]
    text = text.replace(anchor, packaging_marker + '        self.ignoredPackages.extend(' + repr(excluded_packages) + ')\n' + anchor, 1)
blueprint.write_text(text)

# KNotifications 6.31 uses native macOS notifications and does not link to
# QtTextToSpeech. Its Craft blueprint still lists Qt Speech, which in Qt 6.12
# pulls in Qt Multimedia/FFmpeg. Remove that obsolete macOS dependency only.
notifications = prefix / 'etc/blueprints/locations/craft-blueprints-kde/kde/frameworks/tier3/knotifications/knotifications.py'
notification_text = notifications.read_text()
notification_marker = '# MOBIDOC_NATIVE_MAC_NOTIFICATIONS'
if notification_marker not in notification_text:
    anchor = '            self.runtimeDependencies["libs/qt/qtspeech"] = None\n'
    if anchor not in notification_text:
        raise SystemExit('The KNotifications Craft dependencies changed; review the native macOS speech dependency.')
    replacement = ('            ' + notification_marker + '\n'
                   + '            if not CraftCore.compiler.platform.isMacOS:\n'
                   + '    ' + anchor)
    notifications.write_text(notification_text.replace(anchor, replacement, 1))

# Keep KTextEdit (PDF form fields and annotation editors), but rebuild its
# optional speech component out using the upstream CMake switch.
textwidgets = prefix / 'etc/blueprints/locations/craft-blueprints-kde/kde/frameworks/tier3/ktextwidgets/ktextwidgets.py'
widget_text = textwidgets.read_text()
widget_marker = '# MOBIDOC_TEXT_WIDGETS_WITHOUT_SPEECH'
if widget_marker not in widget_text:
    anchor = '        self.runtimeDependencies["libs/qt/qtspeech"] = None\n'
    if anchor not in widget_text:
        raise SystemExit('The KTextWidgets Craft speech dependency changed; review WITH_TEXT_TO_SPEECH.')
    widget_text = widget_text.replace(anchor,
        '        if not CraftCore.compiler.platform.isMacOS:\n    ' + anchor, 1)
    if 'from CraftCore import CraftCore' not in widget_text:
        widget_text = widget_text.replace('import info\n', 'import info\nfrom CraftCore import CraftCore\n', 1)
    anchor = '        super().__init__(**kwargs)'
    if anchor not in widget_text:
        raise SystemExit('The KTextWidgets Craft configure entry point changed.')
    widget_text = widget_text.replace(anchor, anchor + '\n        ' + widget_marker
        + '\n        if CraftCore.compiler.platform.isMacOS:\n'
        + '            self.subinfo.options.configure.args += ["-DWITH_TEXT_TO_SPEECH=OFF"]', 1)
    textwidgets.write_text(widget_text)

# Exclude optional playback plugins and their libraries before Craft resolves
# binary dependencies. Any library required by a retained PDF component will
# still be added back by the macOS dependency bundler.
blacklist = blueprint.parent / 'blacklist_mac.txt'
blacklist_text = blacklist.read_text()
blacklist_marker = '# MOBIDOC_PDF_ONLY_MEDIA_FILTER\n'
blacklist_end = '# END_MOBIDOC_PDF_ONLY_MEDIA_FILTER\n'
if blacklist_marker in blacklist_text:
    start = blacklist_text.index(blacklist_marker)
    end = blacklist_text.find(blacklist_end, start)
    blacklist_text = blacklist_text[:start] + (blacklist_text[end + len(blacklist_end):] if end >= 0 else '')
blacklist_text += '\n' + blacklist_marker + r'''(?:lib/)?plugins/(multimedia|texttospeech)/.*
translations/(qtmultimedia|qtspeech)_[^/]*\.qm
qml/QtMultimedia/.*
qml/org/kde/prison/scanner/.*
lib/QtMultimedia[^/]*\.framework/.*
lib/QtTextToSpeech\.framework/.*
lib/libPrisonScanner[^/]*
lib/libgs(?:\.[^/]*)?
lib/libspectre[^/]*
lib/libav(codec|device|filter|format|util)[^/]*
lib/libsw(resample|scale)[^/]*
lib/libpostproc[^/]*
bin/(gs|ffmpeg|ffprobe|ffplay|ps2pdf[0-9]*|pdf2ps)
''' + blacklist_end
blacklist.write_text(blacklist_text)

# MacAppPackager and MacDMGPackager both use this stage. Reject dependencies
# reintroduced by the dylib bundler before signing the app or producing a DMG.
mac_packager = prefix / 'craft/bin/Packager/MacBasePackager.py'
mac_text = mac_packager.read_text()
audit_marker = '        # MOBIDOC_PDF_ONLY_BUNDLE_AUDIT\n'
if audit_marker not in mac_text:
    anchor = '        return CodeSign.signMacApp(appPath)'
    if anchor not in mac_text:
        raise SystemExit('The Craft macOS packager signing entry point changed.')
    audit = project / 'scripts/audit-macos-pdf-bundle.py'
    hook = (audit_marker
            + '        if appPath.name == "MobiDoc.app":\n'
            + f'            if not utils.system([sys.executable, {str(audit)!r}, str(appPath)]):\n'
            + '                return False\n')
    mac_text = mac_text.replace(anchor, hook + anchor, 1)
    mac_packager.write_text(mac_text)

# Reuse Craft's complete macOS app bundling, without also generating a DMG.
packager = prefix / 'craft/bin/Packager/MacAppPackager.py'
packager.write_text('''from Packager.MacBasePackager import MacBasePackager

class MacAppPackager(MacBasePackager):
    def createPackage(self):
        return self.internalCreatePackage(self.setDefaults(self.defines))
''')
dispatcher = prefix / 'craft/bin/Packager/TypePackager.py'
text = dispatcher.read_text()
import_line = 'from Packager.MacAppPackager import MacAppPackager\n'
if import_line not in text:
    anchor = 'from Packager.MacDMGPackager import MacDMGPackager\n'
    if anchor not in text:
        raise SystemExit('The Craft packager dispatcher changed; update the app-only packager registration.')
    dispatcher.write_text(text.replace(anchor, anchor + import_line))

# Check the real Craft graph, including build dependencies, rather than trusting
# file filters alone. A newly introduced mandatory dependency must be reviewed.
from Blueprints.CraftDependencyPackage import CraftDependencyPackage, DependencyType
from Blueprints.CraftPackageObject import CraftPackageObject
root = CraftDependencyPackage(CraftPackageObject.get('kde/applications/okular'))
dependencies = root.getDependencies(DependencyType.All)
forbidden = {'libs/ghostscript', 'libs/libspectre', 'libs/ffmpeg',
             'libs/qt/qtmultimedia', 'libs/qt6/qtmultimedia',
             'libs/qt/qtspeech', 'libs/qt6/qtspeech', 'qt-libs/phonon', 'qt-libs/phonon-vlc'}
found = sorted({str(package) for package in dependencies} & forbidden)
if found:
    raise SystemExit('The PDF-only build still requires excluded packages: ' + ', '.join(found))
print('PDF-only Craft dependency graph: Ghostscript and FFmpeg are not required.')
PY

if [[ "$skip_deps" != true ]]; then
    craft --options libs/llvm.ignored=True --install-deps okular 2>&1 | tee "$build_root/dependencies.log"
fi

# A previously downloaded binary cache still contains speech even after its
# blueprint changes. Rebuild this dependency from source before linking MobiDoc.
textwidgets_library="$craft_prefix/lib/libKF6TextWidgets.6.dylib"
if [[ ! -f "$textwidgets_library" ]] || /usr/bin/otool -L "$textwidgets_library" | /usr/bin/grep -Eq 'Qt(TextToSpeech|Multimedia)'; then
    craft --options libs/llvm.ignored=True --fetch --unpack --configure --compile --install --qmerge kde/frameworks/tier3/ktextwidgets 2>&1 | tee "$build_root/textwidgets-without-speech.log"
fi
if /usr/bin/otool -L "$textwidgets_library" | /usr/bin/grep -Eq 'Qt(TextToSpeech|Multimedia)'; then
    echo "KTextWidgets still links to speech/multimedia; refusing to build the PDF-only app." >&2
    exit 1
fi

craft_options=(--options "okular.srcDir=$project_root" --options "okular.version=master" --options "okular.buildTests=True" --options "[Compile]Jobs=${OKULAR_BUILD_JOBS:-4}" --options libs/llvm.ignored=True)
craft "${craft_options[@]}" --configure --compile --install --qmerge okular 2>&1 | tee "$build_root/build.log"

app_build="$craft_prefix/build/kde/applications/okular/work/build"
if [[ "$run_tests" == true ]]; then
    ctest --test-dir "$app_build" -R '^(pdfBookmarksTest|documenttest)$' --output-on-failure 2>&1 | tee "$build_root/tests.log"
fi

craft "${craft_options[@]}" --options '[Packager]PackageType=MacAppPackager' --package okular 2>&1 | tee "$build_root/package.log"

"$python_for_craft" - "$project_root" "$craft_prefix" <<'PY'
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

project, prefix = map(Path, sys.argv[1:])
archive = prefix / 'build/kde/applications/okular/archive'
apps = list(archive.rglob('MobiDoc.app'))
if len(apps) != 1:
    raise SystemExit(f'Expected one packaged MobiDoc.app, found {len(apps)}')
destination = project / 'dist/MobiDoc.app'
destination.parent.mkdir(exist_ok=True)
if destination.exists():
    shutil.rmtree(destination)
subprocess.run(['/usr/bin/ditto', str(apps[0]), str(destination)], check=True)
info = destination / 'Contents/Info.plist'
with info.open('rb') as stream:
    metadata = plistlib.load(stream)
metadata.update(CFBundleIdentifier='com.mobicod.mobidoc', CFBundleName='MobiDoc', CFBundleDisplayName='MobiDoc')
with info.open('wb') as stream:
    plistlib.dump(metadata, stream)
subprocess.run([sys.executable, str(project / 'scripts/trim-macos-bundle.py'), str(destination)], check=True)
subprocess.run([sys.executable, str(project / 'scripts/audit-macos-pdf-bundle.py'), str(destination)], check=True)
subprocess.run(['/usr/bin/codesign', '--force', '--deep', '--sign', '-', str(destination)], check=True)
subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(destination)], check=True)
print(f'Application ready: {destination}')
PY
