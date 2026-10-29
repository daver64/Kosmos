#!/bin/sh
# Builds kosmosavd and installs or upgrades KosmosAV (the server, its systemd unit, the Apache proxy snippet and the static landing page).
#
# Usage: deploy-kav.sh                 this machine (asks for sudo)
#        deploy-kav.sh HOST [--secure]  another machine over ssh, e.g. dave@gemini (the install step asks for that machine's sudo password)
#
# --secure (first install only, kept in /etc/default/kosmosav there) makes the login cookie Secure: use it on a machine that serves HTTPS.
# The database and uploads in /var/lib/kosmosav are never touched. The binary is built here (at most 4 threads) and copied, so the other
# machine needs only the same kind of Linux, libsqlite3 and libcrypto, and Apache with proxy, proxy_http and headers (install.sh enables them).
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$SRC/build-kav

cmake -S "$SRC/kav/server" -B "$BUILD" -G Ninja >/dev/null
ninja -C "$BUILD" -j4 kosmosavd

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp "$BUILD/kosmosavd" "$SRC/kav/server/install.sh" "$SRC/kav/server/kosmosavd.service" "$SRC/kav/server/apache-kav.conf" "$SRC/kav/web/index.html" "$SRC/kav/data/registry.txt" "$STAGE/"

if [ -z "$1" ]; then
    sudo sh "$STAGE/install.sh"
else
    HOST=$1
    shift
    REMOTE=kosmosav-deploy
    rsync -az --delete "$STAGE/" "$HOST:$REMOTE/"
    ssh -t "$HOST" "sudo sh \$HOME/$REMOTE/install.sh $*"
fi
echo "KosmosAV deployed."
