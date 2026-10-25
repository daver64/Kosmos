#!/bin/sh
# Cross-builds the Windows packages on Linux with mingw-w64 and vcpkg's static mingw libraries.
# Usage: build-windows.sh [target ...]   (default: kosmos_package)
# Output: build-win/package/*.zip
# Always a clean rebuild (every object recompiled); set INCREMENTAL=1 to reuse the existing build tree.
# Needs simlib installed for mingw (see README) under SIMLIB_PREFIX.
# Overrides (env): VCPKG, TRIPLETS, OUT, INCREMENTAL, SIMLIB_PREFIX (default ~/opt-win), KOSMOS_SIMLIB_DIR (build against a simlib source tree instead)
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/build-win}
VCPKG=${VCPKG:-$HOME/vcpkg-mingw}
TRIPLETS=${TRIPLETS:-$HOME/mingw-triplets}
SIMLIB_PREFIX=${SIMLIB_PREFIX:-$HOME/opt-win}

cmake -S "$SRC" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$TRIPLETS/mingw-posix.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-posix -DVCPKG_HOST_TRIPLET=x64-linux -DVCPKG_MANIFEST_MODE=OFF \
  -DCMAKE_PREFIX_PATH="$SIMLIB_PREFIX" -DVCPKG_INSTALLED_DIR=$VCPKG/installed -DSIMLIB_BUILD_SHADER_COMPILER=OFF \
  ${KOSMOS_SIMLIB_DIR:+-DKOSMOS_SIMLIB_DIR=$KOSMOS_SIMLIB_DIR} \
  -DCMAKE_CXX_FLAGS="-I$TRIPLETS/inc -D_USE_MATH_DEFINES" \
  -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"

[ $# -eq 0 ] && set -- kosmos_package
cmake --build "$OUT" $([ -z "$INCREMENTAL" ] && echo --clean-first) ${JOBS:+-j "$JOBS"} --target "$@"
ls -la "$OUT"/package/*.zip
