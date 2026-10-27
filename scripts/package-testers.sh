#!/usr/bin/env bash
# Assemble the tester zip for one platform from a `plugin` CI build artifact: the bundles, the
# installer, README.txt (with the checklist) and FEEDBACK.txt, with the build ID filled in. CI runs it
# (the zip it smoke-tests is the one testers get); scripts/pack-testers.sh downloads the result.
#
#   scripts/package-testers.sh <macos|windows> <artifact dir> <out dir>
#
# Prints the zip's path. The zip holds no service.json: pack-testers.sh adds one per tester. Needs GNU sed
# (Linux; CI runs it on Ubuntu).
set -euo pipefail
source "$(dirname "$0")/lib.sh"

[ $# -eq 3 ] || { sed -n '6p' "$0" | sed 's/^# //' >&2; exit 2; }
platform=$1 artifact=$2 out=$3
case "$platform" in
  macos) installer=("$repo_root/scripts/macos/install.sh") label=macOS ;;
  windows) installer=("$repo_root/scripts/windows/install.ps1" "$repo_root/scripts/windows/install.cmd") label=Windows ;;
  *) die "platform must be macos or windows" ;;
esac
need python3 "zips are made with python3 -m zipfile"
[ -f "$artifact/BUILD_ID" ] || die "$artifact has no BUILD_ID: is it a 'plugin' CI artifact?"
build_id=$(tr -d '\r\n' < "$artifact/BUILD_ID")
pkg="flowstate-$platform-$build_id"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -R "$artifact" "$work/$pkg"
cp "${installer[@]}" "$work/$pkg/"
fill() { # fill <template> <dest>: the build ID, the platform and the shared checklist
  awk -v checklist="$repo_root/scripts/testers/checklist.txt" '
    $0 == "{{CHECKLIST}}" { while ((getline line < checklist) > 0) print line; next }
    { print }' "$1" |
    sed -e "s/{{BUILD_ID}}/$build_id/g" -e "s/{{PLATFORM}}/$label/g" > "$2"
}
fill "$repo_root/scripts/testers/README-$platform.txt" "$work/$pkg/README.txt"
fill "$repo_root/scripts/testers/FEEDBACK.txt" "$work/$pkg/FEEDBACK.txt"
if [ "$platform" = windows ]; then # Notepad copes with LF, but older ones don't
  sed -i 's/$/\r/' "$work/$pkg/README.txt" "$work/$pkg/FEEDBACK.txt"
fi
if grep -l '{{' "$work/$pkg/README.txt" "$work/$pkg/FEEDBACK.txt"; then die "unfilled placeholder in the files above"; fi

mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/$pkg.zip"
(cd "$work" && python3 -m zipfile -c "$out/$pkg.zip" "$pkg")
echo "$out/$pkg.zip"
