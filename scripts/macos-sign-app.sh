#!/bin/bash
# Usage: ./scripts/macos-sign-app.sh "<app_path>"

set -euo pipefail

APP_PATH="${1:-}"
QTWEBENGINE_FRAMEWORK="$APP_PATH/Contents/Frameworks/QtWebEngineCore.framework"
QTWEBENGINE_PATH="$QTWEBENGINE_FRAMEWORK/Helpers/QtWebEngineProcess.app"

if [[ -z "${CODESIGN_IDENTITY:-}" ]]; then
  echo "CODESIGN_IDENTITY is required"
  exit 1
fi

if [[ -z "$APP_PATH" || ! -d "$APP_PATH" ]]; then
  echo "App not found: $APP_PATH"
  exit 1
fi
# Sign everything first, then give the web engine helper its entitlements,
# which the blanket signature does not carry. Changing the helper breaks the
# seal of every bundle that contains it, so those are signed again from the
# inside out: the framework, then the app.
codesign --verbose --force --options runtime --deep --sign "$CODESIGN_IDENTITY" "$APP_PATH"
codesign --verbose --force --options runtime --sign "$CODESIGN_IDENTITY" --entitlements "$QTWEBENGINE_PATH/Contents/Resources/QtWebEngineProcess.entitlements" "$QTWEBENGINE_PATH"
codesign --verbose --force --options runtime --sign "$CODESIGN_IDENTITY" "$QTWEBENGINE_FRAMEWORK"
codesign --verbose --force --options runtime --sign "$CODESIGN_IDENTITY" "$APP_PATH"

# Fail here rather than at notarization if any seal is broken.
codesign --verify --deep --strict --verbose=2 "$APP_PATH"
