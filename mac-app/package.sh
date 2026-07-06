#!/usr/bin/env bash
#
# Package the BPP Lint Editor into a distributable macOS .app bundle and .dmg.
#
# The SwiftUI editor is an SPM executable; this wraps the release binary in a
# proper .app (Info.plist + icon, ad-hoc signed) and a drag-to-Applications
# .dmg. The app finds the `bpp-lint` binary at runtime (Homebrew / PATH /
# BPP_LINT_BINARY), so it is not bundled here -- install it with
# `brew install bpp/tap/bpp-lint`.
#
# Requires: Swift toolchain + macOS built-ins (sips, iconutil, codesign,
# hdiutil). Output lands in ./dist.  Usage:  ./package.sh
#
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

APP_NAME="BPP Lint Editor"
EXE_NAME="BppLintEditor"
BUNDLE_ID="org.bpp.BppLintEditor"
APP_VERSION="${APP_VERSION:-0.1.0}"
DIST="dist"
APP="$DIST/$APP_NAME.app"

echo ">> building release binary"
swift build -c release
BIN="$(swift build -c release --show-bin-path)/$EXE_NAME"
[ -x "$BIN" ] || { echo "error: build produced no $EXE_NAME" >&2; exit 1; }

echo ">> assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/$EXE_NAME"

if [ -f AppIcon.png ]; then
  echo ">> building icon (.png -> .icns)"
  ICONSET="$(mktemp -d)/AppIcon.iconset"
  mkdir -p "$ICONSET"
  for s in 16 32 64 128 256 512; do
    sips -z "$s" "$s"             AppIcon.png --out "$ICONSET/icon_${s}x${s}.png"    >/dev/null
    sips -z "$((s*2))" "$((s*2))" AppIcon.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
  rm -rf "$(dirname "$ICONSET")"
fi

echo ">> writing Info.plist"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>$APP_NAME</string>
  <key>CFBundleDisplayName</key><string>$APP_NAME</string>
  <key>CFBundleExecutable</key><string>$EXE_NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$APP_VERSION</string>
  <key>CFBundleVersion</key><string>$APP_VERSION</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.developer-tools</string>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeName</key><string>BPP control file</string>
      <key>CFBundleTypeExtensions</key><array><string>ctl</string></array>
      <key>CFBundleTypeRole</key><string>Editor</string>
      <key>LSHandlerRank</key><string>Owner</string>
    </dict>
  </array>
</dict>
</plist>
PLIST

plutil -lint "$APP/Contents/Info.plist" >/dev/null

echo ">> ad-hoc code-signing"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP" && echo "   signature OK"

echo ">> building dmg"
DMG="$DIST/$APP_NAME $APP_VERSION.dmg"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGE"

echo ""
echo "Built:"
echo "  $PWD/$APP"
echo "  $PWD/$DMG"
echo ""
echo "Install: open the .dmg and drag \"$APP_NAME\" to Applications."
echo "Runtime dep: bpp-lint on PATH (brew install bpp/tap/bpp-lint)."
