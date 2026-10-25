#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
installer_setup="/Users/pitch/CODICE/WORK/INSTALLER/installer.setup"
legacy_build_directory="/Users/pitch/CODICE/WORK/okular"
legacy_build_link="$legacy_build_directory/.build-macos"
created_legacy_directory=false
created_legacy_link=false
temporary_directory=""

cleanup_build_compatibility_link() {
    if [[ -n "$temporary_directory" ]]; then
        rm -rf "$temporary_directory"
    fi
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

if [[ ! -f "$installer_setup" ]]; then
    echo "Installer signing configuration not found: $installer_setup" >&2
    exit 1
fi

# Reuse the Developer ID and notary profile already used by the plugin installer.
# shellcheck source=/dev/null
source "$installer_setup"

for required_value in CODESIGN_IDENTITY INSTALLER_SIGNING_IDENTITY NOTARY_PROFILE; do
    if [[ -z "${!required_value:-}" ]]; then
        echo "Missing $required_value in $installer_setup" >&2
        exit 1
    fi
done

build_args=()
for argument in "$@"; do
    case "$argument" in
        --skip-deps|--run-tests) build_args+=("$argument") ;;
        *) echo "Usage: $0 [--skip-deps] [--run-tests]" >&2; exit 2 ;;
    esac
done

if (( ${#build_args[@]} > 0 )); then
    "$project_root/scripts/build-macos.sh" "${build_args[@]}"
else
    "$project_root/scripts/build-macos.sh"
fi

app="$project_root/dist/MobiDoc.app"
app_info="$app/Contents/Info.plist"
[[ -d "$app" && -f "$app_info" ]] || {
    echo "MobiDoc.app was not produced by the build." >&2
    exit 1
}

# Keep English UI catalogs only. This affects MobiDoc's menus/dialogs, not the
# language or contents of PDFs opened by the user.
locale_directory="$app/Contents/Resources/locale"
if [[ -d "$locale_directory" ]]; then
    for locale_path in "$locale_directory"/*; do
        [[ -d "$locale_path" ]] || continue
        case "${locale_path##*/}" in
            en|en_GB|en_US) ;;
            *) rm -rf "$locale_path" ;;
        esac
    done
fi

qt_translations="$app/Contents/Resources/Translations"
if [[ -d "$qt_translations" ]]; then
    for catalog in "$qt_translations"/*.qm; do
        [[ -f "$catalog" ]] || continue
        case "${catalog##*/}" in
            *_en.qm) ;;
            *) rm -f "$catalog" ;;
        esac
    done
fi

version_major="$(sed -nE 's/^set \(RELEASE_SERVICE_VERSION_MAJOR "([0-9]+)"\)$/\1/p' "$project_root/CMakeLists.txt" | head -n 1)"
version_minor="$(sed -nE 's/^set \(RELEASE_SERVICE_VERSION_MINOR "([0-9]+)"\)$/\1/p' "$project_root/CMakeLists.txt" | head -n 1)"
version_micro="$(sed -nE 's/^set \(RELEASE_SERVICE_VERSION_MICRO "([0-9]+)"\)$/\1/p' "$project_root/CMakeLists.txt" | head -n 1)"
version="${version_major}.${version_minor}.${version_micro}"
if [[ -z "$version_major" || -z "$version_minor" || -z "$version_micro" ]]; then
    echo "Unable to read the MobiDoc release version from CMakeLists.txt." >&2
    exit 1
fi

# Qt 6.12 in the bundled Craft environment requires macOS 14.4 or later.
/usr/libexec/PlistBuddy -c 'Set :LSMinimumSystemVersion 14.4' "$app_info" || \
    /usr/libexec/PlistBuddy -c 'Add :LSMinimumSystemVersion string 14.4' "$app_info"

echo "Signing MobiDoc.app with Developer ID..."
codesign --force --deep --options runtime --timestamp --sign "$CODESIGN_IDENTITY" "$app"
codesign --verify --deep --strict --verbose=2 "$app"

package_directory="/Users/pitch/CODICE/COMPILATI/MAC/MobiCod"
package="$package_directory/MobiDoc-${version}-Mac-Setup.pkg"
temporary_directory="$(mktemp -d "/private/tmp/mobidoc-installer.XXXXXX")"
payload="$temporary_directory/payload"
component_plist="$temporary_directory/components.plist"

mkdir -p "$package_directory" "$payload/Applications"
rm -f "$package"
ditto "$app" "$payload/Applications/MobiDoc.app"

pkgbuild --analyze --root "$payload" "$component_plist"
/usr/libexec/PlistBuddy -c 'Set :0:BundleIsRelocatable false' "$component_plist"
/usr/libexec/PlistBuddy -c 'Set :0:BundleIsVersionChecked false' "$component_plist"
pkgbuild --root "$payload" \
    --component-plist "$component_plist" \
    --identifier "com.mobicod.mobidoc" \
    --version "$version" \
    --install-location / \
    --sign "$INSTALLER_SIGNING_IDENTITY" \
    "$package"

echo "Submitting MobiDoc installer for notarization..."
xcrun notarytool submit "$package" --keychain-profile "$NOTARY_PROFILE" --wait
xcrun stapler staple "$package"
xcrun stapler validate "$package"
pkgutil --check-signature "$package"

echo "MobiDoc installer ready: $package"
