#!/bin/bash
# Installs (or removes) the Flowstate plug-ins from this folder, for the current user.
# In Terminal, type "bash " (with the space), drag this file onto the window, press Return.
# To remove Flowstate again: the same, with " --uninstall" after the file name.
# The hosted agent service: a service.json in this folder is installed for you. Or pass it yourself:
#   bash install.sh --service https://flowstate.example.com --token fst_...
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
vst3="$HOME/Library/Audio/Plug-Ins/VST3"
au="$HOME/Library/Audio/Plug-Ins/Components"
apps="$HOME/Applications"
bundles=("$vst3/Flowstate.vst3" "$vst3/Flowstate MIDI FX.vst3"
         "$au/Flowstate.component" "$au/Flowstate MIDI FX.component" "$apps/Flowstate.app")
# Where the plug-in looks for the agent service and the tester token (docs/bridge-spec.md).
settings="$HOME/Library/Application Support/Flowstate/service.json"

uninstall=0 service="" token=""
while [ $# -gt 0 ]; do
  case "$1" in
    --uninstall) uninstall=1 ;;
    --service) service=${2:?--service needs a URL}; shift ;;
    --token) token=${2:?--token needs a value}; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ "$uninstall" = 1 ]; then
  for b in "${bundles[@]}" "$settings"; do
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

# The service settings: from the flags, else the service.json that came with this folder.
if [ -n "$service$token" ]; then
  [[ $service =~ ^https://[A-Za-z0-9.-]+(:[0-9]+)?/?$ ]] || { echo "--service must be an https URL" >&2; exit 2; }
  [[ $token =~ ^fst_[A-Za-z0-9_-]+$ ]] || { echo "--token must be the fst_... token you were sent" >&2; exit 2; }
  mkdir -p "$(dirname "$settings")"
  printf '{"url": "%s", "token": "%s"}\n' "${service%/}" "$token" > "$settings"
elif [ -f "$here/service.json" ]; then
  mkdir -p "$(dirname "$settings")"
  cp "$here/service.json" "$settings"
fi
if [ -f "$settings" ]; then
  chmod 600 "$settings"
  echo "  $settings (agent service)"
fi

# Makes Logic and other AU hosts notice the new components without a restart.
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done. Open your DAW and rescan plug-ins; look for Flowstate and Flowstate MIDI FX."
