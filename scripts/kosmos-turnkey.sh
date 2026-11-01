#!/bin/sh
# One command from "everything is ready" to "it is live on the public server".
#
# Usage: kosmos-turnkey.sh [--yes] [--skip-tests] [--no-build] [--no-kav] [--local-only] [--delete] [--host USER@HOST]
#
# 1. runs the tests (scripts/run-tests.sh)
# 2. builds the Linux and Windows packages and publishes them to this machine's web site (scripts/release.sh: stamps the version, notes, SDK)
# 3. installs the KosmosAV server here (scripts/deploy-kav.sh)
# 4. asks you to check the local site, then copies the Kosmos and arcade-game site directories plus the landing page to the public host with rsync
# 5. installs the KosmosAV server on the public host (scripts/deploy-kav.sh HOST)
#
# --yes          do not stop for the "local site looks right" question
# --skip-tests   skip step 1
# --no-build     skip the builds and publishing in step 2 (use what is already on the site)
# --no-kav       leave the KosmosAV server alone (steps 3 and 5)
# --local-only   stop after the local steps; nothing goes to the public host
# --delete       remove stale files within the explicitly synced site directories
# --host H       the public host (default dave@gemini, over ssh without a password)
# Tooling never uses more than 4 threads. Both machines ask for their sudo password during the KosmosAV install.
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
WEB_ROOT=${WEB_ROOT:-/var/www/html}
HOST=${HOST:-dave@gemini}
yes=0 tests=1 build=1 kav=1 public=1 del=
while [ $# -gt 0 ]; do
    a=$1; shift
    case $a in
        --yes) yes=1 ;;
        --skip-tests) tests=0 ;;
        --no-build) build=0 ;;
        --no-kav) kav=0 ;;
        --local-only) public=0 ;;
        --delete) del=--delete ;;
        --host) HOST=$1; shift ;;
        *) sed -n 2,3p "$0"; exit 1 ;;
    esac
done

step() { printf '\n== %s\n' "$*"; }

if [ $public -eq 1 ]; then
    ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" true || { echo "cannot reach $HOST over ssh without a password; nothing has been changed"; exit 1; }
fi

if [ $tests -eq 1 ]; then
    step "Tests"
    "$SRC/scripts/run-tests.sh"
fi
if [ $build -eq 1 ]; then
    step "Build and publish to this machine"
    "$SRC/scripts/release.sh"
fi
if [ $kav -eq 1 ]; then
    step "KosmosAV server on this machine"
    "$SRC/scripts/deploy-kav.sh"
fi

[ $public -eq 1 ] || { step "Done (local only)"; exit 0; }

if [ $yes -eq 0 ]; then
    printf '\nLocal site is updated. Check http://localhost/kosmos/ and http://localhost/kosmos/kav/app/ now.\nPublish to %s? [y/N] ' "$HOST"
    read -r answer
    case $answer in y|Y|yes) ;; *) echo "stopped before the public host; nothing was copied"; exit 0 ;; esac
fi

step "Copy the site to $HOST"
# Sync only known site directories. Never recurse over WEB_ROOT: it also contains large unrelated downloads.
# kav/ is root-owned (the KosmosAV install writes it with sudo), so it is left to deploy-kav.sh.
rsync -rlt --omit-dir-times --exclude=/kav/ --partial --info=progress2 $del "$WEB_ROOT/kosmos/" "$HOST:$WEB_ROOT/kosmos/"
for game in exinvaders exbomberman exdefender exasteroids; do
    rsync -rlt --omit-dir-times --partial --info=progress2 $del "$WEB_ROOT/$game/" "$HOST:$WEB_ROOT/$game/"
done

# The landing page and its arcade-game card thumbnails are one level up from kosmos/.
rsync -t "$WEB_ROOT/index.html" "$HOST:$WEB_ROOT/index.html"
rsync -t "$WEB_ROOT/nebulabrot.jpg" "$HOST:$WEB_ROOT/nebulabrot.jpg"
rsync -t "$WEB_ROOT"/img/card-kosmos.jpg "$WEB_ROOT"/img/card-simlib.jpg \
    "$WEB_ROOT"/img/card-exinvaders.jpg "$WEB_ROOT"/img/card-exbomberman.jpg \
    "$WEB_ROOT"/img/card-exdefender.jpg "$WEB_ROOT"/img/card-exasteroids.jpg "$HOST:$WEB_ROOT/img/"

if [ $kav -eq 1 ]; then
    step "KosmosAV server on $HOST"
    "$SRC/scripts/deploy-kav.sh" "$HOST"
fi
step "Published"
