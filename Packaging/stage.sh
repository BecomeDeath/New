#!/usr/bin/env bash
# Gathers a Release build into the layout every installer packs:
#
#   <out>/VST3/PromptMIDI.vst3        the plug-in bundles, without the instrument sounds
#   <out>/VST2/...                    only if the build has a VST2 (PROMPTMIDI_VST2_SDK)
#   <out>/AU/PromptMIDI.component     macOS
#   <out>/Standalone/...              PromptMIDI.exe / PromptMIDI.app / PromptMIDI
#   <out>/Sounds/                     the instrument sounds, installed once for every format
#   <out>/Docs/ReadMe.txt, Licences/  ReadMe (with the version filled in) and licences
#   <out>/version.txt
#
#   Packaging/stage.sh <artefacts dir, e.g. build/PromptMIDI_artefacts/Release> <out dir> <version>
#
# On macOS the bundles are signed again after the sounds are taken out: with
# $MACOS_SIGN_IDENTITY ("Developer ID Application: ...") if set, otherwise ad hoc.
set -euo pipefail

ART=${1:?artefacts dir}
OUT=${2:?output dir}
VERSION=${3:?version}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SF2=GeneralUser-GS.sf2
SF2_LICENCE=GeneralUser-GS-LICENSE.txt

rm -rf "$OUT"
mkdir -p "$OUT"/{VST3,Standalone,Sounds,Docs/Licences}

strip_sounds() { rm -f "$1/$SF2" "$1/$SF2_LICENCE"; }

cp -R "$ART/VST3/PromptMIDI.vst3" "$OUT/VST3/"
strip_sounds "$OUT/VST3/PromptMIDI.vst3/Contents/Resources"

if [ -d "$ART/AU/PromptMIDI.component" ]; then
    mkdir -p "$OUT/AU"
    cp -R "$ART/AU/PromptMIDI.component" "$OUT/AU/"
    strip_sounds "$OUT/AU/PromptMIDI.component/Contents/Resources"
fi

if [ -d "$ART/VST" ]; then
    mkdir -p "$OUT/VST2"
    for f in "$ART/VST"/PromptMIDI.vst "$ART/VST"/PromptMIDI.dll "$ART/VST"/PromptMIDI.so; do
        if [ -e "$f" ]; then cp -R "$f" "$OUT/VST2/"; fi
    done
    if [ -d "$OUT/VST2/PromptMIDI.vst" ]; then strip_sounds "$OUT/VST2/PromptMIDI.vst/Contents/Resources"; fi
fi

if [ -d "$ART/Standalone/PromptMIDI.app" ]; then
    cp -R "$ART/Standalone/PromptMIDI.app" "$OUT/Standalone/"
    strip_sounds "$OUT/Standalone/PromptMIDI.app/Contents/Resources"
fi
if [ -f "$ART/Standalone/PromptMIDI.exe" ]; then cp "$ART/Standalone/PromptMIDI.exe" "$OUT/Standalone/"; fi
if [ -f "$ART/Standalone/PromptMIDI" ]; then cp "$ART/Standalone/PromptMIDI" "$OUT/Standalone/"; fi

cp "$ROOT/Resources/SoundFont/$SF2" "$ROOT/Resources/SoundFont/$SF2_LICENCE" "$OUT/Sounds/"

sed "s/@VERSION@/$VERSION/" "$ROOT/Packaging/ReadMe.txt" > "$OUT/Docs/ReadMe.txt"
cp "$ROOT/Resources/SoundFont/$SF2_LICENCE" "$OUT/Docs/Licences/"
cp "$ROOT/ThirdParty/TinySoundFont/LICENSE" "$OUT/Docs/Licences/TinySoundFont-LICENSE.txt"
cp "$ROOT/Resources/Fonts/Inter-OFL.txt" "$OUT/Docs/Licences/Inter-OFL.txt"
echo "$VERSION" > "$OUT/version.txt"

if [ "$(uname)" = "Darwin" ]; then
    identity=${MACOS_SIGN_IDENTITY:--}
    opts=()
    if [ "$identity" != "-" ]; then opts=(--options runtime --timestamp); fi
    for b in "$OUT"/VST3/*.vst3 "$OUT"/AU/*.component "$OUT"/VST2/*.vst "$OUT"/Standalone/*.app; do
        # (macOS's bash 3.2 treats an empty "${opts[@]}" as unset under set -u.)
        if [ -e "$b" ]; then codesign --force --deep ${opts[@]+"${opts[@]}"} --sign "$identity" "$b"; fi
    done
fi

echo "staged PromptMIDI $VERSION in $OUT:"
(cd "$OUT" && find . -maxdepth 2 | sort)
