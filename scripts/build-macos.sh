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

"$python_for_craft" - "$craft_prefix" <<'PY'
from pathlib import Path
import sys

prefix = Path(sys.argv[1])
blueprint = prefix / 'etc/blueprints/locations/craft-blueprints-kde/kde/applications/okular/okular.py'
text = blueprint.read_text()
dependency = '        self.runtimeDependencies["libs/qpdf"] = None\n'
if dependency.strip() not in text:
    anchor = '        self.runtimeDependencies["qt-libs/poppler"] = None\n'
    if anchor not in text:
        raise SystemExit('The Craft Okular blueprint changed; update the QPDF dependency insertion.')
    text = text.replace(anchor, anchor + dependency)

# The optional KWallet integration can block a standalone Finder launch while
# waiting for a DBus session. Password entry works without password storage.
wallet_anchor = '["-DFORCE_NOT_REQUIRED_DEPENDENCIES=LibSpectre"]'
wallet_options = '["-DFORCE_NOT_REQUIRED_DEPENDENCIES=LibSpectre;KF6Wallet", "-DCMAKE_DISABLE_FIND_PACKAGE_KF6Wallet=ON"]'
if wallet_anchor in text:
    text = text.replace(wallet_anchor, wallet_options)
elif '-DCMAKE_DISABLE_FIND_PACKAGE_KF6Wallet=ON' not in text:
    raise SystemExit('The Craft macOS options changed; update the optional KWallet configuration.')
blueprint.write_text(text)

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
PY

if [[ "$skip_deps" != true ]]; then
    craft --options libs/llvm.ignored=True --install-deps okular 2>&1 | tee "$build_root/dependencies.log"
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
import subprocess
import sys

project, prefix = map(Path, sys.argv[1:])
archive = prefix / 'build/kde/applications/okular/archive'
apps = list(archive.rglob('okular.app'))
if len(apps) != 1:
    raise SystemExit(f'Expected one packaged okular.app, found {len(apps)}')
destination = project / 'dist/Okular Bookmarks.app'
destination.parent.mkdir(exist_ok=True)
subprocess.run(['/usr/bin/ditto', str(apps[0]), str(destination)], check=True)
info = destination / 'Contents/Info.plist'
with info.open('rb') as stream:
    metadata = plistlib.load(stream)
metadata.update(CFBundleIdentifier='local.okular.bookmarks', CFBundleName='Okular Bookmarks', CFBundleDisplayName='Okular Bookmarks')
with info.open('wb') as stream:
    plistlib.dump(metadata, stream)
subprocess.run(['/usr/bin/codesign', '--force', '--deep', '--sign', '-', str(destination)], check=True)
subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(destination)], check=True)
print(f'Application ready: {destination}')
PY
