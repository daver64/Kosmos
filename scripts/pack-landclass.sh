#!/bin/sh
# Condenses the Earth land cover into the gzipped cache that ships in the game package (about 26 MB).
# Run it after adding or changing import/landclass/sol/earth/consensus_full_class_N.tif. The game builds
# landclass.kavlc from those rasters on first use if it is missing, then this script compresses it.
# Usage: pack-landclass.sh
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
dir="$SRC/data/landclass/sol/earth"
[ -d "$dir" ] || { echo "$dir not found" >&2; exit 1; }
cache="$dir/landclass.kavlc"
if [ ! -f "$cache" ]; then
    echo "No $cache yet: start Kosmos once on Earth so it builds the cache from the .tif files, then run this again." >&2
    exit 1
fi
gzip -9 -c "$cache" > "$cache.gz"
du -h "$cache.gz" | cut -f1 | sed "s|\$| $cache.gz|"
