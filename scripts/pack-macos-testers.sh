#!/usr/bin/env bash
# Build the zip to send to macOS testers from the latest green `plugin` CI run: the bundles,
# an install script and a README with the build ID.
#
#   scripts/pack-macos-testers.sh [--branch B] [--run ID] [--out DIR] [--service URL --token T]
#
# Defaults: the current branch, output in dist/builds. --service and --token put a service.json for one
# tester in the zip (the hosted service; mint the token with npm run -w cloud token -- <tester>).
set -euo pipefail
source "$(dirname "$0")/lib.sh"

usage() { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

branch="" run="" out="$repo_root/dist/builds" service="" token=""
while [ $# -gt 0 ]; do
  case "$1" in
    --branch) branch=${2:?--branch needs a value}; shift ;;
    --run) run=${2:?--run needs a value}; shift ;;
    --out) out=${2:?--out needs a value}; shift ;;
    --service) service=${2:?--service needs a URL}; shift ;;
    --token) token=${2:?--token needs a value}; shift ;;
    -h|--help) usage ;;
    *) echo "unknown argument: $1" >&2; usage 1 ;;
  esac
  shift
done

if [ -n "$service$token" ]; then
  [[ $service =~ ^https:// ]] || die "--service must be the https URL of the hosted service"
  [[ $token =~ ^fst_[A-Za-z0-9_-]+$ ]] || die "--token must be a tester token (npm run -w cloud token -- <tester>)"
fi
need gh "GitHub CLI, https://cli.github.com, then 'gh auth login'"
need zip "e.g. apt install zip"
branch=${branch:-$(current_branch)}
run=$(resolve_run "$branch" "$run")

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
gh run download "$run" -n flowstate-macos-universal -D "$work/dl"
build_id=$(cat "$work/dl/BUILD_ID")
pkg="flowstate-macos-$build_id"

mkdir -p "$work/$pkg"
mv "$work/dl"/* "$work/$pkg/"
cp "$repo_root/scripts/macos/install.sh" "$work/$pkg/"
sed "s/{{BUILD_ID}}/$build_id/g" "$repo_root/scripts/macos/README.txt" > "$work/$pkg/README.txt"
if [ -n "$token" ]; then
  printf '{"url": "%s", "token": "%s"}\n' "${service%/}" "$token" > "$work/$pkg/service.json"
fi

mkdir -p "$out"
rm -f "$out/$pkg.zip"
(cd "$work" && zip -qr "$out/$pkg.zip" "$pkg")
echo "Tester zip: $out/$pkg.zip"
is_wsl && echo "  Windows path: $(wslpath -w "$out/$pkg.zip")"
echo "Send it with the steps in README.txt (or just the zip; the README is inside)."
if [ -n "$token" ]; then
  echo "It holds this tester's token: send it to them privately, and to no one else."
fi
