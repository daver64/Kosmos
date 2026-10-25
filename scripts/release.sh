#!/bin/sh
# Builds the Linux and Windows packages and publishes them to the web site.
# Usage: release.sh [--no-publish] [--linux-only | --windows-only]
# Publishing (scripts/publish-web.sh) only acts when an archive's checksum differs from the published one,
# then it stamps the new version on the pages and moves VERSION on to the next build.
# Overrides (env): the ones of build-linux.sh, build-windows.sh and publish-web.sh (OUT is not passed on).
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
linux=1 windows=1 publish=1
for a in "$@"; do
    case $a in
        --no-publish) publish=0 ;;
        --linux-only) windows=0 ;;
        --windows-only) linux=0 ;;
        *) sed -n 2,3p "$0"; exit 1 ;;
    esac
done

archives=
if [ $linux -eq 1 ]; then
    env -u OUT "$SRC/scripts/build-linux.sh"
    archives="$archives $SRC/build/package/kosmos-beta-Linux.tar.gz"
fi
if [ $windows -eq 1 ]; then
    env -u OUT "$SRC/scripts/build-windows.sh"
    archives="$archives $SRC/build-win/package/kosmos-beta-Windows.zip"
fi

if [ $publish -eq 1 ]; then
    # shellcheck disable=SC2086
    "$SRC/scripts/publish-web.sh" $archives
else
    echo "built:$archives (not published)"
fi
