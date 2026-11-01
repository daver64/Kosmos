#!/bin/sh
# Copy just the web pages (html, css, js, images, report/ scripts) from this machine's site to the public host.
# No build, no version bump; game and Kosmos downloads, kav/ and SDK archives are left alone.
#
# Usage: sync-site.sh [--dry-run] [--host USER@HOST]
set -e
WEB_ROOT=${WEB_ROOT:-/var/www/html}
HOST=${HOST:-dave@gemini}
dry=
while [ $# -gt 0 ]; do
    a=$1; shift
    case $a in
        --dry-run) dry=--dry-run ;;
        --host) HOST=$1; shift ;;
        *) sed -n 2,6p "$0"; exit 1 ;;
    esac
done
ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" true || { echo "cannot reach $HOST over ssh without a password"; exit 1; }
rsync -rlt --omit-dir-times --itemize-changes $dry \
    --exclude=/kav/ --exclude=/download/ \
    "$WEB_ROOT/kosmos/" "$HOST:$WEB_ROOT/kosmos/"
for game in exinvaders exbomberman exdefender exasteroids; do
    rsync -rlt --omit-dir-times --itemize-changes $dry --exclude=/download/ \
        "$WEB_ROOT/$game/" "$HOST:$WEB_ROOT/$game/"
done
rsync -t --itemize-changes $dry "$WEB_ROOT/index.html" "$HOST:$WEB_ROOT/index.html"
rsync -t --itemize-changes $dry "$WEB_ROOT/nebulabrot.jpg" "$HOST:$WEB_ROOT/nebulabrot.jpg"
rsync -t --itemize-changes $dry \
    "$WEB_ROOT"/img/card-kosmos.jpg "$WEB_ROOT"/img/card-simlib.jpg \
    "$WEB_ROOT"/img/card-exinvaders.jpg "$WEB_ROOT"/img/card-exbomberman.jpg \
    "$WEB_ROOT"/img/card-exdefender.jpg "$WEB_ROOT"/img/card-exasteroids.jpg "$HOST:$WEB_ROOT/img/"
