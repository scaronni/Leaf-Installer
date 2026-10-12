#!/usr/bin/env bash
# Assembles initial-sd-card.zip: everything needed on a blank SD card —
# hekate + Nyx, Atmosphère, Ultrahand, sys-patch, emuiibo, the RCM payloads
# (fusee, modchip toolboxes, Lockpick_RCM) and the Leaf Installer release zip
# built by `make release`.
#
# Usage: initial-sd-card.sh <Leaf-Installer.zip> <output.zip> [--latest]
#
# Third-party components are pinned to the tags below; bump them when tagging
# a new Leaf release. Only the tag is pinned — the asset is looked up in that
# release by name pattern, so no commit hashes (Atmosphère) need updating.
# --latest ignores the pins and takes each project's latest release instead
# (handy for checking what to bump to). GITHUB_TOKEN, when set, is used for
# the API lookups to avoid the anonymous rate limit (60 requests/hour).
set -euo pipefail

#   name          repo                          tag       asset pattern
COMPONENTS=(
    "hekate        CTCaer/hekate                 v6.5.4    hekate_ctcaer_*_Nyx_*.zip"
    "atmosphere    Atmosphere-NX/Atmosphere      1.12.0    atmosphere-*.zip"
    "ultrahand     ppkantorski/Ultrahand-Overlay v2.5.3    sdout.zip"
    "sys-patch     impeeza/sys-patch             v1.6.2.3  sys-patch-*.zip"
    "emuiibo       XorTroll/emuiibo              1.1.3     emuiibo.zip"
)
# Extra payloads for bootloader/payloads/ (same columns).
PAYLOADS=(
    "fusee         Atmosphere-NX/Atmosphere      1.12.0    fusee.bin"
    "hwfly         hwfly-nx/hwfly-toolbox        1.1.1     hwfly_toolbox.bin"
    "lockpick      Kofysh/Lockpick_RCM           v1.9.21   Lockpick_RCM.bin"
)
# Not a GitHub release asset: fetched from the repository as-is.
PICOFLY_TOOLBOX_URL="https://github.com/Ansem-SoD/Picofly/raw/main/Firmwares/picofly_toolbox_0.2.bin"

LEAF_ZIP=$(realpath "$1")
OUT_ZIP=$(realpath -m "$2")
LATEST=0
[ "${3:-}" = "--latest" ] && LATEST=1

WORK=$(cd "$(dirname "$0")/.." && pwd)/build/initial-sd-card
CACHE=$WORK/downloads
SD=$WORK/sd
rm -rf "$SD"
mkdir -p "$CACHE" "$SD"

# Prints the download URL of the asset matching <pattern> in <repo>'s release
# <tag> (or its latest release with --latest).
asset_url() {
    local repo=$1 tag=$2 pattern=$3 api
    if [ "$LATEST" = 1 ]; then
        api="https://api.github.com/repos/$repo/releases/latest"
    else
        api="https://api.github.com/repos/$repo/releases/tags/$tag"
    fi
    curl -fsSL ${GITHUB_TOKEN:+-H "Authorization: Bearer $GITHUB_TOKEN"} "$api" |
        python3 -c '
import fnmatch, json, sys
release = json.load(sys.stdin)
matches = [a for a in release["assets"] if fnmatch.fnmatch(a["name"], sys.argv[1])]
if len(matches) != 1:
    sys.exit("%s %s: expected one asset matching %s, found %s"
             % (sys.argv[2], release["tag_name"], sys.argv[1], [a["name"] for a in matches]))
print(release["tag_name"], matches[0]["browser_download_url"])
' "$pattern" "$repo"
}

# Downloads <url> into the cache (reused across runs) and prints its path.
fetch() {
    local url=$1 file="$CACHE/$(basename "$1")"
    if [ ! -s "$file" ]; then
        curl -fsSL -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "$file"
}

for entry in "${COMPONENTS[@]}"; do
    read -r name repo tag pattern <<< "$entry"
    info=$(asset_url "$repo" "$tag" "$pattern")
    read -r resolved url <<< "$info"
    echo "  $name $resolved"
    zip=$(fetch "$url")
    unzip -qo "$zip" -d "$SD"
done

# emuiibo (and possibly others) ship their SD layout under SdOut/.
if [ -d "$SD/SdOut" ]; then
    cp -a "$SD/SdOut/." "$SD/"
    rm -rf "$SD/SdOut"
fi

# Leaf last, so its hekate_ipl.ini (with the [Leaf Update] entry) wins.
echo "  leaf-installer $(basename "$LEAF_ZIP")"
unzip -qo "$LEAF_ZIP" -d "$SD"

mkdir -p "$SD/bootloader/payloads"
for entry in "${PAYLOADS[@]}"; do
    read -r name repo tag pattern <<< "$entry"
    info=$(asset_url "$repo" "$tag" "$pattern")
    read -r resolved url <<< "$info"
    echo "  $name $resolved"
    bin=$(fetch "$url")
    cp "$bin" "$SD/bootloader/payloads/"
done
echo "  picofly toolbox"
bin=$(fetch "$PICOFLY_TOOLBOX_URL")
cp "$bin" "$SD/bootloader/payloads/"

# Picofly boots /payload.bin: make it hekate.
mv "$SD"/hekate_ctcaer_*.bin "$SD/payload.bin"

rm -f "$OUT_ZIP"
(cd "$SD" && zip -qr9 "$OUT_ZIP" .)
echo "Built $OUT_ZIP — $(wc -c < "$OUT_ZIP") bytes"
