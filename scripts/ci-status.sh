#!/usr/bin/env bash
# Show recent CI runs for a branch, or wait for the latest one to finish.
#
#   scripts/ci-status.sh [--branch B] [--watch]
#
# Default: the current branch. --watch follows the newest `plugin` run until it ends and
# exits non-zero if it failed.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

usage() { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

branch="" watch=0
while [ $# -gt 0 ]; do
  case "$1" in
    --branch) branch=${2:?--branch needs a value}; shift ;;
    --watch) watch=1 ;;
    -h|--help) usage ;;
    *) echo "unknown argument: $1" >&2; usage 1 ;;
  esac
  shift
done

need gh "GitHub CLI, https://cli.github.com, then 'gh auth login'"
branch=${branch:-$(current_branch)}

if [ "$watch" = 1 ]; then
  run=$(gh run list --workflow plugin.yml --branch "$branch" -L1 --json databaseId -q '.[0].databaseId // empty')
  [ -n "$run" ] || die "no 'plugin' run on branch '$branch'"
  gh run watch "$run" --exit-status
else
  gh run list --branch "$branch" -L 10
fi
