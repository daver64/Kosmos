#!/bin/sh
# Stamp website version labels; release archives are published on GitHub.
set -e
WEB_ROOT=${WEB_ROOT:-/var/www/html}
if [ "$#" -eq 2 ] && [ "$1" = "--stamp" ]; then
    find "$WEB_ROOT" -maxdepth 2 -name '*.html' -print0 |
        xargs -0 sed -i -E -e "s|(class=\"ver\" data-ver=\"kosmos\">)[^<]*|\1$2|g"
    find "$WEB_ROOT" -maxdepth 2 -name '*.html' ! -name 'release-notes.html' -print0 |
        xargs -0 sed -i -E -e "s#kosmos-beta-([0-9]+\.[0-9]+-)?(Linux|Windows)#kosmos-beta-$2-\2#g"
    exit 0
fi
echo "publish-web: game archives are published on GitHub, not the website" >&2
exit 1
