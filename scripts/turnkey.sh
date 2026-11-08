#!/bin/sh
# Build and publish the next release, then update the public website.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 "$ROOT/scripts/turnkey.py" "$@"
