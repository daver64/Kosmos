#!/bin/sh
# Builds the Kosmos SDK archives (kosmos-sdk.tar.gz and kosmos-sdk.zip) in build/package/ from sdk/.
# The SDK is separate from the game: it holds the KosmosLink client library, examples, KosmosMap (a larger example program), a mock server and the manual.
#
# Usage: build-sdk.sh [--publish]
#   --publish   also copies the archives to the web download folder, updates SHA256SUMS.txt, and regenerates sdk.html
# Overrides (env): WEB_ROOT (default /var/www/html), FASTNOISE_DIR (where FastNoiseLite.h is, default ~/opt/include),
#                  VCPKG_MINGW (vcpkg root with x64-mingw-static zlib, default ~/vcpkg-mingw; no Windows planetbake.exe without it)
#                  GLM_INCLUDE_DIR (folder holding glm/, default FASTNOISE_DIR; for the Windows planetbake build)
# The build never runs more than 4 jobs at once.
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
WEB_ROOT=${WEB_ROOT:-/var/www/html}
. "$SRC/scripts/release-web-common.sh"
OUT=$SRC/build/package
STAGE=$OUT/sdk-stage/kosmos-sdk
VER=$(head -n 1 "$SRC/VERSION" | tr -d '[:space:]')

rm -rf "$OUT/sdk-stage"
mkdir -p "$STAGE" "$OUT"
cp -r "$SRC/sdk/include" "$SRC/sdk/src" "$SRC/sdk/examples" "$SRC/sdk/template" "$SRC/sdk/kosmosmap" "$SRC/sdk/planetbake" "$SRC/sdk/tools" "$SRC/sdk/docs" "$SRC/sdk/CMakeLists.txt" "$STAGE/"
rm -rf "$STAGE/template/bin" "$STAGE/kosmosmap/bin" "$STAGE/examples/__pycache__" "$STAGE/tools/__pycache__"
cp "$STAGE/docs/MANUAL.md" "$STAGE/README.md"
rm -rf "$STAGE/planetbake/bin"
# planetbake uses the game's own world generator, so the SDK carries a copy of it.
FASTNOISE_DIR=${FASTNOISE_DIR:-$HOME/opt/include}
mkdir -p "$STAGE/planetbake/gen"
cp "$SRC/src/planet_gen.h" "$SRC/src/planet_gen.cpp" "$STAGE/planetbake/gen/"
cp "$FASTNOISE_DIR/FastNoiseLite.h" "$STAGE/planetbake/gen/"
echo "$VER" > "$STAGE/VERSION"
cp "$SRC/LICENSE"* "$STAGE/" 2>/dev/null || true

# Prove the package builds before shipping it.
BUILD=$(mktemp -d)
cmake -S "$STAGE" -B "$BUILD" > /dev/null
cmake --build "$BUILD" --parallel 4 > /dev/null
cmake -S "$STAGE/template" -B "$BUILD/template" > /dev/null
cmake --build "$BUILD/template" --parallel 4 > /dev/null

# Ship planetbake as a program too: the Linux build from the test build above, and a Windows command line build if mingw is here.
mkdir -p "$STAGE/planetbake/bin"
cp "$BUILD/planetbake/bin/planetbake" "$STAGE/planetbake/bin/planetbake-linux-x86_64"
VCPKG_MINGW=${VCPKG_MINGW:-$HOME/vcpkg-mingw}
WINZ=$VCPKG_MINGW/installed/x64-mingw-static
# With GTK 3 in vcpkg's x64-mingw-posix tree (and pkg-config) the Windows program gets the preview window too; otherwise it is command line only.
WINGTK=$VCPKG_MINGW/installed/x64-mingw-posix
WINPC=$WINGTK/lib/pkgconfig:${MINGW_PKGCONFIG_EXTRA:-$HOME/mingw-triplets/pkgconfig}
GTKDEF=""
GTKINC=""
GTKLIBS=""
if [ -f "$WINGTK/lib/libgtk-3.a" ] && command -v pkg-config > /dev/null 2>&1 \
    && PKG_CONFIG_LIBDIR=$WINPC PKG_CONFIG_PATH= pkg-config --exists gtk+-3.0; then
    GTKDEF=-DPLANETBAKE_GTK=1
    GTKINC=$(PKG_CONFIG_LIBDIR=$WINPC PKG_CONFIG_PATH= pkg-config --cflags gtk+-3.0)
    GTKLIBS=$(PKG_CONFIG_LIBDIR=$WINPC PKG_CONFIG_PATH= pkg-config --static --libs gtk+-3.0 | sed 's/-lgcc_s//g')
    WINZ=$WINGTK
fi
WINCXX=x86_64-w64-mingw32-g++-posix
if command -v "$WINCXX" > /dev/null 2>&1 && [ -f "$WINZ/lib/libzs.a" ]; then
    PB=$STAGE/planetbake
    WINTMP=$(mktemp -d)
    # At most 4 compiles at once.
    export WINCXX PB WINZ WINTMP VER FASTNOISE_DIR GTKDEF GTKINC
    printf '%s\n' main bake layers encode gui imageio pack settings fill edit | xargs -P 4 -I{} sh -c \
        '"$WINCXX" -std=c++17 -O2 $GTKDEF $GTKINC "-DPLANETBAKE_VERSION=\"$VER\"" -I"$PB/compat" -I"$PB/src" -I"$PB/gen" -I"$WINZ/include" -I"${GLM_INCLUDE_DIR:-$FASTNOISE_DIR}" -c "$PB/src/{}.cpp" -o "$WINTMP/{}.o"' 2> /dev/null || true
    "$WINCXX" -std=c++17 -O2 "-DPLANETBAKE_VERSION=\"$VER\"" -I"$PB/compat" -I"$PB/src" -I"$PB/gen" -I"$WINZ/include" -I"${GLM_INCLUDE_DIR:-$FASTNOISE_DIR}" -c "$PB/gen/planet_gen.cpp" -o "$WINTMP/planet_gen.o" 2> /dev/null || true
    if [ -n "$GTKDEF" ]; then
        "$WINCXX" -static -o "$PB/bin/planetbake-windows-x86_64.exe" "$WINTMP"/*.o $GTKLIBS 2> /dev/null || echo "note: no Windows planetbake.exe in the SDK (mingw build failed)"
        # GTK aborts without GSettings schemas (the file chooser needs them); GLib looks in <exe folder>/../share/glib-2.0/schemas
        mkdir -p "$PB/share/glib-2.0/schemas"
        cp "$WINGTK"/share/glib-2.0/schemas/*.gschema.xml "$PB/share/glib-2.0/schemas/"
        glib-compile-schemas "$PB/share/glib-2.0/schemas" && rm -f "$PB/share/glib-2.0/schemas"/*.xml
    else
        "$WINCXX" -static -o "$PB/bin/planetbake-windows-x86_64.exe" "$WINTMP"/*.o "$WINZ/lib/libzs.a" 2> /dev/null || echo "note: no Windows planetbake.exe in the SDK (mingw build failed)"
    fi
    rm -rf "$WINTMP"
else
    echo "note: no Windows planetbake.exe in the SDK (needs $WINCXX and zlib in $WINZ)"
fi
rm -rf "$BUILD" "$STAGE/template/bin" "$STAGE/kosmosmap/bin"

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
        update_download_size "$WEB_ROOT/kosmos/download.html" "$DOWNLOAD/$name"
    done
    python3 "$SRC/scripts/sdk-manual-html.py" "$SRC/sdk/docs/MANUAL.md" "$WEB_ROOT/kosmos"
    echo "published to $DOWNLOAD"
fi
