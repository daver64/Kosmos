#!/bin/sh
# Checks that the simlib source checkout matches the commit Kosmos is pinned to (SIMLIB_COMMIT at the repo root).
# Source checkout: KOSMOS_SIMLIB_DIR, else SIMLIB_SRC, else ~/Documents/Projects/rsimlib.
# Set SIMLIB_ALLOW_MISMATCH=1 to continue anyway. To move the pin after changing simlib, commit simlib,
# install it, then write the new hash into SIMLIB_COMMIT: git -C <rsimlib> rev-parse HEAD > SIMLIB_COMMIT
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
PIN=$(tr -d '[:space:]' < "$SRC/SIMLIB_COMMIT")
LIB=${KOSMOS_SIMLIB_DIR:-${SIMLIB_SRC:-$HOME/Documents/Projects/rsimlib}}
if [ ! -d "$LIB/.git" ]; then
    echo "check-simlib: no simlib checkout at $LIB; cannot verify the pin ($PIN)" >&2
    exit 0
fi
HEAD=$(git -C "$LIB" rev-parse HEAD)
DIRTY=$(git -C "$LIB" status --porcelain --untracked-files=no | wc -l)
if [ "$HEAD" != "$PIN" ] || [ "$DIRTY" -ne 0 ]; then
    echo "check-simlib: simlib at $LIB is $HEAD ($DIRTY modified files); Kosmos is pinned to $PIN" >&2
    [ -n "$SIMLIB_ALLOW_MISMATCH" ] && exit 0
    echo "Check out the pinned commit, or set SIMLIB_ALLOW_MISMATCH=1." >&2
    exit 1
fi
echo "check-simlib: simlib matches pin ${PIN%${PIN#???????}}"
