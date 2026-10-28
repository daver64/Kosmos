#!/bin/sh
# Builds the Kosmos SDK archives (kosmos-sdk.tar.gz and kosmos-sdk.zip) in build/package/ from sdk/.
# The SDK is separate from the game: it holds the KosmosLink client library, examples, a mock server and the manual.
#
# Usage: build-sdk.sh [--publish]
#   --publish   also copies the archives to the web download folder, updates SHA256SUMS.txt, and regenerates sdk.html
# Overrides (env): WEB_ROOT (default /var/www/html)
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
WEB_ROOT=${WEB_ROOT:-/var/www/html}
OUT=$SRC/build/package
STAGE=$OUT/sdk-stage/kosmos-sdk
VER=$(head -n 1 "$SRC/VERSION" | tr -d '[:space:]')

rm -rf "$OUT/sdk-stage"
mkdir -p "$STAGE" "$OUT"
cp -r "$SRC/sdk/include" "$SRC/sdk/src" "$SRC/sdk/examples" "$SRC/sdk/template" "$SRC/sdk/tools" "$SRC/sdk/docs" "$SRC/sdk/CMakeLists.txt" "$STAGE/"
rm -rf "$STAGE/template/bin" "$STAGE/examples/__pycache__" "$STAGE/tools/__pycache__"
cp "$STAGE/docs/MANUAL.md" "$STAGE/README.md"
echo "$VER" > "$STAGE/VERSION"
cp "$SRC/LICENSE"* "$STAGE/" 2>/dev/null || true

# Prove the package builds before shipping it.
BUILD=$(mktemp -d)
cmake -S "$STAGE" -B "$BUILD" > /dev/null
cmake --build "$BUILD" > /dev/null
cmake -S "$STAGE/template" -B "$BUILD/template" > /dev/null
cmake --build "$BUILD/template" > /dev/null
rm -rf "$BUILD" "$STAGE/template/bin"

rm -f "$OUT/kosmos-sdk.tar.gz" "$OUT/kosmos-sdk.zip"
(cd "$OUT/sdk-stage" && tar czf "$OUT/kosmos-sdk.tar.gz" kosmos-sdk && zip -qr "$OUT/kosmos-sdk.zip" kosmos-sdk)
rm -rf "$OUT/sdk-stage"
echo "built $OUT/kosmos-sdk.tar.gz and kosmos-sdk.zip (Kosmos $VER)"

if [ "$1" = "--publish" ]; then
    DOWNLOAD=$WEB_ROOT/kosmos/download
    mkdir -p "$DOWNLOAD"
    for name in kosmos-sdk.tar.gz kosmos-sdk.zip; do
        cp "$OUT/$name" "$DOWNLOAD/$name"
        sums=$(grep -s -v "  $name\$" "$DOWNLOAD/SHA256SUMS.txt" || true)
        { [ -z "$sums" ] || echo "$sums"; (cd "$OUT" && sha256sum "$name"); } | sort -k2 > "$DOWNLOAD/SHA256SUMS.txt.new"
        mv "$DOWNLOAD/SHA256SUMS.txt.new" "$DOWNLOAD/SHA256SUMS.txt"
    done
    python3 "$SRC/scripts/sdk-manual-html.py" "$SRC/sdk/docs/MANUAL.md" "$WEB_ROOT/kosmos"
    echo "published to $DOWNLOAD"
fi
