#!/usr/bin/env bash
# Fetches everything the station's offline map needs into data/ (git-ignored):
#
#   data/bin/pmtiles                 go-pmtiles CLI, pinned and checksum-verified
#   data/tiles/region.pmtiles        vector tiles around PX4's default home (Zurich)
#   data/assets/fonts/<stack>/*.pbf  label glyphs
#   data/assets/sprites/v4/dark*     map icons for the dark flavour
#
# This script is the only part of the map that touches the network. Once it has
# run, the gateway serves these files and the station needs no internet at all.
#
#   tools/maps/fetch-region.sh
#
# PROTOMAPS_BUILD=YYYYMMDD picks another Protomaps daily build. Protomaps keeps
# roughly the last two months of builds, so the pinned date below eventually
# disappears; the list of builds that still exist is at
#   https://build-metadata.protomaps.dev/builds.json
#
# Needs curl, tar and sha256sum. Linux on x86_64 or arm64 only for now, because
# those are the go-pmtiles builds whose checksums are pinned here.
set -euo pipefail

# go-pmtiles release and the SHA-256 of each Linux archive, as published with
# the release on GitHub. A mismatch stops the script before anything runs.
PMTILES_VERSION=1.31.2
PMTILES_SHA256_x86_64=3ed7dbf4ec2e6dfe5e25b6f70d1ffc932729f93c86db353bf514dd71010a312f
PMTILES_SHA256_arm64=f8bd47e7ea866863489cad588fbaf2f31f42e5821f7a03f009b3769f05801cb1

# Protomaps daily planet build (basemap tiles v4).
PROTOMAPS_BUILD=${PROTOMAPS_BUILD:-20261124}

# Fonts and sprites. basemaps-assets has no tags or releases, so I pin a commit
# and the SHA-256 of its source archive.
ASSETS_COMMIT=028c18f713baecad011301ff7a69acc39bcc2ae7
ASSETS_SHA256=57e40e8c512bd8042d0a3a251f19d0d1c8523ad963c666c3c6643bada4dc92d0
# Every font stack the English style can ask for. Devanagari is only used for
# names in that script, but leaving it out would turn those labels into 404s.
FONT_STACKS=("Noto Sans Regular" "Noto Sans Medium" "Noto Sans Italic" "Noto Sans Devanagari Regular v1")

# About 23 km x 20 km around PX4's default home, 47.397742 N 8.545594 E.
BBOX=8.40,47.30,8.70,47.48
MAXZOOM=15
MAX_BYTES=$((50 * 1000 * 1000))

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
data=$root/data

say() { echo "fetch-region: $*"; }
die() { echo "fetch-region: error: $*" >&2; exit 1; }

for tool in curl tar sha256sum; do
  command -v "$tool" >/dev/null || die "$tool is required"
done
[[ $PROTOMAPS_BUILD =~ ^[0-9]{8}$ ]] || die "PROTOMAPS_BUILD must be YYYYMMDD, got '$PROTOMAPS_BUILD'"

[[ $(uname -s) == Linux ]] || die "only Linux is supported for now (got $(uname -s))"
case "$(uname -m)" in
  x86_64) arch=x86_64; pmtiles_sha=$PMTILES_SHA256_x86_64 ;;
  aarch64 | arm64) arch=arm64; pmtiles_sha=$PMTILES_SHA256_arm64 ;;
  *) die "no pinned go-pmtiles build for $(uname -m)" ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Downloads $1 to $2 unless $2 already has SHA-256 $3, and fails closed if the
# download does not have it either.
fetch_verified() {
  local url=$1 dest=$2 want=$3
  if [[ -f $dest ]] && echo "$want  $dest" | sha256sum --check --status; then
    say "have $(basename "$dest") (checksum ok)"
    return
  fi
  say "downloading $url"
  curl -fsSL --retry 3 --proto '=https' --tlsv1.2 -o "$tmp/download" "$url"
  local got
  got=$(sha256sum "$tmp/download" | cut -d' ' -f1)
  if [[ $got != "$want" ]]; then
    die "checksum mismatch for $url: expected $want, got $got; refusing to use it"
  fi
  mkdir -p "$(dirname "$dest")"
  mv "$tmp/download" "$dest"
  say "checksum ok for $(basename "$dest")"
}

# 1. The pmtiles CLI. The verified archive stays in data/bin so a second run
#    does not download it again; the binary is always unpacked from it.
pmtiles_archive=go-pmtiles_${PMTILES_VERSION}_Linux_${arch}.tar.gz
fetch_verified \
  "https://github.com/protomaps/go-pmtiles/releases/download/v${PMTILES_VERSION}/${pmtiles_archive}" \
  "$data/bin/$pmtiles_archive" "$pmtiles_sha"
tar -xzf "$data/bin/$pmtiles_archive" -C "$tmp" pmtiles
install -m 0755 "$tmp/pmtiles" "$data/bin/pmtiles"
pmtiles=$data/bin/pmtiles

# 2. The region. extract reads only the byte ranges it needs from the planet
#    build, so this downloads tens of megabytes, not the whole planet.
build_url=https://build.protomaps.com/${PROTOMAPS_BUILD}.pmtiles
if ! curl -fsSI --proto '=https' -o /dev/null "$build_url"; then
  die "Protomaps build $PROTOMAPS_BUILD is not available; pick a date from https://build-metadata.protomaps.dev/builds.json and set PROTOMAPS_BUILD"
fi
say "extracting bbox $BBOX up to zoom $MAXZOOM from build $PROTOMAPS_BUILD"
mkdir -p "$data/tiles"
"$pmtiles" extract "$build_url" "$tmp/region.pmtiles" --bbox="$BBOX" --maxzoom="$MAXZOOM" --quiet
if ! "$pmtiles" verify "$tmp/region.pmtiles" >"$tmp/verify.log" 2>&1; then
  cat "$tmp/verify.log" >&2
  die "the extracted archive does not verify"
fi
size=$(stat -c %s "$tmp/region.pmtiles")
if (( size > MAX_BYTES )); then
  die "region.pmtiles is $size bytes, over the $MAX_BYTES byte budget; lower MAXZOOM"
fi
mv "$tmp/region.pmtiles" "$data/tiles/region.pmtiles"
say "wrote data/tiles/region.pmtiles ($((size / 1000)) kB, maxzoom $MAXZOOM)"

# 3. Fonts and sprites.
assets_archive=basemaps-assets-${ASSETS_COMMIT}.tar.gz
fetch_verified \
  "https://github.com/protomaps/basemaps-assets/archive/${ASSETS_COMMIT}.tar.gz" \
  "$data/bin/$assets_archive" "$ASSETS_SHA256"
tar -xzf "$data/bin/$assets_archive" -C "$tmp"
src=$tmp/basemaps-assets-$ASSETS_COMMIT
rm -rf "$data/assets/fonts" "$data/assets/sprites"
mkdir -p "$data/assets/fonts" "$data/assets/sprites/v4"
for stack in "${FONT_STACKS[@]}"; do
  [[ -d $src/fonts/$stack ]] || die "basemaps-assets has no font stack '$stack'"
  cp -R "$src/fonts/$stack" "$data/assets/fonts/"
done
cp "$src/fonts/OFL.txt" "$data/assets/fonts/"
cp "$src"/sprites/v4/dark.json "$src"/sprites/v4/dark.png \
   "$src"/sprites/v4/dark@2x.json "$src"/sprites/v4/dark@2x.png "$data/assets/sprites/v4/"
say "wrote data/assets (fonts: ${FONT_STACKS[*]}; sprites: v4/dark)"
say "done; run the gateway with --data-dir data"
