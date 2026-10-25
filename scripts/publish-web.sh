#!/bin/sh
# Publishes freshly built archives to the web site and keeps the version numbers in step.
#
# Usage: publish-web.sh APP ARCHIVE...      APP is kosmos, exdefender or exinvaders
#        publish-web.sh --stamp APP VERSION rewrite the version shown on the pages without publishing
#
# Versions are MAJOR.BUILD: MAJOR is 0 for kosmos and 1 for exdefender and exinvaders, BUILD counts published binaries.
# The VERSION file of each app holds the number the next published build will carry, so the binary you built already
# shows it. When an archive's SHA-256 differs from the one on the site, the archives are copied to the download
# folder, SHA256SUMS.txt and every version marker on the pages (class="ver" data-ver="APP") are updated, and VERSION
# moves on to the next build. When every checksum matches the published one nothing changes.
# Overrides (env): WEB_ROOT (default /var/www/html), SIMLIB_DIR (default ../rsimlib next to this repo)
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
WEB_ROOT=${WEB_ROOT:-/var/www/html}
SIMLIB_DIR=${SIMLIB_DIR:-$SRC/../rsimlib}

stamp() {
    find "$WEB_ROOT" -maxdepth 2 -name '*.html' -print0 |
        xargs -0 sed -i -E "s|(class=\"ver\" data-ver=\"$1\">)[^<]*|\1$2|g"
}

if [ "$1" = "--stamp" ]; then
    stamp "$2" "$3"
    exit 0
fi

APP=$1
[ $# -ge 2 ] || { sed -n 2,5p "$0"; exit 1; }
shift
case $APP in
    kosmos) DOWNLOAD=$WEB_ROOT/kosmos/download; VERSION_FILE=$SRC/VERSION ;;
    exdefender|exinvaders) DOWNLOAD=$WEB_ROOT/$APP/download; VERSION_FILE=$SIMLIB_DIR/src/examples/$APP/VERSION ;;
    *) echo "unknown app: $APP" >&2; exit 1 ;;
esac

VER=$(head -n 1 "$VERSION_FILE" | tr -d '[:space:]')
MAJOR=${VER%%.*}
BUILD=${VER#*.}

changed=0
for f in "$@"; do
    name=$(basename "$f")
    new=$(sha256sum "$f" | cut -d' ' -f1)
    old=$(grep -s "  $name\$" "$DOWNLOAD/SHA256SUMS.txt" | cut -d' ' -f1 || true)
    [ "$new" = "$old" ] || changed=1
done
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
stamp "$APP" "$VER"
echo "$MAJOR.$((BUILD + 1))" > "$VERSION_FILE"
echo "$APP: published $VER; the next build will be $MAJOR.$((BUILD + 1))"
