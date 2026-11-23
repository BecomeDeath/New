#!/usr/bin/env bash
# Builds the macOS installer, PromptMIDI-macOS.pkg, from a staged build (Packaging/stage.sh).
#
#   Packaging/macOS/make_pkg.sh <stage dir> <version> <output .pkg>
#
# The installer has a choice per part (VST3, AU, VST2 if the build has one, the app, the ReadMe;
# the instrument sounds are always installed) and, under Customize, a Location for each.
#
# Optional, for an installer macOS opens without a warning:
#   PKG_SIGN_IDENTITY="Developer ID Installer: ..."   signs the package
#   NOTARY_PROFILE=<keychain profile>                  notarizes and staples it (xcrun notarytool)
# (Stage with MACOS_SIGN_IDENTITY="Developer ID Application: ..." so the bundles are signed too.)
set -euo pipefail

STAGE=$(cd "${1:?stage dir}" && pwd)
VERSION=${2:?version}
OUT=${3:?output .pkg}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ART="$HERE/../Art"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkgs" "$WORK/resources" "$WORK/scripts"

VST3_DIR="/Library/Audio/Plug-Ins/VST3"
AU_DIR="/Library/Audio/Plug-Ins/Components"
VST2_DIR="/Library/Audio/Plug-Ins/VST"
APP_DIR="/Applications"
SUPPORT_DIR="/Library/Application Support/PromptMIDI"

# A preinstall script that removes an older copy of a bundle first, so nothing stale is left
# inside its signature. $2 is where the choice installs (the Location the user picked).
remove_old() {
    local dir="$WORK/scripts/$1"
    mkdir -p "$dir"
    printf '#!/bin/sh\nif [ -n "$2" ] && [ -d "$2/%s" ]; then rm -rf "$2/%s"; fi\nexit 0\n' "$2" "$2" > "$dir/preinstall"
    chmod +x "$dir/preinstall"
}

# component <id> <payload root> <install location> [scripts dir]
component() {
    local id=$1 root=$2 location=$3 scripts=${4:-}
    local args=(--root "$root" --identifier "com.promptmidi.pkg.$id" --version "$VERSION" --install-location "$location")

    local bundles=0 f
    for f in "$root"/*.vst3 "$root"/*.component "$root"/*.vst "$root"/*.app; do
        if [ -e "$f" ]; then bundles=1; fi
    done

    if [ $bundles = 1 ]; then
        # Install where chosen, even if macOS knows of a copy of the bundle somewhere else.
        local plist="$WORK/$id.plist" i=0
        pkgbuild --analyze --root "$root" "$plist" > /dev/null
        while plutil -extract "$i" xml1 -o /dev/null "$plist" 2> /dev/null; do
            plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
            i=$((i + 1))
        done
        args+=(--component-plist "$plist")
    fi

    if [ -n "$scripts" ]; then args+=(--scripts "$scripts"); fi
    pkgbuild "${args[@]}" "$WORK/pkgs/$id.pkg"
}

payload() { mkdir -p "$WORK/root-$1"; echo "$WORK/root-$1"; }

# --- Components ----------------------------------------------------------------------------------
r=$(payload vst3); ditto "$STAGE/VST3/PromptMIDI.vst3" "$r/PromptMIDI.vst3"
remove_old vst3 PromptMIDI.vst3
component vst3 "$r" "$VST3_DIR" "$WORK/scripts/vst3"

r=$(payload au); ditto "$STAGE/AU/PromptMIDI.component" "$r/PromptMIDI.component"
remove_old au PromptMIDI.component
# Make Logic and GarageBand see the new Audio Unit without a restart.
printf '#!/bin/sh\nkillall -9 AudioComponentRegistrar > /dev/null 2>&1\nexit 0\n' > "$WORK/scripts/au/postinstall"
chmod +x "$WORK/scripts/au/postinstall"
component au "$r" "$AU_DIR" "$WORK/scripts/au"

HAVE_VST2=0
if [ -d "$STAGE/VST2/PromptMIDI.vst" ]; then
    HAVE_VST2=1
    r=$(payload vst2); ditto "$STAGE/VST2/PromptMIDI.vst" "$r/PromptMIDI.vst"
    remove_old vst2 PromptMIDI.vst
    component vst2 "$r" "$VST2_DIR" "$WORK/scripts/vst2"
fi

r=$(payload app); ditto "$STAGE/Standalone/PromptMIDI.app" "$r/PromptMIDI.app"
remove_old app PromptMIDI.app
component app "$r" "$APP_DIR" "$WORK/scripts/app"

r=$(payload sounds); cp "$STAGE"/Sounds/* "$r/"
component sounds "$r" "$SUPPORT_DIR/Sounds"

r=$(payload readme); cp "$STAGE/Docs/ReadMe.txt" "$r/"; ditto "$STAGE/Docs/Licences" "$r/Licences"
component readme "$r" "$SUPPORT_DIR"

# --- Installer pages -----------------------------------------------------------------------------
cp "$ART/macos-background.png" "$WORK/resources/background.png"
cp "$ART/macos-background-dark.png" "$WORK/resources/background-dark.png"
for page in welcome conclusion; do
    sed "s/@VERSION@/$VERSION/g" "$HERE/Resources/$page.rtf" > "$WORK/resources/$page.rtf"
done

choice() { # id title description location
    printf '    <choice id="%s" title="%s" description="%s" customLocation="%s">\n        <pkg-ref id="com.promptmidi.pkg.%s"/>\n    </choice>\n' "$1" "$2" "$3" "$4" "$1"
}

{
    cat <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>PromptMIDI $VERSION</title>
    <organization>com.promptmidi</organization>
    <domains enable_localSystem="true"/>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    <background file="background.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>
    <background-darkAqua file="background-dark.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>
    <welcome file="welcome.rtf" mime-type="text/rtf"/>
    <conclusion file="conclusion.rtf" mime-type="text/rtf"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
EOF
    if [ $HAVE_VST2 = 1 ]; then echo '        <line choice="vst2"/>'; fi
    cat <<EOF
        <line choice="app"/>
        <line choice="readme"/>
        <line choice="sounds"/>
    </choices-outline>
EOF
    choice vst3 "VST3 plug-in" "For most DAWs: Ableton Live, Cubase, Studio One, Reaper, Bitwig, FL Studio and more." "$VST3_DIR"
    choice au "Audio Unit" "For Logic Pro, GarageBand and other Audio Unit hosts." "$AU_DIR"
    if [ $HAVE_VST2 = 1 ]; then
        choice vst2 "VST2 plug-in" "For older DAWs that don't load VST3." "$VST2_DIR"
    fi
    choice app "Standalone app" "Use PromptMIDI on its own, without a DAW." "$APP_DIR"
    choice readme "ReadMe" "How to get started, where everything went, and the licences." "$SUPPORT_DIR"
    cat <<EOF
    <choice id="sounds" title="Instrument sounds" description="The instruments the preview plays (GeneralUser GS). Every format shares this one copy, so it is always installed." enabled="false" selected="true">
        <pkg-ref id="com.promptmidi.pkg.sounds"/>
    </choice>
EOF
    for id in vst3 au vst2 app readme sounds; do
        if [ -f "$WORK/pkgs/$id.pkg" ]; then
            echo "    <pkg-ref id=\"com.promptmidi.pkg.$id\" version=\"$VERSION\" onConclusion=\"none\">$id.pkg</pkg-ref>"
        fi
    done
    echo '</installer-gui-script>'
} > "$WORK/distribution.xml"

sign=()
if [ -n "${PKG_SIGN_IDENTITY:-}" ]; then sign=(--sign "$PKG_SIGN_IDENTITY"); fi

mkdir -p "$(dirname "$OUT")"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
             --package-path "$WORK/pkgs" ${sign[@]+"${sign[@]}"} "$OUT"

if [ -n "${NOTARY_PROFILE:-}" ]; then
    xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$OUT"
fi

echo "built $OUT"
