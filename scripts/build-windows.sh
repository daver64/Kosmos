#!/bin/sh
# Cross-builds the Windows packages on Linux with mingw-w64 and vcpkg's static mingw libraries.
# Usage: build-windows.sh [-j N] [target ...]   (default: kosmos_package)
# Output: build-win/package/*.zip
# Always a clean rebuild (every object recompiled); set INCREMENTAL=1 to reuse the existing build tree.
# Needs simlib installed for mingw (see README) under SIMLIB_PREFIX.
# Overrides (env): VCPKG, TRIPLETS, OUT, JOBS (default 4; -j N overrides), INCREMENTAL, SIMLIB_PREFIX (default ~/opt-win), KOSMOS_SIMLIB_DIR (build against a simlib source tree instead)
set -e
JOBS=${JOBS:-4}
while [ $# -gt 0 ]; do
    case $1 in
        -j|--jobs) JOBS=$2; shift 2 ;;
        -j*) JOBS=${1#-j}; shift ;;
        *) break ;;
    esac
done
case $JOBS in ''|*[!0-9]*|0) echo "jobs must be a positive number" >&2; exit 1 ;; esac

# Tooling never uses more than 4 jobs at once, whatever -j or JOBS says.
if [ "$JOBS" -gt 4 ]; then JOBS=4; fi
SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/build-win}
VCPKG=${VCPKG:-$HOME/vcpkg-mingw}
TRIPLETS=${TRIPLETS:-$HOME/mingw-triplets}
SIMLIB_PREFIX=${SIMLIB_PREFIX:-$HOME/opt-win}

sh "$SRC/scripts/check-simlib.sh"

# The GTK window programs (planetbake, KosmosMap) are built apart from CMake; without Windows GTK the package just lacks them.
PREBUILT=OFF
if sh "$SRC/scripts/build-win-tools.sh"; then PREBUILT=ON; else echo "note: no Windows planetbake/KosmosMap in the package" >&2; fi

cmake -S "$SRC" -B "$OUT" -DKOSMOS_PREBUILT_WIN_TOOLS=$PREBUILT -G Ninja -DCMAKE_BUILD_TYPE=Release -DKOSMOS_BUILD_JOBS="$JOBS" \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$TRIPLETS/mingw-posix.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-posix -DVCPKG_HOST_TRIPLET=x64-linux -DVCPKG_MANIFEST_MODE=OFF \
  -DCMAKE_PREFIX_PATH="$SIMLIB_PREFIX" -DVCPKG_INSTALLED_DIR=$VCPKG/installed -DSIMLIB_BUILD_SHADER_COMPILER=OFF \
  ${KOSMOS_SIMLIB_DIR:+-DKOSMOS_SIMLIB_DIR=$KOSMOS_SIMLIB_DIR} \
  -DCMAKE_CXX_FLAGS="-I$TRIPLETS/inc -D_USE_MATH_DEFINES" \
  -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"

[ $# -eq 0 ] && set -- kosmos_package
nice cmake --build "$OUT" $([ -z "$INCREMENTAL" ] && echo --clean-first) -j "$JOBS" --target "$@"
ls -la "$OUT"/package/*.zip
