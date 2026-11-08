# Kosmos: notes for AI assistants

Kosmos is a Kerbal/Orbiter-style space flight sim in C++17, built on **simlib** (a separate repo). Read this first in any new session.

## Working rules
- **The owner handles git.** Do not commit or push unless asked. If you do commit, end the message with `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`.
- **Tooling never uses more than 4 threads** (builds with make or ninja, ffmpeg, ImageMagick, the SDK tools such as planetbake and KosmosMap, anything else spawned to do work). Kosmos and simlib's own internal threads are exempt. Pass `-j4` at most; the build scripts clamp it.
- The working tree often has unrelated uncommitted changes (addon edits, deleted scenario files). Leave them alone.
- Keep release notes honest. A line in `docs/release-notes/X.md` must describe something that changed since the previous *published* build. Check `git log` before claiming something is new.
- Docs to keep in step with behaviour: `README.md`, the in-game help text in `src/menu.cpp` (search "F5-F9 MFDs"), and the website under `/var/www/html/kosmos/` (`manual.html`, `index.html`, `modding.html`, `release-notes.html`).

## Layout
- `src/kosmos.cpp` is the entry point; application state is in `src/app_state.h`. `App::run` orchestrates startup (`app_start.cpp`, `app_world_setup.cpp`, `app_ui_setup.cpp`) and frames (`app_frame.cpp`). Input, cameras, rendering and windows live in the corresponding `app_*.cpp` files. See the README's source layout for details.
- `src/command_line.cpp` parses options; `src/selftest.cpp` contains self-tests. Check `src/keybindings.cpp`, `src/app_input.cpp` and the in-game help for current controls rather than relying on the historical header comment in `src/kosmos.cpp`.
- `src/ship.{h,cpp}` and `src/ship_loader.cpp`: ship specs and `ship.json` loading; `src/ship_controls.cpp` handles ship controls. `src/sim.cpp`: simulation and property-driven controls.
- `src/mfd.{h,cpp}`: MFD units and modes (Lua modes from addons). `src/menu.{h,cpp}`: imgui menu bar and windows.
- `src/galaxy.cpp`: star catalogue and procedural systems (`data/systems/*.json` for hand-built ones). `src/planet_data.cpp`: planet imagery and elevation tiles.
- `addons/<id>/`: ships and instruments (`addon.json`, `ship.json`, models, panels). `scenarios/`: the shipped scenarios.
- `launcher/`: wxWidgets launcher and its data layer. `sdk/`: KosmosLink and related tools. `tests/`: regression tests.
- simlib lives at `~/Documents/Projects/rsimlib`. The Orbiter source used for reference is at `~/Documents/Projects/orbiter-main`.

## Build and run
- Dev build: `cmake --build build -j4` (when the `build/` tree is configured). See the README for initial configuration and simlib dependencies.
- Release builds: `sh scripts/release.sh -j4` builds Linux and Windows packages sequentially but does not publish them. `python3 scripts/publish-github-release.py` builds, tests, verifies and publishes a release; see the README's Versions section for prerequisites, scenery reuse and recovery. The scripts check the simlib checkout against `SIMLIB_COMMIT`; this does not verify installed libraries.
- Releases, including game archives and optional scenery, are hosted on GitHub; the website and manuals remain on Salyut. See the README's Versions section for the required release assets. `scripts/publish-web.sh --stamp VERSION` only stamps website version labels; it does not publish archives or bump `VERSION`.
- Run from `build/`. Useful options: `--ship NAME`, `--orbit`, `--cockpit`, `--cockpit3d`, `--set /path=VALUE`, `--hide-info`, `--selftest SECS` (with `--selftest-throttle`, `--selftest-gear-up`), `--test-jump N`, `--system N`, `--orbit-body auto`.
- For off-screen screenshots and tests, use an isolated configuration directory and `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LP_NUM_THREADS=4`; see `tests/earth_cloud_distance_test.py` for an example. Avoid opening windows on the user's desktop.
- Tests: `sh scripts/run-tests.sh` runs the quick regression suite; add `--extended` for longer flight tests. Individual tests such as `build/systems_test` can be run separately.
- MFD manual screenshots: `sh scripts/make-mfd-shots.sh [OUTDIR] [RUN...]` takes them with `--shot` and crops each display; copy the PNGs to `/var/www/html/kosmos/img/mfd/`. Not covered yet: Sync Orbit and Docking (they need a target in the same orbit frame and a tuned IDS beacon).

## Things that have bitten before
- OBJ model parts are named by the **`o` object name**, falling back to the material (`usemtl`) name when the file has no `o` lines. `g` is ignored. Animations and visibility rules match either the part name or its material name.
- Anything that touches OpenGL (`sl::destroy_bitmap`, texture creation) must run on the main thread. Terrain worker threads must not free GPU-backed bitmaps.
- Aircraft ships get the atmosphere-only MFD filter unless the cockpit sets `"mfd_filter": "none"`.
- Panel instruments can be listed in any order. The first two are the ones F6-F9 drive, which is not always left then right on screen.
- A ship's `drag_area` is applied as quadratic drag in air; a value that is too high stops a ship accelerating (the Deltaglider needed 4, the Skylark uses 0.8).
- Editing files with heredocs from the shell is refused in this environment; use the edit/create tools.
