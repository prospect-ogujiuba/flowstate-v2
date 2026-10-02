# Shared helpers for the scripts in this folder. Source it; don't run it.

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

die() { echo "error: $*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "needs '$1' ($2)"; }

current_branch() { git -C "$repo_root" rev-parse --abbrev-ref HEAD; }

is_wsl() { grep -qi microsoft /proc/version 2>/dev/null; }

# The Windows user's Downloads folder, as a WSL path.
windows_downloads() {
  local profile
  profile=$(cd /mnt/c && cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')
  [ -n "$profile" ] || die "couldn't find the Windows user profile; pass --out"
  echo "$(wslpath "$profile")/Downloads"
}

# resolve_run <branch> <run id or empty>: prints the run id of the given run, or of the latest
# successful on-demand `plugin` run on the branch (pushes build Linux only, so only on-demand runs
# carry the macOS and Windows artifacts). Describes the run on stderr.
resolve_run() {
  local branch=$1 run=$2
  if [ -z "$run" ]; then
    run=$(gh run list --workflow plugin.yml --branch "$branch" --status success --event workflow_dispatch \
      -L1 --json databaseId -q '.[0].databaseId // empty')
    [ -n "$run" ] || die "no green on-demand 'plugin' run on branch '$branch' (start one: npm run ci:full, then npm run ci:status -- --watch)"
  fi
  gh run view "$run" --json displayTitle,headBranch,createdAt,url \
    -q '"Run \(.url)\n  \(.headBranch): \(.displayTitle) (\(.createdAt))"' >&2
  echo "$run"
}
