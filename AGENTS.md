# Kosmos: notes for AI assistants

Kosmos is an Kerbal/Orbiter-style space flight sim in C++17, built on **simlib** (a separate repo). Read this first in any new session.

## Working rules
- **The owner handles git.** Do not commit or push unless asked. If you do commit, end the message with `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`.
- **Tooling never uses more than 4 threads** (builds with make or ninja, ffmpeg, ImageMagick, the SDK tools such as planetbake and KosmosMap, anything else spawned to do work). Kosmos and simlib's own internal threads are exempt. Pass `-j4` at most; the build scripts clamp it.
- The working tree often has unrelated uncommitted changes (addon edits, deleted scenario files). Leave them alone.
- Keep release notes honest. A line in `docs/release-notes/X.md` must describe something that changed since the previous *published* build. Check `git log` before claiming something is new.
- Docs to keep in step with behaviour: `README.md`, the in-game help text in `src/menu.cpp` (search "F5-F9 MFDs"), and the website under `/var/www/html/kosmos/` (`manual.html`, `index.html`, `modding.html`, `release-notes.html`).

## Layout
- `src/kosmos.cpp`: the app, main loop, input, cameras, rendering, `--selftest`. Keys are listed in the header comment.
- `src/ship.{h,cpp}`: ship specs (`ship.json` loader), thrusters, gear, cockpit config. `src/sim.cpp`: simulation, controls from properties.
- `src/mfd.{h,cpp}`: MFD units and modes (Lua modes from addons). `src/menu.{h,cpp}`: imgui menu bar and windows.
- `src/galaxy.cpp`: star catalogue and procedural systems (`data/systems/*.json` for hand-built ones). `src/planet_data.cpp`: planet imagery and elevation tiles.
- `addons/<id>/`: ships and instruments (`addon.json`, `ship.json`, models, panels). `scenarios/`: the shipped scenarios.
- simlib lives at `~/Documents/Projects/rsimlib`. The Orbiter source used for reference is at `~/Documents/Projects/orbiter-main`.

## Build and run
- Dev build: `cmake --build build -j8` (the `build/` tree is already configured).
- Linux package: `cmake --build build --target kosmos_package`. Windows package: `sh scripts/build-windows.sh` (clean rebuild, about 5 minutes).
- Publish: `sh scripts/publish-web.sh build/package/kosmos-beta-$(cat VERSION)-Linux.tar.gz build-win/package/kosmos-beta-$(cat VERSION)-Windows.zip`. It needs `docs/release-notes/<VERSION>.md`, copies to `/var/www/html/kosmos/download/`, stamps versions and bumps `VERSION`. It does nothing if the checksums match.
- Run from `build/`. Useful options: `--ship NAME`, `--orbit`, `--cockpit`, `--cockpit3d`, `--set /path=VALUE`, `--hide-info`, `--selftest SECS` (with `--selftest-throttle`, `--selftest-gear-up`), `--test-jump N`, `--system N`, `--orbit-body auto`.
- Screenshot: `DISPLAY=:0 ./kosmos --hide-info --ship "CVEL Deltaglider" --orbit --cam-yaw 70 --cam-pitch 10 --cam-distance 20 --shot FILE.png --shot-at SECS`.
- Tests: `build/systems_test`.

## Things that have bitten before
- OBJ model parts are named by **material** (`usemtl`), not by `g` group. To animate a group separately, give it its own material in the `.obj` and `.mtl`, then use that name in the animation JSON.
- Anything that touches OpenGL (`sl::destroy_bitmap`, texture creation) must run on the main thread. Terrain worker threads must not free GPU-backed bitmaps.
- Aircraft ships get the atmosphere-only MFD filter unless the cockpit sets `"mfd_filter": "none"`.
- Panel instruments can be listed in any order. The first two are the ones F6-F9 drive, which is not always left then right on screen.
- A ship's `drag_area` is applied as quadratic drag in air; a value that is too high stops a ship accelerating (the Deltaglider needed 4, the Skylark uses 0.8).
- Editing files with heredocs from the shell is refused in this environment; use the edit/create tools.
