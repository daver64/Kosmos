#!/bin/sh
# Builds the Linux and Windows packages locally; it never publishes or advances VERSION.
# Usage: release.sh [-j N] [--linux-only | --windows-only]
#        release.sh --stage-only DIR --pack FILE --rights-audit FILE
# -j N (or JOBS=N) sets the most compile jobs run at once; the default is 4.
# --stage-only stages already built and rights-reviewed assets; the output directory must not exist.
# Overrides (env): the ones of build-linux.sh and build-windows.sh (OUT is not passed on).
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
linux=1 windows=1
stage= pack= audit=
JOBS=${JOBS:-4}
while [ $# -gt 0 ]; do
    a=$1; shift
    case $a in
        -j|--jobs) JOBS=$1; shift ;;
        -j*) JOBS=${a#-j} ;;
        --no-publish) : ;;
        --linux-only) windows=0 ;;
        --windows-only) linux=0 ;;
        --stage-only|--pack|--rights-audit)
            [ $# -gt 0 ] || { echo "missing value for $a" >&2; exit 1; }
            case $a in --stage-only) stage=$1 ;; --pack) pack=$1 ;; --rights-audit) audit=$1 ;; esac
            shift ;;
        *) sed -n 2,4p "$0"; exit 1 ;;
    esac
done

# Tooling never uses more than 4 jobs at once, whatever -j or JOBS says.
case $JOBS in ''|*[!0-9]*|0) echo "jobs must be a positive number" >&2; exit 1 ;; esac
if [ "$JOBS" -gt 4 ]; then JOBS=4; fi
export JOBS
VER=$(head -n 1 "$SRC/VERSION" | tr -d '[:space:]')
if [ -n "$stage" ]; then
    [ "$linux" -eq 1 ] && [ "$windows" -eq 1 ] || { echo "staging requires both platforms" >&2; exit 1; }
    [ ! -e "$stage" ] || { echo "staging directory already exists: $stage" >&2; exit 1; }
    [ -f "$pack" ] && [ -f "$audit" ] && [ -f "$SRC/docs/release-notes/$VER.md" ] || {
        echo "staging requires an existing --pack, --rights-audit and release notes for $VER" >&2; exit 1;
    }
elif [ -n "$pack" ] || [ -n "$audit" ]; then
    echo "--pack and --rights-audit require --stage-only" >&2; exit 1
fi
archives=
if [ -z "$stage" ] && [ $linux -eq 1 ]; then
    env -u OUT "$SRC/scripts/build-linux.sh" kosmos_package
    archives="$archives $SRC/build/package/kosmos-beta-$VER-Linux.tar.gz"
fi
if [ -z "$stage" ] && [ $windows -eq 1 ]; then
    env -u OUT "$SRC/scripts/build-windows.sh" kosmos_package
    archives="$archives $SRC/build-win/package/kosmos-beta-$VER-Windows.zip"
fi

if [ -n "$stage" ]; then
    python3 "$SRC/scripts/stage-github-release.py" --version "$VER" \
        --linux "$SRC/build/package/kosmos-beta-$VER-Linux.tar.gz" \
        --windows "$SRC/build-win/package/kosmos-beta-$VER-Windows.zip" \
        --sdk-linux "$SRC/build/package/kosmos-sdk.tar.gz" \
        --sdk-windows "$SRC/build/package/kosmos-sdk.zip" \
        --pack "$pack" --rights-audit "$audit" --notes "$SRC/docs/release-notes/$VER.md" --out "$stage"
fi
if [ -z "$stage" ]; then
    echo "built:$archives (not published; VERSION unchanged)"
fi
