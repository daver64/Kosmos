#!/bin/sh
# Writes download/packs.json, the list of optional packs the launcher offers, from the pack archives found in the
# download folder (name, size and SHA-256 are read from the files). Run it after copying a new kosmos-hires.tar.gz,
# publish-web.sh also runs it.
# Overrides (env): WEB_ROOT (default /var/www/html)
set -e
WEB_ROOT=${WEB_ROOT:-/var/www/html}
DOWNLOAD=$WEB_ROOT/kosmos/download
out=$DOWNLOAD/packs.json
hires=$DOWNLOAD/kosmos-hires.tar.gz
[ -f "$hires" ] || { echo "make-packs-json: no kosmos-hires.tar.gz in $DOWNLOAD, nothing to list"; exit 0; }

size=$(stat -c %s "$hires")
sum=$(sha256sum "$hires" | cut -d' ' -f1)
cat > "$out.new" <<EOF
{
  "packs": [
    {
      "id": "hires",
      "name": "High-resolution Earth, Moon and Mars",
      "description": "16k planet maps, Earth surface tiles, and normal and specular maps. Optional; Kosmos falls back to the standard images without them.",
      "file": "kosmos-hires.tar.gz",
      "size": $size,
      "sha256": "$sum",
      "marker": "data/textures-hires"
    }
  ]
}
EOF
mv "$out.new" "$out"
echo "make-packs-json: wrote $out"
