#!/usr/bin/env bash
# Get the tester zips from the latest green `plugin` CI run (a green run's zips passed every check,
# including the install smoke on a fresh runner), optionally set up for one tester's hosted service.
#
#   scripts/pack-testers.sh [macos|windows|all] [--branch B] [--run ID] [--out DIR]
#                           [--service URL (--tester NAME | --token T)]
#
# Defaults: both platforms, the current branch, output in dist/builds. --service with --tester mints a
# token for that tester (and prints the line for the server's tokens file); --token uses one you have.
# Either puts a service.json in the zip, which the installer installs.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

usage() { sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

target=all branch="" run="" out="$repo_root/dist/builds" service="" tester="" token=""
while [ $# -gt 0 ]; do
  case "$1" in
    macos|windows|all) target=$1 ;;
    --branch) branch=${2:?--branch needs a value}; shift ;;
    --run) run=${2:?--run needs a value}; shift ;;
    --out) out=${2:?--out needs a value}; shift ;;
    --service) service=${2:?--service needs a URL}; shift ;;
    --tester) tester=${2:?--tester needs a name}; shift ;;
    --token) token=${2:?--token needs a value}; shift ;;
    -h|--help) usage ;;
    *) echo "unknown argument: $1" >&2; usage 1 ;;
  esac
  shift
done

if [ -n "$service$tester$token" ]; then
  [[ $service =~ ^https://[A-Za-z0-9.-]+(:[0-9]+)?/?$ ]] || die "--service must be the https URL of the hosted service"
  if [ -n "$tester" ] && [ -n "$token" ]; then die "pass --tester or --token, not both"; fi
  if [ -z "$tester$token" ]; then die "--service needs --tester NAME (mints a token) or --token"; fi
  [ -z "$token" ] || [[ $token =~ ^fst_[A-Za-z0-9_-]+$ ]] || die "--token must be a tester token (fst_...)"
fi
need gh "GitHub CLI, https://cli.github.com, then 'gh auth login'"
need python3 "zips are made with python3 -m zipfile"
branch=${branch:-$(current_branch)}
run=$(resolve_run "$branch" "$run")

tokens_line=""
if [ -n "$tester" ]; then
  minted=$(npm run -s -w cloud token -- "$tester")
  token=$(grep -oE 'fst_[A-Za-z0-9_-]+' <<<"$minted" | head -1)
  tokens_line=$(grep -E "^  $tester [0-9a-f]{64}\$" <<<"$minted" | sed 's/^  //')
  [ -n "$token" ] && [ -n "$tokens_line" ] || die "couldn't mint a token (npm run -w cloud token said: $minted)"
fi

case "$target" in
  all) platforms=(macos windows) ;;
  *) platforms=("$target") ;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"
out=$(cd "$out" && pwd)
zips=()
for p in "${platforms[@]}"; do
  gh run download "$run" -n "flowstate-tester-$p" -D "$work/$p" ||
    die "run $run has no flowstate-tester-$p (runs before the tester package existed don't; start one with npm run ci:full)"
  zip=$(ls "$work/$p"/flowstate-"$p"-*.zip)
  name=$(basename "$zip" .zip)
  if [ -n "$token" ]; then
    (cd "$work/$p" && python3 -m zipfile -e "$zip" . && rm "$zip")
    printf '{"url": "%s", "token": "%s"}\n' "${service%/}" "$token" > "$work/$p/$name/service.json"
    dest="$out/$name${tester:+-$tester}.zip"
    rm -f "$dest"
    (cd "$work/$p" && python3 -m zipfile -c "$dest" "$name")
  else
    dest="$out/$name.zip"
    cp "$zip" "$dest"
  fi
  zips+=("$dest")
done

echo
for z in "${zips[@]}"; do
  echo "Tester zip: $z"
  is_wsl && echo "  Windows path: $(wslpath -w "$z")"
done
echo "Send the zip as it is: its README.txt has the install steps and the checklist, FEEDBACK.txt the form."
if [ -n "$token" ]; then
  echo "It holds ${tester:-this tester}'s token: send it to them privately, and to no one else."
fi
if [ -n "$tokens_line" ]; then
  echo
  echo "Add this line to the server's tokens file, then restart the service (deploy/README.md, Testers):"
  echo "  $tokens_line"
fi
if [ -z "$service" ]; then
  echo "No --service: the build looks for the agent service on this machine (127.0.0.1:8787), which a tester won't have."
fi
