#!/bin/bash
# Installs (or removes) the Flowstate plug-ins from this folder, for the current user.
# In Terminal, type "bash " (with the space), drag this file onto the window, press Return.
# To remove Flowstate again: the same, with " --uninstall" after the file name.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
vst3="$HOME/Library/Audio/Plug-Ins/VST3"
au="$HOME/Library/Audio/Plug-Ins/Components"
apps="$HOME/Applications"
bundles=("$vst3/Flowstate.vst3" "$vst3/Flowstate MIDI FX.vst3"
         "$au/Flowstate.component" "$au/Flowstate MIDI FX.component" "$apps/Flowstate.app")

if [ "${1:-}" = "--uninstall" ]; then
  for b in "${bundles[@]}"; do
    [ -e "$b" ] && rm -rf "$b" && echo "Removed $b"
  done
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  echo "Flowstate is removed. Rescan plug-ins in your DAW."
  exit 0
fi

mkdir -p "$vst3" "$au" "$apps"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for z in "$here"/*.zip; do
  ditto -x -k "$z" "$tmp"
done

put() { # put <bundle> <folder>: replace the installed copy and let macOS load it
  local name
  name=$(basename "$1")
  rm -rf "$2/$name"
  mv "$1" "$2/"
  # These test builds aren't notarized yet, so macOS would block them without this.
  xattr -dr com.apple.quarantine "$2/$name" 2>/dev/null || true
  echo "  $2/$name"
}

echo "Installing Flowstate $(cat "$here/BUILD_ID" 2>/dev/null || echo '(unknown build)'):"
for b in "$tmp"/*.vst3; do [ -e "$b" ] && put "$b" "$vst3"; done
for b in "$tmp"/*.component; do [ -e "$b" ] && put "$b" "$au"; done
for b in "$tmp"/*.app; do [ -e "$b" ] && put "$b" "$apps"; done

# Makes Logic and other AU hosts notice the new components without a restart.
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done. Open your DAW and rescan plug-ins; look for Flowstate and Flowstate MIDI FX."
