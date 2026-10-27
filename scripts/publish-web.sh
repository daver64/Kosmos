#!/bin/sh
# Publishes freshly built archives to the web site and keeps the version numbers in step.
#
# Usage: publish-web.sh ARCHIVE...
#        publish-web.sh --stamp VERSION     rewrite the version shown on the pages without publishing
#
# Versions are MAJOR.BUILD: MAJOR is 0 while Kosmos is in beta, BUILD counts published binaries.
# The VERSION file holds the number the next published build will carry, so the binary you built already
# shows it. When an archive's SHA-256 differs from the one on the site, the archives are copied to the download
# folder, SHA256SUMS.txt and every version marker on the pages (class="ver" data-ver="kosmos") are updated, and VERSION
# moves on to the next build. When every checksum matches the published one nothing changes.
# Each build needs docs/release-notes/VERSION.md (user-facing changes); publishing stops without it.
# Overrides (env): WEB_ROOT (default /var/www/html)
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
WEB_ROOT=${WEB_ROOT:-/var/www/html}

stamp() {
    find "$WEB_ROOT" -maxdepth 2 -name '*.html' -print0 |
        xargs -0 sed -i -E "s|(class=\"ver\" data-ver=\"kosmos\">)[^<]*|\1$1|g"
}

if [ "$1" = "--stamp" ]; then
    stamp "$2"
    exit 0
fi

[ $# -ge 1 ] || { sed -n 2,3p "$0"; exit 1; }
APP=kosmos
DOWNLOAD=$WEB_ROOT/kosmos/download
VERSION_FILE=$SRC/VERSION

VER=$(head -n 1 "$VERSION_FILE" | tr -d '[:space:]')
MAJOR=${VER%%.*}
BUILD=${VER#*.}

NOTES=$SRC/docs/release-notes/$VER.md
[ -f "$NOTES" ] || { echo "$APP: missing release notes $NOTES (write them before publishing)"; exit 1; }

changed=0
for f in "$@"; do
    name=$(basename "$f")
    new=$(sha256sum "$f" | cut -d' ' -f1)
    old=$(grep -s "  $name\$" "$DOWNLOAD/SHA256SUMS.txt" | cut -d' ' -f1 || true)
    [ "$new" = "$old" ] || changed=1
done
"$SRC/scripts/make-packs-json.sh"
if [ $changed -eq 0 ]; then
    echo "$APP: checksums unchanged, still at the published build (next build will be $VER)"
    exit 0
fi

mkdir -p "$DOWNLOAD"
for f in "$@"; do
    name=$(basename "$f")
    cp "$f" "$DOWNLOAD/$name"
    sums=$(grep -s -v "  $name\$" "$DOWNLOAD/SHA256SUMS.txt" || true)
    { [ -z "$sums" ] || echo "$sums"; sha256sum "$f" | sed "s|  .*|  $name|"; } | sort -k2 > "$DOWNLOAD/SHA256SUMS.txt.new"
    mv "$DOWNLOAD/SHA256SUMS.txt.new" "$DOWNLOAD/SHA256SUMS.txt"
done
echo "$VER" > "$DOWNLOAD/VERSION.txt"
# RELEASE_NOTES.md on the site is every version's notes, newest first
mkdir -p "$DOWNLOAD/notes"
cp "$NOTES" "$DOWNLOAD/notes/$VER.md"
ls "$DOWNLOAD"/notes/*.md | sed -E 's|.*/([0-9.]+)\.md$|\1|' | sort -t. -k1,1nr -k2,2nr |
    while read -r v; do cat "$DOWNLOAD/notes/$v.md"; echo; done > "$DOWNLOAD/RELEASE_NOTES.md"
"$SRC/scripts/notes-html.py" "$DOWNLOAD/RELEASE_NOTES.md" "$WEB_ROOT/kosmos/release-notes.html"
stamp "$VER"
echo "$MAJOR.$((BUILD + 1))" > "$VERSION_FILE"
echo "$APP: published $VER; the next build will be $MAJOR.$((BUILD + 1))"
