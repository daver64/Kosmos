#!/bin/sh
# Builds and runs the quick regression suite. Add --extended for long flight tests.
# Usage: run-tests.sh [--extended] [build-dir]   (default: build; the tree must already be configured)
# Exits non-zero if anything fails.
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=$SRC/build
extended=0
for arg in "$@"; do
    case "$arg" in
        --extended) extended=1 ;;
        --*) echo "unknown option: $arg" >&2; exit 1 ;;
        *) OUT=$arg ;;
    esac
done
JOBS=${JOBS:-4}
# Tooling never uses more than 4 jobs at once, whatever -j or JOBS says.
if [ "$JOBS" -gt 4 ]; then JOBS=4; fi

python3 "$SRC/tests/release_web_test.py"
python3 "$SRC/tests/scenario_addons_test.py"
python3 "$SRC/tests/ares_legs_test.py"
python3 "$SRC/tests/proton_exhaust_test.py"
nice cmake --build "$OUT" -j "$JOBS" --target kosmos systems_test reentry_test weather_test joystick_test equipment_test keybindings_test camera_dynamics_test plasma_look_test link_test shipedit_model_test kosmoscad_test missioncore_test launcher_test
cd "$OUT"
export HOME=$(mktemp -d)
export XDG_CONFIG_HOME="$HOME/.config"
trap 'rm -rf "$HOME"' EXIT

python3 "$SRC/tests/scenario_start_test.py" "$PWD/kosmos"
python3 "$SRC/tests/scenario_save_test.py" "$PWD/kosmos"
python3 "$SRC/tests/flight_recording_test.py" "$PWD/kosmos"
python3 "$SRC/tests/sync_approach_test.py" "$PWD/kosmos"
python3 "$SRC/tests/gui_input_test.py" "$PWD/kosmos"
python3 "$SRC/tests/orbital_sas_test.py" "$PWD/kosmos"
echo "== timed burn selftest (Shuttle-A in Mars orbit)"
./kosmos --ship "CVEL Shuttle-A" --orbit-body Mars --orbit-alt 200 --selftest 1 --selftest-timed-burn

for t in systems_test reentry_test weather_test joystick_test equipment_test keybindings_test camera_dynamics_test plasma_look_test link_test shipedit_model_test kosmoscad_test missioncore_test launcher_test; do
    echo "== $t"
    ./$t
done

python3 "$SRC/tests/ordnance_fixture.py" "$HOME/test-root"
TEST_ROOT="$HOME/test-root"
echo "== arsenal selftest (test craft: kit, pistol, plasma, charges, blast damage, bomb rack)"
if ! ./kosmos --root "$TEST_ROOT" --ship "Ordnance test craft" --selftest 1 --selftest-arsenal > "$HOME/arsenal_test.log"; then
    cat "$HOME/arsenal_test.log"
    echo "arsenal selftest failed" >&2
    exit 1
fi
cat "$HOME/arsenal_test.log"
if grep -q '^FAIL' "$HOME/arsenal_test.log"; then echo "arsenal selftest failed"; exit 1; fi
echo "== weapons selftest (Deltaglider in orbit: beam, shield, flak, light cap)"
if ! ./kosmos --ship "CVEL Deltaglider" --orbit --selftest 1 --selftest-weapons > "$HOME/weapons_test.log"; then
    cat "$HOME/weapons_test.log"
    echo "weapons selftest failed" >&2
    exit 1
fi
cat "$HOME/weapons_test.log"
if grep -q '^FAIL' "$HOME/weapons_test.log"; then echo "weapons selftest failed"; exit 1; fi

echo "== bombing selftest (regular and atom bomb detonate)"
for atom in "" "--selftest-drop-atom"; do
    if ! ./kosmos --root "$TEST_ROOT" --scenario-file "$TEST_ROOT/ordnance-test.json" --selftest 40 --selftest-drop 2 $atom > "$HOME/bombing_test.log"; then
        cat "$HOME/bombing_test.log"
        echo "bombing selftest failed" >&2
        exit 1
    fi
    if ! grep -q "detonations 1" "$HOME/bombing_test.log"; then
        cat "$HOME/bombing_test.log"
        echo "bombing selftest did not detonate" >&2
        exit 1
    fi
done
if [ "$extended" -eq 1 ]; then
    echo "== extended flight tests"
    python3 "$SRC/tests/orbitcomp_flight_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/autoland_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/target_landing_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/vessel_ascent_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/vessel_ascent_test.py" "$PWD/kosmos" --cargo
    python3 "$SRC/tests/atlas_autopilot_test.py" "$PWD/kosmos"
fi
echo "all tests passed"
