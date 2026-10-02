#!/usr/bin/env bash
# Deploy the agent service: runs the `service` workflow on main with deploy=true and follows it. It builds
# and smoke-tests the image, pushes it to GHCR and runs it on the server (deploy/README.md).
#
#   scripts/deploy-service.sh [--no-watch]
#
# Deploys what is pushed to main, not your working tree.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

usage() { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

watch=1
while [ $# -gt 0 ]; do
  case "$1" in
    --no-watch) watch=0 ;;
    -h|--help) usage ;;
    *) echo "unknown argument: $1" >&2; usage 1 ;;
  esac
  shift
done

need gh "GitHub CLI, https://cli.github.com, then 'gh auth login'"
git -C "$repo_root" fetch -q origin main
echo "Deploying origin/main at $(git -C "$repo_root" log -1 --format='%h %s' origin/main)"
gh workflow run service.yml --ref main -f deploy=true
[ "$watch" = 1 ] || { echo "Started. Follow: gh run watch"; exit 0; }
sleep 5
run=$(gh run list --workflow service.yml --branch main --event workflow_dispatch -L1 --json databaseId -q '.[0].databaseId')
gh run watch "$run" --exit-status
