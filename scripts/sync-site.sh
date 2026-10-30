#!/bin/sh
# Copy just the web pages (html, css, js, images, report/ scripts) from this machine's site to the public host.
# No build, no version bump, and downloads/, kav/ and the SDK archives are left alone.
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
