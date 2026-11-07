#!/bin/sh
# Packs the hi-res textures (not in git) into one archive for other developers.
# Main Belt Asteroids add-on assets are excluded: their redistribution is not cleared.
# Usage: pack-hires.sh [--sources] [--no-procedural] [--xz] [OUTPUT]
#   --sources        also include import/celestia/hires, the Celestia imagery the textures are built from (large)
#   --no-procedural  leave out textures-hires/procedural, which tools/gen_textures.py can regenerate
#   --xz             use xz instead of gzip (smaller, much slower)
# Unpack from the Kosmos folder: tar -xf kosmos-hires.tar.gz
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
sources=0 procedural=1 ext=gz flag=-z out=
for a in "$@"; do
    case $a in
        --sources) sources=1 ;;
        --no-procedural) procedural=0 ;;
        --xz) ext=xz flag=-J ;;
        -*) sed -n 2,7p "$0"; exit 1 ;;
        *) out=$a ;;
    esac
done
[ -n "$out" ] || out="$SRC/kosmos-hires.tar.$ext"

cd "$SRC"
[ -d data/textures-hires ] || { echo "data/textures-hires not found" >&2; exit 1; }
set -- data/textures-hires data/CREDITS-textures.md
[ $sources -eq 1 ] && [ -d import/celestia/hires ] && set -- "$@" import/celestia/hires

if [ $procedural -eq 0 ]; then
    set -- --exclude=data/textures-hires/procedural "$@"
fi
tar $flag -cf "$out" \
    --exclude=data/textures-hires/gaspra.dds \
    --exclude=data/textures-hires/ida.dds \
    --exclude=data/textures-hires/kleopatra.dds \
    --exclude=data/textures-hires/toutatis.dds \
    --exclude=data/textures-hires/bodies/gaspra.obj \
    --exclude=data/textures-hires/bodies/ida.obj \
    --exclude=data/textures-hires/bodies/kleopatra.obj \
    --exclude=data/textures-hires/bodies/toutatis.obj \
    "$@"
du -h "$out" | cut -f1 | sed "s|\$| $out|"
echo "Unpack from the Kosmos folder with: tar -xf $(basename "$out")"
