#!/usr/bin/env bash
# Downloads the GeneralUser GS SoundFont (free for any use, see its LICENSE) used by `npm run render`.
# It stays out of git (evals/soundfonts/ is ignored); the checksum pins the version the templates were tuned on.
set -euo pipefail
dir="$(cd "$(dirname "$0")/.." && pwd)/soundfonts"
file="$dir/GeneralUser-GS.sf2"
url="https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/main/GeneralUser-GS.sf2"
sha="9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe"
mkdir -p "$dir"
if [ -f "$file" ] && echo "$sha  $file" | sha256sum -c --quiet 2>/dev/null; then echo "already there: $file"; exit 0; fi
curl -fsSL -o "$file.part" "$url"
echo "$sha  $file.part" | sha256sum -c --quiet || { echo "checksum mismatch for $url (the upstream file changed; check it and update the pin)" >&2; rm -f "$file.part"; exit 1; }
mv "$file.part" "$file"
echo "saved $file"
