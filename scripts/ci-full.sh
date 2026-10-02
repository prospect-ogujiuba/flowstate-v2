#!/usr/bin/env bash
# Run the ci and plugin workflows on a branch on demand, e.g. when the last push only touched docs
# (which skip CI) and you want a fresh build.
#
#   scripts/ci-full.sh [--branch B]
#
# Default: the current branch, which must be pushed. Follow it with `npm run ci:status -- --watch`.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

usage() { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

branch=""
while [ $# -gt 0 ]; do
  case "$1" in
    --branch) branch=${2:?--branch needs a value}; shift ;;
    -h|--help) usage ;;
    *) echo "unknown argument: $1" >&2; usage 1 ;;
  esac
  shift
done

need gh "GitHub CLI, https://cli.github.com, then 'gh auth login'"
branch=${branch:-$(current_branch)}

gh workflow run ci.yml --ref "$branch"
gh workflow run plugin.yml --ref "$branch"
echo "Started ci and plugin on '$branch' with macOS and Windows. Follow: npm run ci:status -- --watch"
