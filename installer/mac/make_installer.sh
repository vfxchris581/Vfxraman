#!/usr/bin/env bash
# Nam KNoB (formerly VFXAamps) — macOS installer: a DMG with the standalone app (drag to Applications) and
# "Install Nam KNoB Plug-ins.pkg" (VST3 + AU into /Library/Audio/Plug-Ins).
#
#   installer/mac/make_installer.sh <artefacts-dir> <output-dir> [version]
#
# Optional signing / notarisation (all skipped when unset):
#   MACOS_APP_SIGN_ID     "Developer ID Application: Name (TEAMID)"  - signs app + plug-ins (hardened runtime)
#   MACOS_PKG_SIGN_ID     "Developer ID Installer: Name (TEAMID)"    - signs the .pkg
#   MACOS_NOTARY_APPLE_ID / MACOS_NOTARY_TEAM_ID / MACOS_NOTARY_PASSWORD (app-specific password)
#                         - notarises and staples the .pkg and the .dmg
# Without a Developer ID the bundles are ad-hoc signed (required on Apple Silicon); macOS then
# asks the user to confirm the first launch (see the read-me inside the DMG).
set -euo pipefail

ART="${1:?artefacts dir (…/VFXAamps_artefacts/Release)}"
OUT="${2:?output dir}"
VERSION="${3:-1.0.0}"
PRODUCT="Nam KNoB"
FILE_NAME="NamKNoB"
OLD_PRODUCT="VFXAamps"
IDENT="com.viridianaudioworks.vfxaamps.head"
HERE="$(cd "$(dirname "$0")" && pwd)"

APP="$ART/Standalone/$PRODUCT.app"
VST3="$ART/VST3/$PRODUCT.vst3"
AU="$ART/AU/$PRODUCT.component"
for b in "$APP" "$VST3" "$AU"; do [ -d "$b" ] || { echo "missing $b - build Release first"; exit 1; }; done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

# ---- sign bundles --------------------------------------------------------------------
SIGN_ID="${MACOS_APP_SIGN_ID:-}"
sign() {
    if [ -n "$SIGN_ID" ]; then
        codesign --force --deep --options runtime --timestamp --sign "$SIGN_ID" "$1"
    else
        codesign --force --deep --sign - "$1"
    fi
}
ditto "$APP" "$WORK/app/$PRODUCT.app"
ditto "$VST3" "$WORK/vst3/$PRODUCT.vst3"
ditto "$AU" "$WORK/au/$PRODUCT.component"
sign "$WORK/app/$PRODUCT.app"
sign "$WORK/vst3/$PRODUCT.vst3"
sign "$WORK/au/$PRODUCT.component"

# ---- plug-in installer (.pkg) ----------------------------------------------------------
mkdir -p "$WORK/scripts" "$WORK/scripts-vst3"
# The plug-ins used to be called VFXAamps; the old bundles have the same plug-in ID, so remove them.
printf '#!/bin/sh\nrm -rf "/Library/Audio/Plug-Ins/VST3/%s.vst3"\nexit 0\n' "$OLD_PRODUCT" > "$WORK/scripts-vst3/preinstall"
printf '#!/bin/sh\nrm -rf "/Library/Audio/Plug-Ins/Components/%s.component"\nexit 0\n' "$OLD_PRODUCT" > "$WORK/scripts/preinstall"
cat > "$WORK/scripts/postinstall" <<'EOF'
#!/bin/sh
# Make DAWs rescan the Audio Unit right away.
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
EOF
chmod +x "$WORK/scripts/postinstall" "$WORK/scripts/preinstall" "$WORK/scripts-vst3/preinstall"

pkgbuild --root "$WORK/vst3" --identifier "$IDENT.vst3" --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/VST3" --scripts "$WORK/scripts-vst3" "$WORK/vst3.pkg"
pkgbuild --root "$WORK/au" --identifier "$IDENT.au" --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/Components" --scripts "$WORK/scripts" "$WORK/au.pkg"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$PRODUCT $VERSION</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="vst3" title="VST3 plug-in" description="Installs $PRODUCT.vst3 into /Library/Audio/Plug-Ins/VST3">
        <pkg-ref id="$IDENT.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit (AU)" description="Installs $PRODUCT.component into /Library/Audio/Plug-Ins/Components">
        <pkg-ref id="$IDENT.au"/>
    </choice>
    <pkg-ref id="$IDENT.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="$IDENT.au" version="$VERSION">au.pkg</pkg-ref>
</installer-gui-script>
EOF

PKG="$WORK/dmg/Install $PRODUCT Plug-ins.pkg"
mkdir -p "$WORK/dmg"
if [ -n "${MACOS_PKG_SIGN_ID:-}" ]; then
    productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" --sign "$MACOS_PKG_SIGN_ID" --timestamp "$PKG"
else
    productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$PKG"
fi

notarize() {
    if [ -n "${MACOS_NOTARY_APPLE_ID:-}" ] && [ -n "${MACOS_NOTARY_TEAM_ID:-}" ] && [ -n "${MACOS_NOTARY_PASSWORD:-}" ]; then
        xcrun notarytool submit "$1" --apple-id "$MACOS_NOTARY_APPLE_ID" --team-id "$MACOS_NOTARY_TEAM_ID" \
              --password "$MACOS_NOTARY_PASSWORD" --wait
        xcrun stapler staple "$1"
    fi
}
[ -n "${MACOS_PKG_SIGN_ID:-}" ] && notarize "$PKG"

# ---- DMG: app (drag to Applications) + plug-in installer + read-me -----------------------
ditto "$WORK/app/$PRODUCT.app" "$WORK/dmg/$PRODUCT.app"
ln -s /Applications "$WORK/dmg/Applications"
cp "$HERE/README.txt" "$WORK/dmg/Read Me.txt"

DMG="$OUT/$FILE_NAME-$VERSION-macOS.dmg"
rm -f "$DMG"
hdiutil create -volname "$PRODUCT $VERSION" -srcfolder "$WORK/dmg" -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$DMG"
if [ -n "$SIGN_ID" ]; then
    codesign --force --timestamp --sign "$SIGN_ID" "$DMG"
    notarize "$DMG"
fi
echo "Built $DMG"
