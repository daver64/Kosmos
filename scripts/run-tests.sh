#!/bin/sh
# Builds and runs every Kosmos test: the unit tests plus the headless arsenal and flight selftests.
# Usage: run-tests.sh [build-dir]   (default: build; the tree must already be configured)
# Exits non-zero if anything fails.
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$SRC/build}
JOBS=${JOBS:-4}
# Tooling never uses more than 4 jobs at once, whatever -j or JOBS says.
if [ "$JOBS" -gt 4 ]; then JOBS=4; fi

nice cmake --build "$OUT" -j "$JOBS" --target kosmos systems_test reentry_test weather_test joystick_test equipment_test keybindings_test camera_dynamics_test plasma_look_test link_test shipedit_model_test kosmoscad_test
cd "$OUT"
export HOME=$(mktemp -d)
trap 'rm -rf "$HOME"' EXIT

for t in systems_test reentry_test weather_test joystick_test equipment_test keybindings_test camera_dynamics_test plasma_look_test link_test shipedit_model_test kosmoscad_test; do
    echo "== $t"
    ./$t
done

echo "== arsenal selftest (landed Vulcan: kit, pistol, plasma, charges, blast damage, bomb rack)"
./kosmos --scenario 05_vulcan_wagga --selftest 1 --selftest-arsenal | tee arsenal_test.log
if grep -q '^FAIL' arsenal_test.log; then echo "arsenal selftest failed"; exit 1; fi
rm -f arsenal_test.log
echo "== weapons selftest (Deltaglider in orbit: beam, shield, flak, light cap)"
./kosmos --ship "CVEL Deltaglider" --orbit --selftest 1 --selftest-weapons | tee weapons_test.log
if grep -q '^FAIL' weapons_test.log; then echo "weapons selftest failed"; exit 1; fi
rm -f weapons_test.log

echo "== bombing selftest (regular and atom bomb detonate)"
for atom in "" "--selftest-drop-atom"; do
    ./kosmos --scenario 12_vulcan_atom_iceland --selftest 40 --selftest-drop 2 $atom | grep -q "detonations 1"
done
echo "all tests passed"
