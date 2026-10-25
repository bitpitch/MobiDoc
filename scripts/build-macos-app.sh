#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
output_directory="/Users/pitch/CODICE/COMPILATI/MAC/MobiCod"
output_app="$output_directory/MobiDoc.app"
legacy_build_directory="/Users/pitch/CODICE/WORK/okular"
legacy_build_link="$legacy_build_directory/.build-macos"
created_legacy_directory=false
created_legacy_link=false

cleanup_build_compatibility_link() {
    if [[ "$created_legacy_link" == true ]]; then
        rm -f "$legacy_build_link"
    fi
    if [[ "$created_legacy_directory" == true ]]; then
        rmdir "$legacy_build_directory" 2>/dev/null || true
    fi
}

if [[ ! -e "$legacy_build_directory" ]]; then
    mkdir "$legacy_build_directory"
    created_legacy_directory=true
fi
if [[ ! -e "$legacy_build_link" ]]; then
    ln -s "$project_root/.build-macos" "$legacy_build_link"
    created_legacy_link=true
fi
trap cleanup_build_compatibility_link EXIT

# Use the bundled Python runtime when available; Craft requires Python 3.11+.
if [[ -z "${CRAFT_PYTHON_BIN:-}" && -x "$HOME/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3" ]]; then
    export CRAFT_PYTHON_BIN="$HOME/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3"
fi

"$project_root/scripts/build-macos.sh"

source_app="$project_root/dist/MobiDoc.app"
if [[ ! -d "$source_app/Contents/MacOS" || ! -x "$source_app/Contents/MacOS/MobiDoc" ]]; then
    echo "MobiDoc.app was not produced as a launchable application bundle." >&2
    exit 1
fi

mkdir -p "$output_directory"
rm -rf "$output_app"
ditto "$source_app" "$output_app"

echo "Standalone MobiDoc application ready: $output_app"
echo "Open MobiDoc.app to launch it. This build is not signed with Developer ID and has no installer."
