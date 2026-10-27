#!/bin/sh
# Builds the Linux package.
# Usage: build-linux.sh [-j N] [target ...]   (default: kosmos_package)
# Output: build/package/kosmos-beta-Linux.tar.gz
# Always a clean rebuild (every object recompiled); set INCREMENTAL=1 to reuse the existing build tree.
# Needs simlib installed (cmake --install) under SIMLIB_PREFIX.
# Overrides (env): OUT, JOBS (default 4; -j N overrides), INCREMENTAL, SIMLIB_PREFIX (default ~/opt), KOSMOS_SIMLIB_DIR (build against a simlib source tree instead)
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
SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/build}
SIMLIB_PREFIX=${SIMLIB_PREFIX:-$HOME/opt}

cmake -S "$SRC" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release -DKOSMOS_BUILD_JOBS="$JOBS" \
  -DCMAKE_PREFIX_PATH="$SIMLIB_PREFIX" \
  ${KOSMOS_SIMLIB_DIR:+-DKOSMOS_SIMLIB_DIR=$KOSMOS_SIMLIB_DIR}

[ $# -eq 0 ] && set -- kosmos_package
nice cmake --build "$OUT" $([ -z "$INCREMENTAL" ] && echo --clean-first) -j "$JOBS" --target "$@"
ls -la "$OUT"/package/*.tar.gz
