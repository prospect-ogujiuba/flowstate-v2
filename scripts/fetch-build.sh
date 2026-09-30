#!/usr/bin/env bash
# Download the plugin builds from the latest green `plugin` CI run.
#
#   scripts/fetch-build.sh [windows|macos|all] [--branch B] [--run ID] [--out DIR]
#
# Defaults: all targets, the current branch, and on WSL the Windows Downloads folder
# (so the Windows build is ready to install), elsewhere dist/builds.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

usage() { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

target=all branch="" run="" out=""
while [ $# -gt 0 ]; do
  case "$1" in
    windows|macos|all) target=$1 ;;
    --branch) branch=${2:?--branch needs a value}; shift ;;
    --run) run=${2:?--run needs a value}; shift ;;
    --out) out=${2:?--out needs a value}; shift ;;
    -h|--help) usage ;;
    *) echo "unknown argument: $1" >&2; usage 1 ;;
  esac
  shift
done

need gh "GitHub CLI, https://cli.github.com, then 'gh auth login'"
branch=${branch:-$(current_branch)}
if [ -z "$out" ]; then
  if is_wsl; then out=$(windows_downloads); else out="$repo_root/dist/builds"; fi
fi
mkdir -p "$out"
run=$(resolve_run "$branch" "$run")

case "$target" in
  windows) names=(flowstate-windows-x64) ;;
  macos) names=(flowstate-macos-universal) ;;
  all) names=(flowstate-windows-x64 flowstate-macos-universal) ;;
esac

for name in "${names[@]}"; do
  dir="$out/$name"
  rm -rf "$dir"   # gh won't overwrite files from an earlier download
  gh run download "$run" -n "$name" -D "$dir"
  if [ "$name" = flowstate-windows-x64 ]; then
    cp "$repo_root"/scripts/windows/*.ps1 "$dir/"
  fi
  echo "$name ($(cat "$dir/BUILD_ID")) -> $dir"
done

if [[ " ${names[*]} " == *" flowstate-windows-x64 "* ]]; then
  cat <<'MSG'

Windows: in PowerShell, in that folder:
  powershell -ExecutionPolicy Bypass -File .\install.ps1            # install the VST3s (asks for admin)
  powershell -ExecutionPolicy Bypass -File .\gallery.ps1            # component gallery in the Standalone
  powershell -ExecutionPolicy Bypass -File .\gallery.ps1 -Daw "C:\path\to\daw.exe"
MSG
fi
if [[ " ${names[*]} " == *" flowstate-macos-universal "* ]]; then
  echo; echo "macOS: for testers, run scripts/pack-macos-testers.sh instead; it adds an installer and a README."
fi
