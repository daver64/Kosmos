#!/bin/sh
# Mirror only game archives, including deletions, without touching other downloads.
set -e
[ "$#" -eq 2 ] || { echo "usage: sh sync-release-archives.sh SOURCE DESTINATION" >&2; exit 1; }
rsync -dlt --delete \
    --include='/kosmos-beta-*Linux.tar.gz' \
    --include='/kosmos-beta-*Windows.zip' \
    --exclude='*' "$1/" "$2/"
