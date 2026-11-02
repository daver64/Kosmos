#!/bin/sh
# Cross-builds the Windows GTK 3 programs for the game package: addons/planetbake/bin/planetbake.exe and
# addons/kosmosmap/bin/kosmosmap.exe and addons/kosmosav/bin/kosmosav.exe and addons/shipedit/bin/shipedit.exe and addons/kosmoscad/bin/kosmoscad.exe, each with its compiled GSettings schemas in addons/<name>/share/glib-2.0/schemas.
# Usage: build-win-tools.sh
# Needs mingw-w64 and GTK 3 in vcpkg's x64-mingw-posix tree (see README, "Windows GTK"), plus the overlays in ~/mingw-triplets.
# Overrides (env): VCPKG_MINGW (default ~/vcpkg-mingw), MINGW_PKGCONFIG_EXTRA (default ~/mingw-triplets/pkgconfig), FASTNOISE_DIR, GLM_INCLUDE_DIR
# Exits 1 without touching anything when GTK is missing, so build-windows.sh can fall back to the command line planetbake.
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
VCPKG_MINGW=${VCPKG_MINGW:-$HOME/vcpkg-mingw}
WINGTK=$VCPKG_MINGW/installed/x64-mingw-posix
WINPC=$WINGTK/lib/pkgconfig:${MINGW_PKGCONFIG_EXTRA:-$HOME/mingw-triplets/pkgconfig}
WINCXX=x86_64-w64-mingw32-g++-posix
WINCC=x86_64-w64-mingw32-gcc-posix
FASTNOISE_DIR=${FASTNOISE_DIR:-$HOME/opt/include}
VER=$(cat "$SRC/VERSION" 2> /dev/null || echo 0)

pc() { PKG_CONFIG_LIBDIR=$WINPC PKG_CONFIG_PATH= pkg-config "$@"; }
if ! command -v "$WINCXX" > /dev/null 2>&1 || [ ! -f "$WINGTK/lib/libgtk-3.a" ] || ! pc --exists gtk+-3.0 gdk-pixbuf-2.0; then
    echo "no Windows GTK 3 here (needs $WINCXX and GTK 3 in $WINGTK)" >&2
    exit 1
fi
CFLAGS=$(pc --cflags gtk+-3.0 gdk-pixbuf-2.0)
LIBS=$(pc --static --libs gtk+-3.0 gdk-pixbuf-2.0 | sed 's/-lgcc_s//g')
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export WINCXX WINCC CFLAGS TMP SRC WINGTK VER FASTNOISE_DIR

schemas() {
    mkdir -p "$1/share/glib-2.0/schemas"
    cp "$WINGTK"/share/glib-2.0/schemas/*.gschema.xml "$1/share/glib-2.0/schemas/"
    glib-compile-schemas "$1/share/glib-2.0/schemas"
    rm -f "$1/share/glib-2.0/schemas"/*.xml
}

# planetbake: at most 4 compiles at once.
PB=$SRC/sdk/planetbake
mkdir -p "$TMP/pb" "$TMP/km"
# Inside the game tree the generator is the game's own src/planet_gen.*.
GEN=$SRC/src
export PB GEN
printf '%s\n' main bake layers encode gui imageio pack settings fill edit | xargs -P 4 -I{} sh -c \
    '"$WINCXX" -std=c++17 -O2 -DPLANETBAKE_GTK=1 $CFLAGS "-DPLANETBAKE_VERSION=\"$VER\"" -I"$PB/compat" -I"$PB/src" -I"$GEN" -I"$WINGTK/include" -I"${GLM_INCLUDE_DIR:-$FASTNOISE_DIR}" -c "$PB/src/{}.cpp" -o "$TMP/pb/{}.o"'
"$WINCXX" -std=c++17 -O2 "-DPLANETBAKE_VERSION=\"$VER\"" -I"$PB/compat" -I"$PB/src" -I"$GEN" -I"$WINGTK/include" -I"${GLM_INCLUDE_DIR:-$FASTNOISE_DIR}" -c "$GEN/planet_gen.cpp" -o "$TMP/pb/planet_gen.o"
mkdir -p "$SRC/addons/planetbake/bin"
"$WINCXX" -static -o "$SRC/addons/planetbake/bin/planetbake.exe" "$TMP"/pb/*.o $LIBS
schemas "$SRC/addons/planetbake"

# KosmosMap.
KM=$SRC/sdk/kosmosmap
printf '%s\n' main render terrain | xargs -P 4 -I{} sh -c \
    '"$WINCXX" -std=c++17 -O2 -D_USE_MATH_DEFINES $CFLAGS -I"$SRC/sdk/include" -I"$WINGTK/include" -c "'"$KM"'/src/{}.cpp" -o "$TMP/km/{}.o"'
"$WINCC" -O2 -I"$SRC/sdk/include" -c "$SRC/sdk/src/kosmos_link.c" -o "$TMP/km/kosmos_link.o"
mkdir -p "$SRC/addons/kosmosmap/bin"
"$WINCXX" -static -o "$SRC/addons/kosmosmap/bin/kosmosmap.exe" "$TMP"/km/*.o $LIBS -lws2_32
schemas "$SRC/addons/kosmosmap"

# ShipEdit (no simlib; only the json header).
SE=$SRC/sdk/shipedit
JSON_INCLUDE=${SHIPEDIT_JSON_INCLUDE:-$HOME/Documents/Projects/rsimlib/include}
mkdir -p "$TMP/se"
printf '%s\n' main model mesh preview gl_preview | xargs -P 4 -I{} sh -c \
    '"$WINCXX" -std=c++17 -O2 -D_USE_MATH_DEFINES $CFLAGS -I"'"$SE"'/src" -I"'"$JSON_INCLUDE"'" -I"$WINGTK/include" -c "'"$SE"'/src/{}.cpp" -o "$TMP/se/{}.o"'
mkdir -p "$SRC/addons/shipedit/bin"
"$WINCXX" -static -mwindows -o "$SRC/addons/shipedit/bin/shipedit.exe" "$TMP"/se/*.o $LIBS
schemas "$SRC/addons/shipedit"

# KosmosCAD (no simlib; only the json header; OpenGL through GTK's libepoxy).
KC=$SRC/sdk/kosmoscad
mkdir -p "$TMP/kc"
printf '%s\n' main editor gl_view mesh csg scene equipment ship_io image mesh_io | xargs -P 4 -I{} sh -c \
    '"$WINCXX" -std=c++17 -O2 -D_USE_MATH_DEFINES $CFLAGS -I"'"$KC"'/src" -I"'"$JSON_INCLUDE"'" -I"$WINGTK/include" -c "'"$KC"'/src/{}.cpp" -o "$TMP/kc/{}.o"'
mkdir -p "$SRC/addons/kosmoscad/bin"
"$WINCXX" -static -mwindows -o "$SRC/addons/kosmoscad/bin/kosmoscad.exe" "$TMP"/kc/*.o $LIBS
schemas "$SRC/addons/kosmoscad"

# KosmosAV (WinHTTP, no libcurl).
KAV=$SRC/kav/client/src
mkdir -p "$TMP/av"
printf '%s\n' main agent http | xargs -P 4 -I{} sh -c \
    '"$WINCXX" -std=c++17 -O2 -D_USE_MATH_DEFINES $CFLAGS -I"$SRC/sdk/include" -I"'"$KAV"'" -I"$WINGTK/include" -c "'"$KAV"'/{}.cpp" -o "$TMP/av/{}.o"'
"$WINCC" -O2 -I"$SRC/sdk/include" -c "$SRC/sdk/src/kosmos_link.c" -o "$TMP/av/kosmos_link.o"
mkdir -p "$SRC/addons/kosmosav/bin"
"$WINCXX" -static -mwindows -o "$SRC/addons/kosmosav/bin/kosmosav.exe" "$TMP"/av/*.o $LIBS -lws2_32 -lwinhttp
schemas "$SRC/addons/kosmosav"
ls -la "$SRC/addons/kosmosav/bin/kosmosav.exe" "$SRC/addons/planetbake/bin/planetbake.exe" "$SRC/addons/kosmosmap/bin/kosmosmap.exe" "$SRC/addons/shipedit/bin/shipedit.exe" "$SRC/addons/kosmoscad/bin/kosmoscad.exe"
