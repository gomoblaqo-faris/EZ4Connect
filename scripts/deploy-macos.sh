#!/bin/bash
# macOS deployment script
# Usage: ./scripts/deploy-macos.sh "EZ4Connect" "build" "amd64" "false"

set -euo pipefail

TARGET_NAME="${1:-EZ4Connect}"
BUILD_DIR="${2:-build}"
ARCH="${3:-arm64}"
NIGHTLY="${4:-false}"
APP_PATH="$TARGET_NAME.app"

# Copy app bundle
cp -R "$BUILD_DIR/$TARGET_NAME.app" .

# Download and extract zju-connect
ZJU_ARCH="${ARCH}"
if [ "$ARCH" = "x86_64" ]; then
    ZJU_ARCH="amd64"
fi

ZJU_ASSET="zju-connect-darwin-$ZJU_ARCH.zip"
ZJU_RELEASES="https://github.com/Mythologyli/zju-connect/releases"
PINNED_RELEASE="$(cd "$(dirname "$0")" && pwd)/zju-connect-release.txt"

if [ "$NIGHTLY" = "true" ]; then
    # A nightly build is replaced under the same name, so it cannot be pinned.
    echo "warning: bundling an unverified nightly zju-connect" >&2
    curl -fLO "$ZJU_RELEASES/download/nightly/$ZJU_ASSET"
else
    ZJU_VERSION="$(awk '$1 == "version" {print $2}' "$PINNED_RELEASE")"
    ZJU_SHA256="$(awk -v asset="$ZJU_ASSET" '$1 == asset {print $2}' "$PINNED_RELEASE")"
    if [ -z "$ZJU_VERSION" ] || [ -z "$ZJU_SHA256" ]; then
        echo "error: no pinned version or checksum for $ZJU_ASSET in $PINNED_RELEASE" >&2
        exit 1
    fi
    curl -fLO "$ZJU_RELEASES/download/$ZJU_VERSION/$ZJU_ASSET"
    echo "$ZJU_SHA256  $ZJU_ASSET" | shasum -a 256 -c -
fi
unzip -o "$ZJU_ASSET"
rm "$ZJU_ASSET"

# Copy zju-connect into app bundle
cp zju-connect "$APP_PATH/Contents/MacOS/"

# The command-line client goes next to the core, which it looks for beside
# itself, and shares the bundled Qt.
cp "$BUILD_DIR/ez4connect-cli" "$APP_PATH/Contents/MacOS/"

# Run macdeployqt
macdeployqt "$APP_PATH" -executable="$APP_PATH/Contents/MacOS/ez4connect-cli"

# Reduce bundle size by stripping unused slices from universal Mach-O binaries.
thinned_count=0
before_size=0
after_size=0
while IFS= read -r -d '' binary; do
    file_output="$(file "$binary")"
    if [[ "$file_output" != *"Mach-O universal binary"* ]]; then
        continue
    fi

    lipo_info="$(lipo -info "$binary" 2>/dev/null || true)"
        if [[ "$lipo_info" != *"$ARCH"* ]]; then
            echo "Skipping universal binary without $ARCH slice: $binary" >&2
            continue
        fi

        original_mode="$(stat -f%Lp "$binary")"
        thin_output="$(mktemp)"
        lipo "$binary" -thin "$ARCH" -output "$thin_output"
        chmod "$original_mode" "$thin_output"
        mv "$thin_output" "$binary"

done < <(find "$APP_PATH" -type f -print0)

# macdeployqt has a bug that it doesn't copy the Qt translations to the bundle
# so we need to copy them manually
QT_TRANSLATIONS=$(qmake -query QT_INSTALL_TRANSLATIONS)
mkdir -p "$APP_PATH/Contents/translations"
if [ -d "$QT_TRANSLATIONS" ]; then
    cp -R "$QT_TRANSLATIONS"/qtbase_*.qm "$APP_PATH/Contents/translations"
fi
