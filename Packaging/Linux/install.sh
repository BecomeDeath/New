#!/usr/bin/env bash
# PromptMIDI installer for Linux.
#
#   ./install.sh            asks what to install and where
#   ./install.sh --yes      installs everything in the standard places, no questions
#   sudo ./install.sh       installs for every user (under /usr/local)
#
# Options: --yes, --vst3-dir DIR, --vst2-dir DIR, --app-dir DIR, --no-vst3, --no-vst2, --no-app,
#          --no-readme, --help
# The instrument sounds are always installed (once, shared by every format), next to an
# uninstall.sh that removes everything again.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
VERSION=$(cat "$HERE/version.txt" 2> /dev/null || echo "?")

if [ "$(id -u)" = 0 ]; then
    VST3_DIR=/usr/local/lib/vst3
    VST2_DIR=/usr/local/lib/vst
    APP_DIR=/usr/local/bin
    SHARE=/usr/local/share
else
    VST3_DIR="$HOME/.vst3"
    VST2_DIR="$HOME/.vst"
    APP_DIR="$HOME/.local/bin"
    SHARE="${XDG_DATA_HOME:-$HOME/.local/share}"
fi
DATA_DIR="$SHARE/PromptMIDI"

HAVE_VST2=0
if [ -f "$HERE/VST2/PromptMIDI.so" ]; then HAVE_VST2=1; fi
WANT_VST3=1 WANT_VST2=$HAVE_VST2 WANT_APP=1 WANT_README=1 ASK=1

usage() { sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0; }

while [ $# -gt 0 ]; do
    case "$1" in
        -y|--yes) ASK=0 ;;
        --vst3-dir) VST3_DIR=${2:?}; shift ;;
        --vst2-dir) VST2_DIR=${2:?}; shift ;;
        --app-dir) APP_DIR=${2:?}; shift ;;
        --no-vst3) WANT_VST3=0 ;;
        --no-vst2) WANT_VST2=0 ;;
        --no-app) WANT_APP=0 ;;
        --no-readme) WANT_README=0 ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

if [ ! -t 0 ]; then ASK=0; fi

# --- Looks ---------------------------------------------------------------------------------------
if [ -t 1 ]; then
    V=$'\e[38;2;155;144;255m' D=$'\e[38;2;115;124;138m' B=$'\e[1m' G=$'\e[38;2;64;201;139m' Y=$'\e[38;2;244;181;75m' R=$'\e[0m'
else
    V='' D='' B='' G='' Y='' R=''
fi

say()  { printf '%s\n' "$*"; }
step() { printf '  %s%s%s %s\n' "$V" "▸" "$R" "$*"; }
done_() { printf '  %s✓%s %s\n' "$G" "$R" "$*"; }

ask_yes() { # question default(1/0)
    local answer hint="[Y/n]"
    if [ "$2" = 0 ]; then hint="[y/N]"; fi
    if [ $ASK = 0 ]; then return $((1 - $2)); fi
    read -r -p "  $1 $D$hint$R " answer || true
    case "${answer:-}" in
        [Yy]*) return 0 ;;
        [Nn]*) return 1 ;;
        *) return $((1 - $2)) ;;
    esac
}

ask_dir() { # variable prompt
    local current=${!1} answer
    if [ $ASK = 0 ]; then return; fi
    read -r -p "    $2 $D[$current]$R " answer || true
    answer=${answer:-$current}
    answer=${answer/#\~/$HOME}
    printf -v "$1" '%s' "$answer"
}

say ""
say "  ${V}${B}♪  PromptMIDI${R} ${D}$VERSION${R}"
say "  ${D}Describe the music in words, get multi-track MIDI.${R}"
say ""

# --- What and where ------------------------------------------------------------------------------
if ask_yes "Install the VST3 plug-in?" $WANT_VST3; then WANT_VST3=1; ask_dir VST3_DIR "VST3 folder:"; else WANT_VST3=0; fi
if [ $HAVE_VST2 = 1 ]; then
    if ask_yes "Install the VST2 plug-in?" $WANT_VST2; then WANT_VST2=1; ask_dir VST2_DIR "VST2 folder:"; else WANT_VST2=0; fi
fi
if ask_yes "Install the standalone app?" $WANT_APP; then WANT_APP=1; ask_dir APP_DIR "Folder for the promptmidi command:"; else WANT_APP=0; fi
if ask_yes "Install the ReadMe?" $WANT_README; then WANT_README=1; else WANT_README=0; fi

if [ $WANT_VST3 = 0 ] && [ $WANT_VST2 = 0 ] && [ $WANT_APP = 0 ]; then
    say "  Nothing to install."
    exit 0
fi

say ""
MANIFEST="$DATA_DIR/installed-files"
mkdir -p "$DATA_DIR"
: > "$MANIFEST.new"
installed() { printf '%s\n' "$1" >> "$MANIFEST.new"; }

# --- Install -------------------------------------------------------------------------------------
if [ $WANT_VST3 = 1 ]; then
    mkdir -p "$VST3_DIR"
    rm -rf "$VST3_DIR/PromptMIDI.vst3"
    cp -R "$HERE/VST3/PromptMIDI.vst3" "$VST3_DIR/"
    installed "$VST3_DIR/PromptMIDI.vst3"
    done_ "VST3 plug-in   $D$VST3_DIR/PromptMIDI.vst3$R"
fi

if [ $WANT_VST2 = 1 ]; then
    mkdir -p "$VST2_DIR"
    cp "$HERE/VST2/PromptMIDI.so" "$VST2_DIR/PromptMIDI.so"
    installed "$VST2_DIR/PromptMIDI.so"
    done_ "VST2 plug-in   $D$VST2_DIR/PromptMIDI.so$R"
fi

if [ $WANT_APP = 1 ]; then
    mkdir -p "$APP_DIR" "$SHARE/applications" "$SHARE/icons/hicolor/256x256/apps"
    install -m 755 "$HERE/Standalone/PromptMIDI" "$APP_DIR/promptmidi"
    install -m 644 "$HERE/promptmidi.png" "$SHARE/icons/hicolor/256x256/apps/promptmidi.png"
    cat > "$SHARE/applications/promptmidi.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=PromptMIDI
Comment=Describe the music in words, get multi-track MIDI
Exec=$APP_DIR/promptmidi
Icon=promptmidi
Categories=AudioVideo;Audio;Music;Midi;
Terminal=false
EOF
    installed "$APP_DIR/promptmidi"
    installed "$SHARE/applications/promptmidi.desktop"
    installed "$SHARE/icons/hicolor/256x256/apps/promptmidi.png"
    done_ "App            $D$APP_DIR/promptmidi (and in your applications menu)$R"
fi

mkdir -p "$DATA_DIR/Sounds"
cp "$HERE"/Sounds/* "$DATA_DIR/Sounds/"
done_ "Sounds         $D$DATA_DIR/Sounds$R"

rm -rf "$DATA_DIR/Licences" "$DATA_DIR/ReadMe.txt"
cp -R "$HERE/Docs/Licences" "$DATA_DIR/Licences"
if [ $WANT_README = 1 ]; then
    cp "$HERE/Docs/ReadMe.txt" "$DATA_DIR/ReadMe.txt"
    done_ "ReadMe         $D$DATA_DIR/ReadMe.txt$R"
fi

# The uninstaller removes what this run installed, then the PromptMIDI data folder itself.
cat > "$DATA_DIR/uninstall.sh" <<EOF
#!/bin/sh
# Removes PromptMIDI, as installed by install.sh. Your settings stay in ~/.config/PromptMIDI.
set -e
DATA_DIR='$DATA_DIR'
if [ -f "\$DATA_DIR/installed-files" ]; then
    while IFS= read -r path; do
        case "\$path" in
            */PromptMIDI.vst3|*/PromptMIDI.so|*/promptmidi|*/promptmidi.desktop|*/promptmidi.png) rm -rf -- "\$path" ;;
        esac
    done < "\$DATA_DIR/installed-files"
fi
case "\$DATA_DIR" in
    */PromptMIDI) rm -rf -- "\$DATA_DIR" ;;
esac
command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database -q '$SHARE/applications' 2> /dev/null || true
echo "PromptMIDI has been removed."
EOF
chmod 755 "$DATA_DIR/uninstall.sh"

# Keep entries from an earlier install that this one didn't replace, so uninstall.sh finds them.
if [ -f "$MANIFEST" ]; then
    while IFS= read -r path; do
        if [ -e "$path" ] && ! grep -qxF "$path" "$MANIFEST.new"; then installed "$path"; fi
    done < "$MANIFEST"
fi
mv "$MANIFEST.new" "$MANIFEST"

if command -v update-desktop-database > /dev/null 2>&1; then
    update-desktop-database -q "$SHARE/applications" 2> /dev/null || true
fi

# --- Libraries -----------------------------------------------------------------------------------
# The binaries need a few common system libraries. Report any that are missing.
binary="$HERE/Standalone/PromptMIDI"
if [ ! -f "$binary" ]; then binary=$(ls "$HERE"/VST3/PromptMIDI.vst3/Contents/*/PromptMIDI.so 2> /dev/null | head -n 1); fi
missing=""
if [ -n "$binary" ] && command -v ldd > /dev/null 2>&1; then
    missing=$(ldd "$binary" 2> /dev/null | awk '/not found/ { print $1 }' | tr '\n' ' ')
fi

say ""
if [ -n "$missing" ]; then
    say "  ${Y}Some libraries PromptMIDI needs are missing:${R} $missing"
    if command -v apt-get > /dev/null 2>&1; then
        say "  Install them with:  sudo apt install libcurl4 libasound2 libfreetype6 libfontconfig1"
    elif command -v dnf > /dev/null 2>&1; then
        say "  Install them with:  sudo dnf install libcurl alsa-lib freetype fontconfig"
    elif command -v pacman > /dev/null 2>&1; then
        say "  Install them with:  sudo pacman -S curl alsa-lib freetype2 fontconfig"
    fi
    say ""
fi

say "  ${B}PromptMIDI is installed.${R} Rescan plug-ins in your DAW, or run ${V}promptmidi${R}."
if [ $WANT_APP = 1 ]; then
    case ":$PATH:" in
        *":$APP_DIR:"*) ;;
        *) say "  ${D}($APP_DIR isn't on your PATH; open PromptMIDI from the applications menu instead.)${R}" ;;
    esac
fi
say "  ${D}To remove it: $DATA_DIR/uninstall.sh${R}"
say ""
