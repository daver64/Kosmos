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
python3 "$SRC/tests/github_release_test.py"
python3 "$SRC/tests/page_report_test.py"
python3 "$SRC/tests/scenario_addons_test.py"
python3 "$SRC/tests/station_assets_test.py"
python3 "$SRC/tools/msh_import.py" --selftest
python3 "$SRC/tests/deltaglider_panel_assets_test.py"
python3 "$SRC/tests/shuttle_vc_textures_test.py"
python3 "$SRC/tests/salyut_panels_test.py"
python3 "$SRC/tests/sev_assets_test.py"
python3 "$SRC/tests/ares_legs_test.py"
python3 "$SRC/tests/proton_exhaust_test.py"
python3 "$SRC/tests/soyuz_exhaust_test.py"
python3 "$SRC/tests/vessel_ascent_limits_test.py"
nice cmake --build "$OUT" -j "$JOBS" --target kosmos performance_metrics_test systems_test ship_lights_test orbiter_tree_lookup_test orbiter_microtexture_test orbital_relief_guard_test orbiter_zip_test reentry_test weather_test joystick_test equipment_test synth_sound_test keybindings_test camera_dynamics_test rcs_visual_test landing_plane_test plasma_look_test link_test shipedit_model_test kosmoscad_test missioncore_test launcher_test
python3 "$SRC/tests/deltaglider_takeoff_test.py" "$OUT/kosmos"
python3 "$SRC/tests/buran_flight_test.py" "$OUT/kosmos"
python3 "$SRC/tests/exhaust_render_test.py" "$OUT/kosmos"
python3 "$SRC/tests/camera_soi_render_test.py" "$OUT/kosmos"
python3 "$SRC/tests/launcher_ground_relief_test.py" "$OUT/launcher_test"
python3 "$SRC/tests/mfd_overlay_layout_test.py" "$OUT/kosmos"
python3 "$SRC/tests/mfd_map_texture_test.py" "$OUT/kosmos"
python3 "$SRC/tests/stellar_light_render_test.py" "$OUT/kosmos"
python3 "$SRC/tests/stellar_glare_render_test.py" "$OUT/kosmos"
python3 "$SRC/tests/planet_texture_selection_test.py" "$OUT/kosmos"
python3 "$SRC/tests/close_orbit_haze_test.py" "$OUT/kosmos"
python3 "$SRC/tests/distant_earth_cloud_depth_test.py" "$OUT/kosmos"
cd "$OUT"
export HOME=$(mktemp -d)
export XDG_CONFIG_HOME="$HOME/.config"
trap 'rm -rf "$HOME"' EXIT

python3 "$SRC/tests/scenario_start_test.py" "$PWD/kosmos"
python3 "$SRC/tests/planet_moon_scenarios_test.py" "$PWD/kosmos"
python3 "$SRC/tests/warp_atmosphere_test.py" "$PWD/kosmos"
python3 "$SRC/tests/nasa_assets_test.py" "$PWD/kosmos"
python3 "$SRC/tests/rover_rollover_test.py" "$PWD/kosmos"
python3 "$SRC/tests/entrance_label_settings_test.py" "$PWD/kosmos"
python3 "$SRC/tests/star48_stack_test.py" "$PWD/kosmos"
echo "== Energia parallel ignition and staging"
./kosmos --scenario buran_energia_baikonur --selftest 1 --set /selftest/energia-liftoff=true
./kosmos --scenario buran_energia_baikonur --selftest 1 --set /selftest/energia-autopilot=true
python3 "$SRC/tests/centaur_handoff_test.py" "$PWD/kosmos"
python3 "$SRC/tests/scenario_save_test.py" "$PWD/kosmos"
python3 "$SRC/tests/gravity_j2_test.py" "$PWD/kosmos"
python3 "$SRC/tests/flight_recording_test.py" "$PWD/kosmos"
python3 "$SRC/tests/sync_approach_test.py" "$PWD/kosmos"
python3 "$SRC/tests/gui_input_test.py" "$PWD/kosmos"
python3 "$SRC/tests/orbital_sas_test.py" "$PWD/kosmos"
echo "== timed burn selftest (Shuttle-A in Mars orbit)"
./kosmos --ship "CVEL Shuttle-A" --orbit-body Mars --orbit-alt 200 --selftest 1 --selftest-timed-burn
echo "== landing plane prediction (Ares in Mars orbit)"
./kosmos --ship "Ares Spider" --orbit-body Mars --orbit-alt 200 --selftest 1 --set /selftest/landing-plane=true
echo "== landing plane prediction (Ares in Moon orbit)"
./kosmos --ship "Ares Spider" --orbit-body Moon --orbit-alt 200 --selftest 1 --set /selftest/landing-plane=true --set /selftest/landing-plane-base=tranquility
echo "== landing plane prediction (Shuttle-A in Earth orbit)"
./kosmos --ship "CVEL Shuttle-A" --orbit-body Earth --orbit-alt 200 --selftest 1 --set /selftest/landing-plane=true --set /selftest/landing-plane-base=kourou
echo "== finite landing plane burns (Ares on Mars and Moon)"
./kosmos --ship "Ares Spider" --orbit-body Mars --orbit-alt 200 --selftest 1 --set /selftest/landing-plane=true --set /selftest/landing-plane-burn=true
./kosmos --ship "Ares Spider" --orbit-body Moon --orbit-alt 200 --selftest 1 --set /selftest/landing-plane=true --set /selftest/landing-plane-base=tranquility --set /selftest/landing-plane-burn=true
./kosmos --ship "CVEL Shuttle-A" --orbit-body Earth --orbit-alt 200 --selftest 1 --set /selftest/landing-plane=true --set /selftest/landing-plane-base=kourou --set /selftest/landing-plane-burn=true

for t in performance_metrics_test systems_test ship_lights_test orbiter_tree_lookup_test orbiter_microtexture_test orbital_relief_guard_test orbiter_zip_test reentry_test weather_test joystick_test equipment_test keybindings_test camera_dynamics_test rcs_visual_test landing_plane_test plasma_look_test link_test shipedit_model_test kosmoscad_test missioncore_test launcher_test; do
    echo "== $t"
    ./$t
done
echo "== synth_sound_test"
SDL_AUDIODRIVER=dummy ./synth_sound_test

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
    echo "== Soyuz deorbit and entry (untargeted)"
    python3 "$SRC/tests/soyuz_countdown_test.py" "$PWD/kosmos"
    ./kosmos --scenario soyuz_tma_orbit --selftest 1 --set /selftest/soyuz-entry=true
    ./kosmos --scenario soyuz_tma_orbit --selftest 1 --set /selftest/soyuz-entry=true --set /selftest/soyuz-entry-coast=500 --set /selftest/soyuz-entry-step=0.02
    python3 "$SRC/tests/soyuz_autoland_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/orbitcomp_flight_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/autoland_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/target_landing_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/orbital_target_landing_test.py" "$PWD/kosmos"
    echo "== experimental orbital descent probes (not the player-facing autopilot)"
    ./kosmos --ship "Ares Spider" --orbit-body Mars --orbit-alt 200 --selftest 1 --set /selftest/landing-plane=true --set /selftest/landing-plane-burn=true --set /selftest/landing-plane-descent=true --set /selftest/landing-shadow=true --set /selftest/landing-plane-lead=0.48 --set /selftest/landing-plane-orbits=14
    ./kosmos --ship "Ares Spider" --orbit-body Moon --orbit-alt 200 --selftest 1 --set /selftest/landing-plane=true --set /selftest/landing-plane-base=tranquility --set /selftest/landing-plane-burn=true --set /selftest/landing-plane-descent=true --set /selftest/landing-shadow=true
    ./kosmos --ship "CVEL Shuttle-A" --orbit-body Earth --orbit-alt 200 --selftest 1 --set /selftest/landing-plane=true --set /selftest/landing-plane-base=kourou --set /selftest/landing-plane-burn=true --set /selftest/landing-plane-descent=true --set /selftest/landing-shadow=true --set /selftest/landing-plane-lead=0.42
    python3 "$SRC/tests/vessel_ascent_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/vessel_ascent_test.py" "$PWD/kosmos" --cargo
    python3 "$SRC/tests/atlas_autopilot_test.py" "$PWD/kosmos"
    python3 "$SRC/tests/cveln1_test.py" "$PWD/kosmos"
fi
echo "all tests passed"
