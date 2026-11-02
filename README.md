# Kosmos

An Orbiter/Kerbal-style space simulator built on simlib. Fly from a launch pad to orbit, transfer to the Moon, and jump to other stars.

## Build and run

Kosmos builds against an installed [simlib](https://github.com/daver64/rsimlib) (`cmake --install` of rsimlib; pinned to the commit in `SIMLIB_COMMIT`; the build scripts check your simlib checkout against it, and the pin is only moved after simlib itself changes). Needs a C++17 compiler, CMake 3.28+ and simlib's dependencies; the Linux command below uses Ninja.

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/opt
    cmake --build build --target kosmos
    ./build/kosmos [options]

Use `-DKOSMOS_SIMLIB_DIR=/path/to/rsimlib` to build against a simlib source checkout instead.

### Native Windows build (MSVC)

With the MSVC C++ tools, a neighboring simlib checkout at the commit in `SIMLIB_COMMIT`, and simlib's dependencies (including wxWidgets for the launcher) installed in vcpkg's `x64-windows` triplet, configure and build the default targets:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake -S . -B build-msvc -G "Visual Studio 18 2026" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DKOSMOS_SIMLIB_DIR=..\rsimlib `
  -DSIMLIB_RENDERERS=gl -DSIMLIB_BUILD_SHADER_COMPILER=OFF -DSIMLIB_VALIDATE_SHADERS=OFF
cmake --build build-msvc --config Release --parallel 4
```

Use the Visual Studio generator name matching the installed version. The default build includes `kosmos`, `kosmos-launcher` and `planetbake`; optional GTK-based tools are not part of this native Windows CMake build. The Windows package script below remains the MinGW cross-build.

### Source layout

`src/kosmos.cpp` is only `main`. The application is the `App` struct in `src/app_state.h` (its members are the state that used to be `main`'s locals); `App::run` calls the start-up steps in `app_start.cpp`, `app_world_setup.cpp` and `app_ui_setup.cpp`, then one frame at a time from `app_frame.cpp`, whose phases live in `app_input.cpp` (events, keys), `app_camera.cpp` (time, camera), `app_draw_world.cpp` (the 3D scene; its lighting, sky, bodies, ships, weather and effects passes are in `app_scene.cpp`), `app_draw_hud.cpp` (overlays, sound, HUD) and `app_gui.cpp` and `app_windows.cpp` (menus and the tool windows). The pieces that need no shared state are modules of their own: `scenario_start`, `body_terrain`, `surface_textures`, `cockpit_panels`, `ship_visuals`, `exhaust_fx`, `map_views`, `settings`, `procedural`, `engine_sound`, `selftest` and `command_line`.

### Tests

`sh scripts/run-tests.sh` builds and runs everything: the unit tests (`systems_test`, `weather_test`, `joystick_test`, `equipment_test`, `keybindings_test`, `link_test`, which also checks that every locker and hardpoint in the addons names real equipment) and two headless selftests. `kosmos --scenario 05_vulcan_wagga --selftest 1 --selftest-arsenal` checks the pack, pistol, plasma rifle, thrown charges, blast damage and the bomb rack. The simlib commit Kosmos is built against is pinned in `SIMLIB_COMMIT`; the build scripts check your simlib checkout against it (`SIMLIB_ALLOW_MISMATCH=1` overrides). Move the pin only after changing simlib.

## Launcher

`kosmos-launcher` (wxWidgets; on Linux install `libwxgtk3.2`) lists scenarios with previews, toggles addons (`<user dir>/addons.json`), manages texture packs, sets launch options, checks for updates and shows logs, with a "Report this log" button that posts a log (home paths removed, after a preview) to the server's `report/submit.php`. The server side is `report/` (PHP: submit, password-protected `admin.php`, public form), installed once with `scripts/setup-reports.sh [HOST]`; reports are kept in `/var/lib/kosmos-reports`, outside the web root. Previews are `thumbs/<scenario id>.png` beside each scenario JSON; `scripts/make-thumbs.sh` makes them. `scripts/make-packs-json.sh` writes the pack list the launcher downloads from.

## Distributions

    scripts/build-linux.sh                          # build/package/kosmos-beta-<version>-Linux.tar.gz
    scripts/build-windows.sh                        # build-win/package/kosmos-beta-<version>-Windows.zip (mingw-w64 + vcpkg cross-build) (both do a clean rebuild; INCREMENTAL=1 skips the clean)
    scripts/release.sh                              # both builds, then publish to the web site (--no-publish, --linux-only, --windows-only, -j N)
    # builds run at most 4 compile or link jobs at once, even a bare `ninja` (a Ninja job pool); use -j N or JOBS=N with the scripts, or -DKOSMOS_BUILD_JOBS=N with cmake, to change that

Both scripts use the installed simlib under `SIMLIB_PREFIX` (default `~/opt` for Linux, `~/opt-win` for the mingw build); set `KOSMOS_SIMLIB_DIR` to build from a source checkout instead.

`import/orbiter/` (your own Orbiter high-resolution tiles; `import/` holds source material that is never packaged) and `data/textures-hires/` (optional high-resolution surface maps) are git-ignored and never packaged; the repository carries a default texture set in `data/textures/` that works on its own. See the developers manual for installing hi-res data.

## Native addons

Native addons: programs that connect over KosmosLink (loopback only) and are allowed per addon in View > Native addons. The client library, examples and manual are the separate Kosmos SDK (`sdk/`, packaged by `scripts/build-sdk.sh`; the protocol and server are in `src/link/`). `addons/kosmosmap` is the launcher for KosmosMap, a GTK 3 moving map of the body the ship is at (Linux); its source is the SDK's model program, `sdk/kosmosmap`.

**KosmosMap (Linux and Windows).** Tick it in View > Native addons to open a map window: relief or imagery of the body you are at, your ship (yellow), other ships (blue), your track, airports with runways and navaids, and a read-out of position, altitude, heading, speed and the height under the pointer. Drag pans, the wheel and +/- zoom, F follows the ship, C clears the track. It also runs on its own: `kosmosmap --body Moon --lat 10 --lon 20 --ppd 12` (`--imagery`, `--root DIR`, `--no-link`, `--shot FILE.png [--size WxH]`). Usage and internals: `sdk/kosmosmap/README.md`.

**ShipEdit (Linux; `sdk/shipedit`, launcher `addons/shipedit`).** A GTK 3 editor for ships and rocket stacks, started from the Native addons list or as `addons/shipedit/bin/shipedit`. Pick an addon and ship and load it; the tree shows Ship > Stage > parts, engines and gear. Add, delete, duplicate and reorder stages; choose the mesh (OBJ or GLB) or generated prop of each part; edit stage mass, propellant (with a slider), starting fuel (`fuel_start`, 0 to 1 of the tank), Z, length and radius, engine thrust, Isp and placement (Offset, Rotate and Position fields have nudge sliders; one drag is one undo step), and the ship's class, drag and gear; every node also has a raw JSON box, and any key the editor does not know (cockpit, systems, animations, scripts) is written back untouched. A live preview (software drawn with cairo, no OpenGL) shows the stack: drag to orbit, middle or right drag (or Shift+drag) to pan, wheel to zoom at the pointer, with Front, Back, Left, Right, Top, Bottom and Iso view buttons and Fit, and a table gives each stage's mass, thrust, Isp, delta-v and thrust-to-weight, with a problems list (missing models, engines outside their stage, duplicate ship name). **Save** writes the ship back into the addon it came from, after copying the whole addon to `shipedit-backups/<id>-<time>/` beside the addons folder. **Save as new addon** instead writes a new addon folder (a copy of the source with the edited ship, a new id and name, and any model files you pulled in) and leaves the source alone. `shipedit --addon ID --shot FILE.png` draws the preview without a window.

**KosmosCAD (Linux and Windows; `sdk/kosmoscad`, launcher `addons/kosmoscad`).** A GTK 3 3D modeller whose four views (Top, Front, Side and a perspective view) are all OpenGL; nothing is drawn with cairo. Open a `ship.json` (or a `.kmodel` scene) with `addons/kosmoscad/bin/kosmoscad FILE`, or press **Model in KosmosCAD** in ShipEdit. Object, Vertex, Edge and Face modes with click and box picking; Move, Rotate and Scale gizmos; add box, cylinder, cone, sphere, torus and plane, sketch extrude, loft and lathe; extrude, inset, split edge, weld, flip and delete faces; mirror; boolean union, subtract and intersect; per-face materials (the object name, written as an OBJ `o` line, is the animatable part name in Kosmos), UV projection, a textured view (Z cycles wire, solid and textured; PNG, JPG and Orbiter DDS) that also shows each material's normal map, metallic/roughness and emissive settings, a Blender-style UV editor (U: slide, rotate and scale the mapping of the selected faces over the material's image) View > Render... (a window with Save), mesh import (Orbiter .msh, glTF/GLB, OBJ) and an Edit-in-KosmosCAD choice next to Blender in the part and base editors (`--edit-mesh FILE`; Ctrl+S sends the mesh back to the sim); undo and redo. Engines, gear, tanks, ISRU, reactors and pressure vessels are draggable markers with property forms. Scenes save as `.kmodel` and export OBJ+MTL (texture paths are relative to the model's folder, and images from elsewhere are copied into a `textures` folder beside it); saving a `ship.json` keeps a `ship.json.bak`. `kosmoscad FILE --shot OUT.png --size 1280x800` renders without interaction. File > Close file (Ctrl+W) clears the editor; opening a JSON that is not a ship.json shows a warning. A manual and tutorial are on the website (`kosmoscad.html`). The modelling core is tested headless: `cmake --build build --target kosmoscad_test && build/kosmoscad_test`.

## Controls

The keys below are the defaults. View > Keyboard rebinds any of them (saved in `keys.json` in the config folder); addons can add bindable actions with a `keys.json` of their own, see the modders manual.

| Key | Action |
|---|---|
| Keypad + / Keypad * | Full throttle / cut throttle |
| Ctrl+Keypad + / Ctrl+Keypad - | Ramp the throttle up / down while held |
| Keypad 2 8 / 1 3 / 4 6 | Pitch / yaw / roll, as in Orbiter |
| Arrow keys | RCS translation |
| Joystick | View > Joystick: bind axes and buttons separately for aircraft and spacecraft (saved in `joystick.json`) |
| Space | Stage |
| Keypad / , Ctrl+Keypad / | RCS rotation / linear mode, RCS off (linear: Keypad 2 8 up/down, 1 3 left/right, 6 9 forward/back; Ctrl for fine) |
| Keypad 7 / Insert | Fire primary / secondary weapons |
| Keypad 5 / `[` / `]` / `;` / `'` | SAS: kill rotation / prograde / retrograde / normal / anti-normal (as Orbiter's navigation modes) |
| View > Camera | Standard Camera or Dynamic Camera (spring-mounted cockpit eye that lags under acceleration) |
| X / Z, Ctrl+X / Ctrl+Z | Widen / narrow the field of view while held / by a step (10 to 90 degrees), as in Orbiter |
| Ctrl+P / Ctrl+C / Ctrl+Q | Pause / start or stop video recording / quit |
| P | Autopilot (aircraft: hold heading, altitude and thrust; rockets: ascent, for the Kestrel and the CVEL Soyuz-FG, Delta IV Heavy, Atlas III and Saturn V). The Autopilot menu has an Aircraft and a Spacecraft window |
| R / T | Time warp down / up (Shift+R restarts the scenario) |
| `,` | Wheel brakes (also B) |
| Ctrl+M (or Tab) | Orrery map (F: focus, `-` `=` zoom, right-drag rotate) |
| **G** | Toggle landing gear (also **L**) |
| **N** | **Galaxy map** (Left/Right choose star, `-` `=` zoom, **Enter** jump) |
| Arrows, PgUp/PgDn | In the cockpit view: move your head forward/back, left/right, up/down |
| **Enter** | **Leave the ship / board a ship** within 30 m (also View > Leave ship). On foot: W A S D walk, either Shift boosts, Space jumps, Ctrl crouches, the mouse looks (press ` or hold Alt to free the pointer for the menus), arrows also look, F manoeuvring pack, Q/E roll when floating |
| C | Cycle cameras: chase, 2D cockpit, 3D cockpit, then your own cameras (an orbiting External camera is built in) |
| **F2** | **Camera window**: pick any ship, cockpit, interior or base camera |
| **F3** | **Settings window**: body/ship labels (off by default), orbit line, key help, sound, volume, time warp. Saved to `~/.config/kosmos/settings.json` on exit. **Edit > Performance settings** (no key) holds the graphics, weather (including the cloud style: simple deck, layers or volumetric), scenery and texture options |
| **F4** | **Property browser**: browse, filter, read and write the property tree (like FlightGear). Typing in its boxes does not fly the ship |
| **F11** | **Lua console** (like FlightGear's Nasal console): edit scripts with syntax highlighting, **Run** (Ctrl+Enter), **Save** (Ctrl+S, to `~/.config/kosmos/scripts/`). Choose the event that runs a script: manual, scenario load / ship change, every frame, timer, or when a property changes, and tick **Armed**. Scripts see `props` plus `event`, `dt`, `event_path`, `event_value`; `print` and errors go to the output pane. Examples live in `data/scripts/` |
| **F12** | **Multiplayer** window: host or join a game, browse the IRC lobby, chat |
| **Menu bar** | A **File / Edit / View / Tools / Help** bar is pinned along the top (untick View > *Keep menu bar visible* and it slides in only when the mouse touches the top edge). **File**: open a scenario (built-in list or a `.json` file), *Save scenario* (captures every ship's live position; orbits are saved as circular), restart, quit. **Edit**: undo/redo for the open editor, Settings, Properties. **View**: every window plus both editors. **Tools**: *Scenario editor*, *Base editor*, screenshot, *Start/Stop recording video* (H.264 + AAC sound, `kosmos-<time>.mp4` in the working directory, needs `ffmpeg` on the path), reload user content. **Help**: controls, about. **Esc** closes menus and windows first and quits only when none are open |
| , / F | Wheel brakes / cycle flaps (aircraft) |
| G | Landing gear up / down (retractable gear) |
| Ctrl+D | Undock |
| Home | Reset the cockpit view direction to forwards (as in Orbiter) |
| Ctrl+I | Show or hide all on-screen text |
| Left mouse drag | On a 2D cockpit panel or an MFD, hold the left button on anything that is not a control and move the mouse to reposition it |
| Delete / Shift+Delete | Deploy / cut parachutes (ships with a `parachutes` block) |
| Y / Shift+Y | Navigation lights / landing and nose lights on or off (also under View) |
| V | Switch ship |
| O | Orbit line |
| I | Show or hide all on-screen text (title, telemetry, nearest base, key help); hidden when Kosmos starts |
| M | Sound on/off (rocket, RCS and ground wind) |
| R | Reset |

## Command line

| Option | Meaning |
|---|---|
| `--system <index\|name>` | Start in a galaxy star system (0 / "Sol" is the hand-authored one) |
| `--render-style normal\|flat\|wireframe` | Shading style (flat and wireframe are much cheaper on slow or software graphics) |
| `--stars` | Print the 20 nearest stars and exit |
| `--galaxy-map` | Start in the galaxy map |
| `--orbit`, `--orbit-body B`, `--orbit-alt KM` | Start in orbit (`--orbit-alt` also lifts a scenario's circular orbits) |
| `--sunlit`, `--sun-angle DEG` | Start on the Sun side of the planet; with `--sun-angle`, DEG round from there (about 70 gives a planet two thirds lit) |
| `--max-texture N` | Largest body texture accepted (default 16384; halved until the GPU accepts it) |
| `--gl`, `--vulkan`, `--d3d12` | Choose the graphics backend (D3D12 is Windows only). Vulkan prefers a discrete GPU (`SIMLIB_VULKAN_DEVICE_TYPE=discrete\|integrated\|any` overrides); the chosen GPU is printed at startup |
| `--user-camera N`, `--camera-editor` | Start looking through the Nth user camera, or open the Camera editor (View menu) |
| `--selftest SECS`, `--autopilot` | Headless test run |
| `--shot FILE`, `--shot-at SECS` | Save a screenshot (`scripts/make-gallery.sh` uses it to make the web gallery's JPEGs; `scripts/make-promo.sh` records the showreel) |
| `--alone` | With `--orbit`, leave out the other addon ships that normally share the orbit |
| `--cam-tilt DEG` | Nod the chase camera up and down by up to DEG degrees (for video) |
| `--cam-dolly M` | Move the chase camera away from the ship at M metres per second (negative moves in), for video |
| `--cam-spin DEG` | Swing the chase camera round the ship at DEG degrees per second (for video) |
| `--record SECS` | Record a video from the start for SECS seconds, then quit |
| `--auto-stage` | Scripted ascent for video: pitch over at half the first stage's propellant, drop each empty stage and light the next 5 s later (and return to 1x warp) |
| `--drop SECS COUNT INTERVAL` | Release COUNT bombs from the first hardpoint, one every INTERVAL seconds from SECS (a scripted ripple drop for video) |
| `--launch-at SECS` | With `--auto-stage`, hold the throttle at zero until SECS, then light the engines with a thump and ramp thrust up over 3 s |
| `--scenario-file PATH` | Start from a scenario file outside the scenario folders |
| `--set-at SECS PATH=VALUE` | Set a property once SECS into the run (repeatable) |
| `--record-after-stage SECS` | With `--record`, end the video SECS after the first staging instead of at the `--record` time |
| `--host [PORT]` | Host a multiplayer game (default UDP port 7777) |
| `--join HOST[:PORT]` | Join a hosted game |
| `--name NAME` | Your name in multiplayer and the lobby |
| `--lobby` | Open the Multiplayer window at start |
| `--irc HOST`, `--channel #NAME`, `--advertise ADDR:PORT` | Lobby IRC server (default `salyut.ddns.net`, `#kosmos`) and the address you announce |
| `--no-clutter` | Leave out trees, buildings, rocks and outposts on the ground |
| `--perf` | Print frame rate and ground-scatter CPU time every few seconds |
| `--no-clouds` | Skip the cloud layers (faster start, helps on weak GPUs) |
| `--show-info` | Start with the on-screen text (title, telemetry, key reminders) showing; it is hidden by default (toggle with `I`). The bottom warp and time line is separate (View menu) |
| `--clean` | Like `--hide-info`, and also hides the on-foot HUD (suit panel, door markers, prompts, log lines, held item) for promotional shots |
| `--interior-at X Z YAW PITCH` | After walking into an interior building, stand at this spot (metres in the plan, degrees) |
| `--console` | Windows only: open a console window for the output when Kosmos was not started from a terminal (it is a GUI program and shows none by default) |
| `--hide-info` | Hide the on-screen text and the orbit line and let the menu bar slide away (for screenshots); overrides `--show-info` |
| `--life-support off\|warn\|fatal` | What failing life support does to a crew: nothing, alarms only (default), or the crew dies and the flight ends. A scenario's `life_support` key overrides it |
| `--hud MODE` | Start with the head-up display in `orbit`, `surface` or `docking` mode (`H` cycles, `Alt+H` changes colour; shown in cockpit views only). Aircraft offer only the `surface` HUD unless the cockpit lists others in `"hud_modes"` |
| `--version` | Print the Kosmos version and exit |

## Versions

Versions are `major.build`. The major number is 0 while Kosmos is in beta, and the build number counts published binaries. The [VERSION](VERSION) file holds the number the next published build will carry, so a fresh binary already shows it in **Help > About**, in `--version`, and on the web pages. `scripts/publish-web.sh ARCHIVE...` copies built archives to the web site; if their SHA-256 differs from the published one it updates `SHA256SUMS.txt`, stamps the version on the landing page, download pages and docs, and then bumps `VERSION` to the next build. If the checksums are unchanged nothing happens. Archives carry the version in their name (`kosmos-beta-0.32-Linux.tar.gz`); the publisher removes older versioned archives.

    scripts/publish-web.sh build/package/kosmos-beta-$(cat VERSION)-Linux.tar.gz build-win/package/kosmos-beta-$(cat VERSION)-Windows.zip

## Galaxy

`galaxy.cpp` generates a deterministic catalogue of 24,409 stars: Sol, thirteen real stars at approximate positions (Alpha Centauri, Proxima, Barnard's, Wolf 359, Sirius, Lalande 21185, Procyon, Epsilon Eridani, Tau Ceti, TRAPPIST-1, Vega, Betelgeuse and Rigel), 400 invented nearby stars and a two-armed spiral disc.
Each star gets a procedural planetary system (planets, moons, belts, comets) with one home planet that has Earth-like terrain and atmosphere.
Press **N**, pick a star and press **Enter** to jump; a short hyperspace effect plays while the new system loads (the window stays open), and you arrive in a 200 km orbit around its home planet.

Hand-authored systems live in `data/systems/<name>.json` (same format as `sol.json`; the file name is the star's name in lower case with dashes). Sirius (with its white dwarf companion), Proxima, TRAPPIST-1, Betelgeuse and Rigel ship this way; every other star uses the generator.

Scenarios belong to a star system: a `"system": "Sirius"` entry in the scenario file (empty means Sol) makes starting it jump there first, and the scenario editor records the current system when it saves.

### Quantum jump drive

A ship with an engine of `"type": "hyperjump"` (the Argo has one; `"range_ly"` sets its reach, 50 by default) can use **View > Quantum navigation**: pick a star within range, optionally choose a specific planet or moon in the *Arrive at* box, and press *Engage quantum jump*. With no world chosen the ship arrives in orbit of the best world in the system (an Earth-like planet if there is one, else a Mars- or Venus-like rocky planet, else any rocky planet, else a moon of a gas giant, else the giant itself; `--orbit-body auto` does the same from the command line), and the window reopens at the new system. Ships without the drive see a "no quantum jump drive" notice. `--quantum` opens the window at startup.

### Invented worlds

Every generated planet and moon gets a procedural look from `planet_gen.cpp`: Mercury-like (craters), Venus-like (volcanic plains under haze), Earth-like (continents, oceans, ice, biomes), Mars-like (rust, canyons, volcanoes, caps) or gas giant (banded, with storms; ice giants are the same type with soft bands). `galaxy.cpp` picks the type and its parameters (ocean fraction, ice, relief, tint, bands) from the world's size, temperature and atmosphere; the seed makes it the same for everyone. Rocky worlds also get streaming terrain from the same generator, so what you see from orbit is what you land on. The home planet keeps its Earth-like terrain. Earth-, Venus- and Mars-like worlds also get a procedural cloud layer (thin and pinkish on Mars-like, thick and cream on Venus-like), and `--list-worlds mercury|venus|earth|mars|gas` lists the ones within quantum range.
Nothing is shipped for these worlds. The first time a system is entered, each far-view image is generated (2048x1024, 1024x512 for small bodies; a second or two each) and kept in `~/.cache/kosmos/planets` (`$XDG_CACHE_HOME/kosmos/planets`, or `%LOCALAPPDATA%\kosmos\cache\planets` on Windows). Delete the folder at any time; it is rebuilt on demand.

**Far-view detail.** The default far-view image is 2048x1024 (1024x512 for small bodies). Edit > Performance settings > "World texture", the `/sim/settings/world-texture` property or `--world-texture WIDTH` picks Automatic, 2048, 4096, 8192 or 16384 wide instead, and the change is applied at once (a few seconds for the larger sizes; each size is made once and kept). Measured on a Mars-like world, 4096 took about 2 s and a 34 MB PNG, 8192 about 6 s and 134 MB; 16384 is about four times that again (roughly 540 MB, and as much video memory). The cache keeps every width you have tried, so delete the folder to reclaim the space. Close to the ground the terrain colours come straight from the generator (or from baked tiles, below), so this only changes the view from afar.

`kosmos --system N --gen-world "Name" [--gen-width 16384] [--gen-threads 4] [--gen-memory MB]` makes one world's far-view image into the cache from the command line, so the game finds it ready. It works in horizontal strips on at most 4 threads (the default and the limit) and 4 GB (`--gen-memory`, 512 to 4096; a 16384 image needs under 1 GB and about a minute). Use it instead of setting World texture to 16384, which makes every world in the system when you arrive. The image is not made again if it is already cached.

`--gen-planet TYPE SEED out.png [WIDTH]` writes a preview image of a type (`mercury venus earth mars gas`) and exits.

#### High-resolution bake (optional)

**For big or many-layer maps use planetbake.** It is a separate program next to KosmosMap (`addons/planetbake/bin/planetbake`, built with the game on Linux and Windows and in the game packages; the window needs GTK 3; the Linux and Windows game packages both have the window. Its source is in the SDK (`sdk/planetbake`; the SDK download has source and ready-made Linux and Windows programs) that bakes a generated world into colour, height, normal, specular, night-lights and cloud maps, as PNG or DDS, as single images (to 16384 wide) or tile pyramids (to 65536), from a GTK 3 preview window or the command line, on at most 4 threads. It uses the same generator, so the maps match the game. `--gen-world` and `--bake-planet` print the planetbake command line for the world they were given. planetbake can also open an existing pack, fill its gaps from pictures or NASA datasets, open a clicked tile in your graphics program (Krita by default) and lock tiles against overwriting. See `sdk/planetbake/README.md`.

For a closer-looking far view, or maps you can open in other tools, pre-generate a tile set for one world:

    kosmos --system 294 --bake-planet "Noranth 294 e" --bake-width 16384
    kosmos --system 294 --bake-planet "Noranth 294 e" --bake-width 65536 --bake-estimate   # only print the size
    kosmos --system 294 --bake-planet "Noranth 294 e" --bake-width 65536 --bake-lazy       # no upfront work, see below

`--system` takes the star number from `--stars`; naming a body that has no generated ground lists the ones that do. The tiles are in Celestia's virtual-texture layout (`levelN/tx_COLUMN_ROW.png`, 512 pixel tiles, a `tiles.ctx` descriptor) under `<cache>/tiles/`, and Kosmos uses them automatically on the terrain wherever they are as fine as the terrain cell. Closer than the finest tile the generator carries on, so the ground never goes blurry. An interrupted bake resumes where it stopped.

**This takes disk space.** Sizes grow four-fold with each doubling of width (the pyramid of lower levels is included; PNG sizes are an upper estimate, smooth oceans compress better than rock):

| `--bake-width` | Map size | Tiles | Disk (about) | Time on 16 threads (about) |
|---|---|---|---|---|
| 4096 | 4k x 2k | 42 | 16 MB | seconds |
| 8192 | 8k x 4k | 170 | 65 MB | seconds |
| 16384 | 16k x 8k | 682 | 260 MB | under a minute |
| 32768 | 32k x 16k | 2,730 | 1 GB | a few minutes |
| 65536 | 64k x 32k | 10,922 | 4 GB | 10 minutes or more |

**Lazy baking** (`--bake-lazy`) writes nothing up front. Instead the terrain makes each tile the first time you fly over it and keeps it, so only the parts of the world you visit use disk, and a second visit reads a tile instead of recomputing the noise. `--bake-width` sets the finest level it will make (tiles that fine are made only where the terrain needs that detail; finer than that the generator answers directly). A lazy world can be completed later with a normal `--bake-planet` run, which skips tiles that already exist.

Per world. A system has 5 to 20 bodies and the galaxy has 24,409 stars, so bake the handful of worlds you care about, not every planet you pass. `--bake-estimate` prints the figure for your width without writing anything.

## Textures

`data/textures/<body>.{dds,png,jpg}` (equirectangular) is the default surface image of a body, built from Celestia's maps so a fresh checkout works. A file of the same name in `data/textures-hires/` (git-ignored) overrides it, so high-resolution maps never touch the repository; `tools/gen_textures.py` generates test maps up to 16384x8192 as BC1 DDS there.
DDS files are uploaded to the GPU still block-compressed (`sl::load_dds`), with the mip chain the generator bakes in: a 16k Earth is about 90 MB of VRAM and a few hundred MB of total process memory, instead of ~1.5 GB for PNGs.
Earth switches from terrain mesh to a textured sphere above 300 km (scaled by body radius for other bodies).
Orbiter's high-resolution `Surf.tree` archives can be converted with `python3 tools/orbiter_import.py <Orbiter folder> --body Earth --level 8`; the tiles are block-copied into one DDS (no decode) and the tree levels become the mip chain.
See `data/textures/README.txt` and the developers manual.

## Property tree

Simulation state is published in simlib's property tree (`sl::PropertyTree`), shared by C++ and Lua, e.g. `/controls/throttle`, `/autopilot/enabled`, `/galaxy/current`, `/galaxy/nearest/star[i]/name`.

## Weather

Planets with an atmosphere get procedural weather from 64 METAR-issuing stations (`src/weather.cpp`, FastNoiseLite); Earth can use live NOAA METARs instead (View > Graphics, fetched at most hourly via WinHTTP on Windows or libcurl on Linux). Wind acts on every ship at its own position. Scenarios can fix the weather with a `"weather"` METAR line, and View > Graphics or the launcher can choose a preset (clear, scattered, overcast, rain, fog, thunderstorm) or a typed METAR. Thunderstorms have lightning with a flash, a bolt and delayed thunder (`src/lightning.cpp`). Wind, rain, cloud cover and visibility appear under `/environment/*`. `data/weather/earth_stations.json` is generated by `tools/pick_weather_stations.py`. Tests: `cmake --build build --target weather_test && build/weather_test`.

## Equipment and ordnance
On foot, keys 1 to 9 pick an item from your pack and the left mouse button uses it: tricorder (live atmosphere, gravity and ground readout), geo scanner and survey theodolite (click to mark points and measure between them), pistol, plasma rifle and thrown demolition charges. In a ship with hardpoints, K drops the next bomb and Shift+K the special store. Bombs fall under gravity; the Fat Man airbursts with a flash, a damaging shock wave and a mushroom cloud. Equipment is JSON in `<addon>/equipment/*.json` (see `addons/field-kit`): `id`, `name`, `kind` (instrument, weapon, explosive, ordnance), `standard` (in every pack) and stats such as `damage`, `ammo`, `fuse`, `blast_radius` and `yield`. A `ship.json` adds `hardpoints` (`name`, `equipment`, `position`, `count`) and a `locker` list of items a person takes when leaving the ship or building. Scenario 14 is a Vulcan bombing run; scenario 21 launches the `cvel-icbm` two-stage missile from Faslane. Its target, burst and yield settings are in the ICBM Targeting MFD and the View > ICBM targeting window. Hardpoints may add `eject` and `eject_speed`; equipment may add `ship` (a ship class released as a real vessel, as the ICBM warhead is). Ships may add `thermal` (nose radius, heat capacity, ablative shield), `hypersonic` (aero coefficients and trim) and `parachutes` blocks for re-entry; falling ordnance can carry `thermal` too.

Ships can carry energy weapons: beams (a pulse that flares and fades, then recharges), pulses, bolts, volleys and flak with neon glow, sparks, impact explosions and capped weapon lights, plus shields. Weapons are JSON in `<addon>/weapons/*.json` and a `ship.json` mounts them with `weapons` and `shield`; keypad 1 and 7 fire the primary and secondary groups, and any ship can fire through `/ships/ship[N]/weapons/<group>/fire`, and KosmosLink programs through the `fire` request (`kl_fire`). `field-kit` provides red, yellow, green and violet beams and sparkles (`beam-red` ... `spark-violet`) in increasing strength; the Deltaglider and the Argo are fitted. `kosmos --ship "CVEL Deltaglider" --orbit --selftest 1 --selftest-weapons` checks beam, shield, flak and the light cap.

## Addons

Ships live in `addons/<name>/` (spec JSON, Lua scripts, optional cockpit). Lua covers scripting, and compiled or other-language addons connect to a running game through KosmosLink (see the SDK manual, `sdk/docs/MANUAL.md`; the old in-process design is kept in [docs/native_addon_roadmap.md](docs/native_addon_roadmap.md) as history).

## Roadmap

Kosmos is feature complete. From 0.30 on, no major new features will be added: releases are bug fixes and performance work on what is already here. The items below are ideas only and are not scheduled.

- Idea: a module block that launches its KosmosLink program with the ship, and more ships carrying weapons.
- Not planned: in-process native modules ([old design](docs/native_addon_roadmap.md), superseded by KosmosLink).

## Engines and exhaust

Each engine in a stage has a `type` and, for rockets, a `fuel`; together they decide the plume.

| `type` | Plume |
| --- | --- |
| `rocket` | by `fuel`: `kerosene` yellow and billowy (default), `hydrogen` longer, tighter and blue-white, `methane`, `hypergolic` |
| `solid` | short, fat and orange |
| `nuclear` | violet glow |
| `scramjet` | blue with shock diamonds; air-breathing unless `"air_breathing": false` is set |
| `jet` | invisible, unless `"afterburner": true` and the throttle is above 80%; air-breathing |
| `hyperjump` | no thrust and no plume (the interstellar jump drive) |

A ship's `camera_target` (`[x, y, z]` in the ship frame, nose along -Z) is the point the external camera orbits and looks at instead of the centre of mass, with `camera_distance` as the range; use it on a tall ship. `"horizontal": true` makes a non-aircraft ship (a rocket-class vessel such as the Shuttle-A) rest level on its `gear` (roof +Y, nose along the base heading) instead of standing nose-up; put the gear at negative Y.

Ship-borne in-situ resource units: an `isru` component with `"site": true` mines water only while landed (yield from a per-body richness table in `ship.cpp`, `surface_richness`; `n2_kg_per_day` and `regen_kg_per_day` add nitrogen and scrubber sorbent), and a `refinery` turns tank water into main propellant. `/ships/ship[N]/systems/processing` switches all of them. `"life_pod": true` in a ship file marks a supply pod: docked, it tops up the host ship's oxygen, nitrogen and water tanks and CO2 sorbent from its own `systems` tanks and scrubber sorbent (the Shuttle-A Life Support Pod). The CVEL Deltaglider (Nuclear) (`addons/cvel-deltaglider-nuclear`, a Deltaglider variant with an 8 kW `fission` reactor, referencing the parent addon's assets with `../cvel-deltaglider/` paths) powers mining anywhere; scenarios 24-26 demonstrate it, the Shuttle-A with life pods, and lunar mining. `"reverse_throttle": true` in a ship's `ship.json` lets the main throttle go negative (down to -1): linear thrusters then fire in reverse, and the Ctrl+Num - ramp and the panel/VC throttle sliders reverse too (the Deltaglider uses it). `"hud_modes"` in a cockpit lists the HUD modes H cycles through. An engine with `"group": "hover"` is driven by a separate hover throttle (`/controls/hover`) instead of the main throttle: Keypad 0 / Keypad . raise and lower it, Ctrl+Home is full hover and Ctrl+End cuts it; the HUD shows `Hover %` on ships that have such engines. An engine with `"group": "pod"` is a swivelling engine pod (the Shuttle-A's two aux pods, on the left and right of the hull, chosen by the sign of `position.x`): `/controls/pod-left` and `pod-right` set their thrust and `/controls/pod-tilt-left` and `pod-tilt-right` (0 aft/retro, 0.5 up/hover, 1 forward) where they point; the pods swing at `pod_turn_rate` rad/s (default 0.2, as in Orbiter) and the live angle is `{ship}/pod-angle-left` and `-right`. Keys on a ship with pods: Keypad 9/3 pod thrust, Keypad 8/2 swing the pods forward/back, Keypad 4/5/6 jump to retro/hover/forward. Model animations can read `pod-left` and `pod-right` (0 to 1). An engine with `"group": "rcs"` is a reaction-control thruster fired by the control groups in its `"rcs_groups"` list (`left`, `right`, `up`, `down`, `forward`, `back`, `pitch-up`, `pitch-down`, `yaw-left`, `yaw-right`, `bank-left`, `bank-right`): it burns its own propellant and makes its own force and torque, and a ship with any such engine does not use the generic `rcs_thrust` jets. The CVEL Dragonfly's 13 hydrogen thrusters work this way. The generic RCS puffs are drawn at the ends of the stage envelope; `"rcs_effects": false` turns them off for a hull that does not reach there (the Ares Spider). A ship file with `"reverse_throttle": true` (the Deltaglider) lets the main throttle go below zero (Ctrl+Keypad - past zero): the negative part fires the linear RCS backwards, to the strength of `rcs_thrust` and drawing RCS propellant, in vacuum or thin air. A ship file with `"rcs_modes": true` gets an RCS mode switch, `/controls/rcs-mode`: `rot` (normal), `lin` (W/S, A/D and Q/E translate instead of rotating) or `off`. An aircraft-class ship with `"hybrid": true` (the Deltaglider) flies as a spaceship, with RCS, SAS and the spacecraft autopilots, wherever the air is thinner than a tenth of Earth's sea-level density or the ship is above the atmosphere, and as an aircraft otherwise; crossing the boundary disengages the autopilot. An aircraft-class ship with hover thrust holds itself level with RCS (SAS not off), since its control surfaces have no airflow to work with. A ship file's `animation_controls` list (`name`, `property`, `duration` seconds, optional `initial`) adds player-triggered animations, such as the Deltaglider's nose hatch: each is listed under View > Animations, set by `/controls/anim/<property>` (0 or 1), eased at the given duration, and read by model animations through the same `property` (0 to 1). A panel `toggle` control on that property gives it a button.

`plume` is the plume length in metres; `plume_width` and `plume_glow` (default 1) scale its width and brightness, for a very big engine. `plume_density` (default 1) scales how many particles it spawns, so a big plume looks smooth rather than dotty. `plume_billow` (0 to 1, default 0) keeps a hydrogen plume full and soft in vacuum instead of going tight and clean; the shock diamonds stay.

A rocket's `isp` is its vacuum value. Add `"isp_sl"` (sea-level Isp) and the engine behaves as in Orbiter: `thrust` is the vacuum thrust, propellant flow stays constant, and thrust and Isp fall toward `isp_sl` as the air thickens. Without `isp_sl` the Isp is the same everywhere.

A ship can carry its own engine sound: `"engine_sound": "sounds/engine.wav"` names a looping file in the ship's folder (the Piper Cub does this); its volume follows the throttle. A `parachutes` list (see `addons/davemecury1`, `addons/cvel-soyuz/descent.json` and `addons/cvel-saturn-v/command_module.json`) gives each chute `position`, `cd_area`, `reef_cd_area`, `auto_altitude` and `auto_speed`; adding `model` (an OBJ) and `model_radius` draws the canopy, scaled to `canopy_radius`. SAS is switched off while a chute is out.

Specs that only set `"air_breathing": true` still work and count as jets. `--shot-throttle X` holds a throttle during a `--shot`, for exhaust pictures.

## Importing Orbiter add-ons

`python3 tools/orbiter_addon_import.py <add-on root> --id <id>` starts a whole addon: it converts the meshes and drafts
`ship.json` from the add-on's C++ source, leaving `"TODO"` where it can't tell. See `docs/importing-orbiter-addons.md`.

`--props` writes the props-library frame (+Z forward, +X left) for base `model` objects instead; the stock Orbiter meshes in `data/parts/orbiter/` were made that way (`parts/orbiter/vab.obj` in a base). `python3 tools/msh_import.py addons/<name>` converts every `.msh` under `addons/<name>/orbiter/Meshes` to OBJ/MTL in
`addons/<name>/models/` (DDS textures become PNG; needs Pillow). The mesh is mirrored from Orbiter's left-handed, nose +Z frame
into Kosmos's nose -Z frame, so it can be used as is: a part of `{"model": "models/x.obj"}` in a stage's `parts` draws it,
with optional `offset`. `./kosmos --ship "Ares Spider"` starts with that ship. Orbiter keeps masses, thrust and fuel in C++
(often in headers that are not part of the add-on download), so `ship.json` files for imported add-ons are written by hand;
`addons/ares` is the worked example.

## MFDs (multi-function displays)

Press **F5** to cycle the MFD display: off, two framed panels (bottom of the screen, clickable buttons), or heads-up (shapes and text only, no background). In the 2D cockpit the MFDs are the dashboard to start with, and F5 steps dashboard, heads-up, off. **F6/F7** cycle the left/right mode, **F8/F9** toggle power. `--mfd 0|1|2`, `--mfd-left ID`, `--mfd-right ID` set them at launch.

Modes are Lua addons. An addon.json lists them:

```json
{"id": "mymfd", "mfds": [{"id": "fuel", "script": "fuel.lua"}]}
```

A script returns a table: `name`, `buttons` (12 labels, left column then right), `init(self)`, `update(self, dt)`, `draw(self, g, w, h)` and `button(self, i)` (1-based). Each MFD gets its own `self` table. `g` draws in a 400x400 box: `g:colour(r,g,b)`, `line`, `rect`, `fillrect`, `circle`, `disc`, `tri`, `arc(cx,cy,r,deg0,deg1)` `g:text(x,y,s,align)` with align 0 left, 1 centre, 2 right, and `g:image(file,cx,cy,w,h,angle,sx,sy,sw,sh)` for a PNG from the mode's folder (rotated, optionally a source region). `addons/instruments` has an Attitude Indicator and an Altimeter built from bitmaps (`tools/make_instrument_bitmaps.py` draws the placeholders; replace them with real artwork of the same size). Scripts read the property tree (`props.get`), for example `/ships/ship[0]/latitude`. See `addons/stockmfd` for Orbit, Surface and Map. 

**Cockpits.** Press **C** to cycle chase view, 2D cockpit, 3D cockpit and then your own cameras (`--cockpit`, `--cockpit3d`). A ship with a `cockpit` block in ship.json gets a dashboard with two MFDs under its instrument row; set `"mfds": false` to turn that off and `"mfd_left"`/`"mfd_right"` to name the default modes. In the 3D cockpit the MFDs are render-to-texture screens on a console in the ship frame, and the buttons are clicked by ray picking. For a full instrument panel give the cockpit a `layout` and a list of `instruments` (MFD modes placed on a flat board in panel units, framed MFDs or bare gauges); that board is both the 2D panel and the 3D cockpit panel, and the Skylark's six-pack plus engine MFD is the example. The panel can also carry live `controls` over its bitmap, in panel units: `button` (sets a property, lit while it matches), `toggle`, `slider` (dragged, for throttle and hover), `mfd` (a bitmap MFD button: `unit` and `button` 0-14) and `hud` (HUD mode). The Deltaglider and Shuttle-A panels use them. Mode scripts get a fifth `bare` argument in `draw`. The `instruments` addon has the six-pack modes (`adi`, `altimeter`, `asi`, `vsi`, `dg`, `turn`) and `engine`. Panel controls also include `needle`, `bar` and `number` gauges bound to properties (`min`/`max`, `offset`, `lo`/`hi`, `gate_property`), and buttons can be drawn as a `knob` or `lever`. A cockpit can set `"vc_model"` to an OBJ virtual cockpit (made with `tools/msh_import.py` from an Orbiter VC mesh); parts whose material name contains `LMFD_DISPLAY` or `RMFD_DISPLAY` show the left and right MFDs. The Deltaglider and Shuttle-A have one; a part named `HUDDISP` shows the HUD, and `"vc_hotspots"` (a quad plus a panel control, clicked by ray picking; a `button` whose `value` is a comma list cycles through it), `"vc_hide"` (material names never drawn) and `"vc_animations"` entries (`parts`, `source` property or `gear` or `anim:<name>`, `pivot`, `axis`, `angle`, `initial`; or `move`, a ship-frame offset at value 1, to slide a part) move named parts such as levers and throttles (give each animated group its own material in the OBJ). `"vc_hotspots"` (a `quad` of four ship-frame corners, top-left, top-right, bottom-left, bottom-right, plus a panel `control`) make bezel buttons clickable, and `"vc_hide"` lists part names never drawn. Hovering a control in the 2D or 3D cockpit shows a tooltip with its name (a hotspot or panel control can set `label`; Settings > Cockpit tooltips turns it off). Settings has an Anti-aliasing (MSAA) option. A hotspot can `follow` an animated part (a lever is grabbed wherever it has swung to) or be a held `drag` slider (`drag`, `drag_gain`: the Deltaglider throttles). Panel controls `nudge` (adds `value` to a -1..1 property: trim, gimbal) and `step` (moves `button` = -1/+1 detents along the `value` list) suit dials and spring switches. `vc_gauges` (`part`, `image`, `controls`) redraws a VC part's texture each frame with needle/bar/number controls in image pixels (the Shuttle-A's engine and pod-angle dials), and a hotspot with `outline_active` is outlined in red while its control is selected. `vc_instruments` (`part`, `unit`) draws a panel instrument (an entry of the cockpit's `instruments` list) onto a VC part, which the model gives its own material (the Deltaglider's artificial horizon and HSI; an instrument marked `vc_only` is not drawn on the 2D panel). `--set /sim/settings/vc-hotspots=1` outlines every hotspot and logs each click's ray. Ships can use `/controls/af-mode` (`on`, `pitch`, `off`: which control surfaces respond), `/controls/trim`, `/controls/gimbal-pitch` and `/controls/gimbal-yaw` (swing set by the ship field `gimbal_range`, degrees) and the airlock model (`/controls/airlock/outer-door`, `inner-door`, `pressurize`; `/airlock/pressure`). Not done yet: Orbiter's other nine stock modes, and the Deltaglider's hover-mode, hatch, radiator and strobe/flood-light switches. Buildings can have walkable grid interiors with sliding doors, per-room pressure and an airlock (see the modding guide).

### OrbitComp

The website has an MFD Operations Manual (`mfd-manual.html`) covering every display with a screenshot of each, and step-by-step Transfer and OrbitComp guides (`scripts/make-mfd-shots.sh` retakes the screenshots).

**OrbitComp** is an orbital computer MFD in the spirit of Orbiter's TransX: an orbit plot with the target and closest approach, manoeuvre nodes (T+, Pe, Ap, AN, DN) with the post-burn orbit and burn time, and Hohmann and ejection plans that can be loaded into a node. It reads `/universe/bodies/body[i]/*` and publishes `/orbitcomp/*`; `--set PATH=VALUE` sets any property at start.

**MFD filtering.** A cockpit's `mfd_filter` (`auto`, `atmosphere`, `space`, `none`) chooses which MFDs are offered: with `auto`, aircraft that cannot leave the atmosphere do not get the orbital ones and spacecraft lose most of the atmospheric instruments. Addon MFD modes can set `"domain"` (`space` or `atmosphere`); `none` offers everything, for spaceplanes. `mfd_hide` is a list of mode ids a ship never offers (the Deltaglider hides the airliner-style `asi`, `vsi`, `dg`, `turn`, `altimeter` and `engine`).

### Target-based MFDs

The Transfer MFD's AUTO button arms a burn autopilot (`/autopilot/burn/*`: `armed`, `start` as `/sim/time` of the window, `target-radius`, and read-only `state`, `remaining`, `ignition-in`): it holds prograde, burns until the apoapsis reaches the target radius, and ABORT cancels. Transfer and OrbitComp's PLN page draw the central body, both orbits, the transfer ellipse and a dot for each of you and the target (the Sun at the centre for a planet-to-planet plan). Align Planes, Sync Orbit and Transfer work against a target ship or body. Set it with the TGT+/TGT- buttons, `--target NAME`, or `/sim/target`. Telemetry appears under `/target/...`.

### Cameras

In the chase view, dragging with the mouse orbits the ship (the ship stays centred); the camera is kept above the terrain.

**C** cycles chase, 2D cockpit, 3D cockpit and then your own cameras, which include a built-in orbiting **External** camera that always looks at the ship. **View > Camera editor** (or the button in F2) makes more: *orbit* cameras circle a ship, planet or moon (right-drag swings, `[` `]` zoom, optional spin), and *mounted* cameras sit at an offset on a ship or body and face a direction or look at another ship or body. They are saved in `~/.config/kosmos/cameras.json`. `--user-camera N` starts through the Nth one and `--camera-editor` opens the editor.

### Window and full screen

The window can be resized and the view follows. `--fullscreen` (or **Alt+Enter**, a scenario's `"fullscreen"` setting, or the View menu) uses the full screen at the desktop resolution. While loading, a loading screen reports what is being prepared.

## Scenarios

JSON files in `scenarios/` (and `scenarios/` inside any addon) describe a start state:

```json
{"name": "...", "description": "...", "active": 0,
 "ships": [{"ship": "Wren", "body": "Earth", "orbit": {"altitude_km": 400, "inclination": 51.6, "phase": 0}}]}
```

Scenarios can sit in sub-folders; each folder becomes a submenu under File ▸ Open scenario (a leading `NN_` sets the order and is hidden, `_` shows as a space, e.g. `01_Earth/`). The menu re-reads the folders, so edits made outside Kosmos appear without a restart.

`scenarios/default.json` is loaded when Kosmos is run with no arguments (Kestrel on the Chippenham pad). Copy it to `~/.config/kosmos/scenarios/default.json` to change the default start; any start flag (`--scenario`, `--orbit-start`, `--orbit`, `--ship`) skips it.

A ship can use `"surface": {"pad" | "latitude", "longitude", "heading"}` instead of `orbit`. Run with `--scenario NAME`, list with `--list-scenarios`, and press F10 to load the next one. The defaults cover Earth, Moon, Mars, Venus and Mercury, plus an orbit scenario (`23_phobos` to `43_proteus`) for every other moon, including Io and Titan.

## Planet ephemeris

Planets whose VSOP87A file is in `data/vsop87/` (VSOP87A.mer ... .nep) start at their true heliocentric position and velocity for the Julian date
`epoch_jd` in `data/sol.json` (default J2000, 2451545.0); the state is converted to osculating elements and the planet then follows that Kepler orbit.
Other bodies use the elements in the json.

## Bases

**Active buildings.** A base object of type `building` places an addon building: a fixed ship (`"class": "building"` in a ship file listed under `"buildings"` in `addon.json`) with its own power, tanks, pressure vessels and 2D panel. Walk to its `door` on foot and press Enter to go inside, Enter again to leave. Gale Crater on Mars has a habitat, a power plant, a comms mast and an ISRU plant. Panel controls without artwork can be `label`, `readout`, `gauge` or `lamp`; components can be `isru` or `comms` and can have `wear_hours`. Press C inside to cycle chase, 2D panel and a clickable 3D interface board; `layout.image` gives the panel a background bitmap. `--enter-building TEXT` starts inside one.

**Ship systems.** Ships can carry power sources (solar, RTG, fission, fusion, fuel cell), batteries, gas and water tanks and pressure vessels (cabins). A cabin is a box simulated on a 12x12x12 gas grid; vacuum is a sink, so a breach vents it. Press **F1** for the Ship systems window or select the Systems MFD; failure effects follow `--life-support` or a scenario's `life_support`. With no electricity left, MFDs and ship lights go dark. Declare equipment in a ship's `"systems"` list and `"vessels"` list, or tag mesh parts `vessel_NAME`, `crew_*` and `vent_*` (OBJ `usemtl` groups or GLB node names; they are not drawn). From Lua, writing an area in cm2 to `/ships/ship[N]/systems/vessel[i]/breach-cm2` opens a leak. Try `23_cabin_leak` and `24_oxygen_tank_failure`; `tests/systems_test.cpp` is the unit test (`cmake --build build --target systems_test`).

A base is a built-up site on a body's surface, defined in JSON so modders can add their own: put files in `data/bases/` or in a `bases/` folder inside an addon. The terrain is levelled to one height around the centre (`flat_radius`, easing back over `blend`), and the objects stand on it.

```json
{"bases": [{
  "id": "mysite", "name": "My Site", "body": "Earth",
  "latitude": 51.4, "longitude": -2.1, "heading": 90,
  "flat_radius": 300, "blend": 200,
  "navaids": [
    {"type": "vtol", "name": "Pad", "frequency": 92.3},
    {"type": "vor", "x": 20, "z": -170, "frequency": 113.4},
    {"type": "ndb", "x": 200, "z": -200, "frequency": 0.341}
  ],
  "runways": [{"name": "Runway 1", "x": 560, "z": 0, "length": 1500, "width": 45,
    "ils": [{"frequency": 109.5}, {"frequency": 109.9, "reverse": true}]}],
  "cameras": [{"name": "Control tower", "x": 60, "y": 28, "z": -120, "fov": 40}],
  "pad": {"x": 70, "z": 0, "heading": 0},
  "objects": [
    {"type": "hangar", "x": 140, "z": -60, "heading": 270, "width": 36, "depth": 40, "height": 14},
    {"type": "street_lamp", "count": 6, "x": 20, "z": -35, "step_x": 25}
  ]}]}
```

Object coordinates are metres in the base frame (x right of the heading, z along it; `y` lifts a prop). `heading` turns the prop, `scale` scales it, `count` with `step_x`/`step_z` makes rows. Optional `size`, `width`, `depth`, `height`, `length`, `radius`, `number`, `seed`, and `colour`/`colour2` as `[r,g,b]` override the builder defaults. Types: house, office, skyscraper, hangar, wall, fence, water_tower, radar, domed_radar, pressure_dome, habitat_tube, lunar_rover, landing_pad, control_tower, street_lamp, road, barrel, crate, container, tank, conifer, rock, tree, bush, rock_cluster, car, spaceship, aeroplane, launch_tower, launch_pad, saturn_v, rocket_stage, station_module, solar_array, solar_windmill, antenna_dish, satellite, skylab.

**Terrain meshes.** `terrain_meshes` in a base file sets an `.stl`, `.obj`, `.glb` or `.gltf` into the ground so its surface becomes the terrain (a height map: no overhangs or caves). Keys: `file`, `x`, `z`, `heading`, `scale` (metres per unit), `vscale`, `y`, `blend`, `up`, `texture` (`auto` uses the mesh's own textures or vertex colours when it has them, `terrain` keeps the terrain's colouring) and `image` (a picture draped over the footprint, for STL). The Gale Crater base sits in a scan of the crater (`import/nasa/Gale Crater.stl`).

Navaids: `vtol`, `vor`, `ndb` (frequency below 1 MHz, tuned with the ADF MFD) and per-runway `ils`. A runway is drawn with markings and is levelled with the rest of the site; each `ils` entry puts a localizer and glideslope at the touchdown zone (`glide` degrees, default 3) for that landing direction. Base `cameras` appear in the F2 camera window; they follow the flown ship while it is nearby or above the local horizon (otherwise they look at the pad), unless `"track": false`, which uses `yaw` and `pitch` (degrees from the base heading).

A scenario stands a ship on a base with `"surface": {"base": "mysite"}`, optionally with `"runway": 0`, `"reverse": true`, `"along": metres past the threshold`, and `"altitude"` / `"speed"` to start in the air. Bundled: Chippenham, Twickenham, Helsinki and Wagga Wagga (scenarios 15-18).

## Aircraft

Every ship feels atmospheric drag (`drag_area`) and wind; weapons hit a ship's bounding box, or a ball of the stage radius with `"hit_sphere": true`; a ship with `"class": "aircraft"` also flies on simlib's aerodynamic model of lifting surfaces. Besides `stages` and `engines` (give a jet `"type": "jet"` so thrust fades with air density), it describes:

- `surfaces`: lifting surfaces in the ship frame (nose is -Z, roof +Y). Each has `position`, `area`, `cl0`, `cl_alpha`, `cd0`, `induced_drag`, `stall_deg`, optional `forward`/`up` (a fin has `up: [1,0,0]`), and a `control` of `elevator` (pitch), `aileron` (roll), `rudder` (yaw) or `flaps`, with `gain_deg` the angle change at full deflection. W/S, Q/E and A/D drive the surfaces; with SAS on, a rate damper (`damping`) steadies them.
- `wheels`: contact points (`position`, optional `steer_deg` for the nose wheel, `rolling_resistance`, and `drive` for a driven wheel of a ground vehicle). `,` brakes; a parked aircraft holds its brakes.
- **Ground vehicles.** `"class": "ground"` makes a wheeled vehicle (it rests level on its wheels, like an aircraft, and shows surface instruments only). Its `wheels` carry the drive: `"ground": { "drive_force": N, "max_speed": m/s }` is the total force shared by the `drive` wheels, fading to nothing at `max_speed`, limited by the wheels' grip. The throttle (Shift and Ctrl; Ctrl continues below zero) goes from -1 (reverse) to 1, A/D steer wheels that have `steer_deg` (negative for rear wheels that turn the other way), and the vehicle parks on its brakes when the throttle is zero. A `"prop": "lunar_rover"` part draws the six-wheeled rover. See `addons/cvel-lunar-rover`.
- `inertia` (optional): moments about X, Y, Z in kg m^2.
- `cameras` (optional, any ship): extra interior viewpoints with `name`, `position`, `yaw`, `pitch`, `fov`, listed in the F2 window.

The prop part `{"prop": "aeroplane", "span": 12}` gives the airliner mesh. `addons/cvel-piper-cub` is a taildragger with a glTF model made from a Blender file (scenario 16 parks it on the runway at Chippenham; it needs rudder on the take-off run). See `addons/skylark` and scenarios 19 (takeoff) and 20 (ILS approach). Property tree: `/ships/ship/alpha`, `sideslip`, `dynamic-pressure`, `flight-path`, `rate-pitch`, `rate-yaw`, `rate-roll` (angular rates in deg/s; nose up, nose right and right bank are positive), and `/controls/brake`, `/controls/flaps`, `/controls/gear`. Ships with `"retractable_gear": true` (and `gear_time`) fold their gear away: it carries load only when fully down, and `/ships/ship/gear-position` reports the travel. A model part may name an `animations` JSON (simlib format, driven by `gear-position`), or a glTF model may carry a `gear` clip.

Headless test: `--scenario 19_skylark_takeoff --selftest 60 --selftest-throttle 1` flies a scripted takeoff and prints airspeed, climb and ILS deviations.

`--aircraft-autopilot-window`, `--spacecraft-autopilot-window`, `--settings-window`, `--performance-window`, `--property-window`, `--systems-window` (F1) and `--lua-window` open the F3/F4/F1/F11 windows at start; `--menu` pins the menu bar and `--scenario-editor` / `--base-editor` open the editors. `--check-data` loads every scenario and base, saves and reloads them, and reports any that do not round-trip. **Start date.** `--date "1969-07-16 13:32:00"` (GMT; also `T` separators, a trailing `Z`/`UTC` or an offset like `+01:00`) or `--mjd 40408.5648` starts the simulation on that date. With no date from the command line or the scenario, the simulation starts at the real current GMT date and time (the real sky and local daylight); give a scenario a `"date"` to fix it. The planets and Moon start where they really were (VSOP87 planets, Moon from its mean elements with node and perigee precession), and the home planet is turned so the Sun is at the right place for the time of day. A scenario can carry `"date"` or `"mjd"`; try `44_apollo11_launch` and `45_apollo11_tli`. Dates apply to any selected star system and when a dated scenario is loaded during a session; the Info line shows the current GMT date. The planets rotate: Earth, Mars and the Moon (locked to Earth) turn under you, so the Sun crosses the sky, landed ships and bases are carried round with the ground, and launches gain the real eastward speed. Earth and Mars have their real axial tilt (so seasons, and the Sun's height at the pole) and are turned for the date and time: Earth by Greenwich sidereal time, Mars by the IAU prime-meridian rate. A body's `pole` (J2000 RA/Dec) and `meridian` (w0, rate, node_ra) in `sol.json` set this. Limits: no precession of the poles, and the other worlds with terrain are not oriented by date.

`--no-relief` disables normal-map relief on bodies; `--no-detail` disables the fine ground-detail texture (`data/textures/detail_dirt.jpg` on Earth; a body can name its own with `detail`, `detail_tile` and `detail_strength` in `sol.json`, as the Moon and Mars do) blended into terrain colour; `--no-tiles` disables streamed Earth textures. Source packs go under `import/orbiter/` and `import/celestia/`; installed runtime textures go under `data/textures/` and optional overrides under `data/textures-hires/`. Only low-resolution fallback maps are included; large Celestia and Orbiter source packs are not shipped. Sol now includes 17 more moons with Celestia textures (`tools/celestia_import.py`). Scenario `21_mars_high_orbit` puts a tug 9,000 km above Mars. Settings live under `/sim/settings/` (`labels`, `ship-labels`, `help-text`, `clock`, `orbit-line`, `sound`, `volume`).

### Multiplayer (F12)

Up to 4 players share a sky as "ghost ships": each runs their own simulation and sees the others (drawn, labelled with name and distance, and listed in the HUD). There is no collision or docking between players, but weapon fire is shared: other players' shots look and sound right and do no damage. One player hosts (`--host`, or **Host a game** in the F12 window); others join by `host:port` or pick the game from the IRC lobby, which the host announces automatically. The host must make UDP port 7777 reachable (port forward or a VPN). Press **J** (or **Go to** beside a name in the F12 window) to move your ship next to another player, so everyone can gather at a scene: in space you appear about 150 m away matching their velocity, and beside a landed player you are set down on the ground 60 m away. Time warp is held at 1x while two or more players are connected. Clocks are not synchronised, so ghosts are positioned relative to the body they orbit; views that span two bodies can be a little off. The lobby uses plain-text IRC: don't type anything private.

### In-game editors (Tools menu)

Both editors are windows with Undo/Redo (one drag or one typed edit is one step) and a file browser for Load/Save.

- **Scenario editor**: name, description, flown ship, target and NAV1/NAV2 frequencies, and a list of ships (add, duplicate, delete, reorder). Each ship has a type, label, body and either an orbit (altitude, inclination, phase) or a surface start (latitude/longitude/heading, the launch pad, or a base with a runway, direction and distance past the threshold, optionally airborne). **From flight** fills it from the live ships, **Launch** runs it at once, **Save...** writes `~/.config/kosmos/scenarios/<id>.json`.
- **Base editor**: edits the bases already loaded, and every change to objects and cameras shows at once. Tabs: *Site* (id, body, latitude/longitude, heading, levelled radius, blend, elevation, landing pad), *Objects* (any prop type, position, heading, scale, rows, sizes, colours), *Runways* (with ILS), *Navaids* and *Cameras*. **Place at flown ship** copies the flown ship's position into the selected item, so you can fly to a spot and drop an object there. **Save** writes `~/.config/kosmos/bases/<id>.json`; **Export/Import** use the file browser. Moving a site or changing its levelled ground, and new beacons, take effect after a restart.

Files in `~/.config/kosmos/scenarios/` and `bases/` are loaded at start and replace shipped ones with the same id.
