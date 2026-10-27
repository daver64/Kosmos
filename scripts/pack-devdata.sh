#!/bin/sh
# Packs the large source data that is not in git or in the game package, for other developers.
# It holds the raw Earth land-cover rasters (data/landclass, the input for the land-cover cache) and, with
# --hires, the hi-res textures from pack-hires.sh. Players do not need any of it.
# Usage: pack-devdata.sh [--hires] [--xz] [OUTPUT]
#   --hires  also include data/textures-hires and data/CREDITS-textures.md
#   --xz     use xz instead of gzip (smaller, much slower)
# Unpack from the Kosmos folder: tar -xf kosmos-devdata.tar.gz
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
hires=0 ext=gz flag=-z out=
for a in "$@"; do
    case $a in
        --hires) hires=1 ;;
        --xz) ext=xz flag=-J ;;
        -*) sed -n 2,9p "$0"; exit 1 ;;
        *) out=$a ;;
    esac
done
[ -n "$out" ] || out="$SRC/kosmos-devdata.tar.$ext"

cd "$SRC"
[ -d data/landclass ] || { echo "data/landclass not found" >&2; exit 1; }
set -- data/landclass
if [ $hires -eq 1 ]; then
    [ -d data/textures-hires ] || { echo "data/textures-hires not found" >&2; exit 1; }
    set -- "$@" data/textures-hires data/CREDITS-textures.md
fi
# The unpacked cache (380 MB) is rebuilt from the rasters, so it stays out.
tar $flag -cf "$out" --exclude='*.kavlc' "$@"
du -h "$out" | cut -f1 | sed "s|\$| $out|"
echo "Unpack from the Kosmos folder with: tar -xf $(basename "$out")"
