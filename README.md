# Kosmos

An Orbiter/Kerbal-style space simulator built on simlib. Fly from a launch pad to orbit, transfer to the Moon, and jump to other stars.

## Build and run

Kosmos builds against an installed [simlib](https://github.com/daver64/rsimlib) (`cmake --install` of rsimlib; no version is pinned, so keep it up to date). Needs a C++17 compiler, CMake 3.28+, Ninja and simlib's dependencies.

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/opt
    cmake --build build --target kosmos
    ./build/kosmos [options]

Use `-DKOSMOS_SIMLIB_DIR=/path/to/rsimlib` to build against a simlib source checkout instead.

## Launcher

`kosmos-launcher` (wxWidgets; on Linux install `libwxgtk3.2`) lists scenarios with previews, toggles addons (`<user dir>/addons.json`), manages texture packs, sets launch options, checks for updates and shows logs. Previews are `thumbs/<scenario id>.png` beside each scenario JSON; `scripts/make-thumbs.sh` makes them. `scripts/make-packs-json.sh` writes the pack list the launcher downloads from.

## Distributions

    scripts/build-linux.sh                          # build/package/kosmos-beta-Linux.tar.gz
    scripts/build-windows.sh                        # build-win/package/kosmos-beta-Windows.zip (mingw-w64 + vcpkg cross-build) (both do a clean rebuild; INCREMENTAL=1 skips the clean)
    scripts/release.sh                              # both builds, then publish to the web site (--no-publish, --linux-only, --windows-only, -j N)
    # builds run at most 4 compile or link jobs at once, even a bare `ninja` (a Ninja job pool); use -j N or JOBS=N with the scripts, or -DKOSMOS_BUILD_JOBS=N with cmake, to change that

Both scripts use the installed simlib under `SIMLIB_PREFIX` (default `~/opt` for Linux, `~/opt-win` for the mingw build); set `KOSMOS_SIMLIB_DIR` to build from a source checkout instead.

`data/orbiter/` (your own Orbiter high-resolution tiles) and `data/textures-hires/` (optional high-resolution surface maps) are git-ignored and never packaged; the repository carries a default texture set in `data/textures/` that works on its own. See the developers manual for installing hi-res data.

## Controls

| Key | Action |
|---|---|
| Z / X, Shift / Ctrl | Throttle |
| W A S D Q E | Rotate |
| Arrow keys | RCS translation |
| Joystick | View > Joystick: bind axes and buttons separately for aircraft and spacecraft (saved in `joystick.json`) |
| Space | Stage |
| T | Cycle SAS hold mode |
| P | Autopilot (aircraft: hold heading, altitude and thrust; rockets: ascent). The Autopilot menu has an Aircraft and a Spacecraft window |
| `,` / `.` | Time warp down / up |
| Tab | Orrery map (F: focus, `[` `]` zoom, right-drag rotate) |
| **G** | Toggle landing gear (also **L**) |
| **N** | **Galaxy map** (Left/Right choose star, `[` `]` zoom, **Enter** jump) |
| Arrows, PgUp/PgDn | In the cockpit view: move your head forward/back, left/right, up/down |
| **Enter** | **Leave the ship / board a ship** within 30 m (also View > Leave ship). On foot: W A S D walk, either Shift boosts, Space jumps, Ctrl crouches, the mouse looks (hold Alt to free the pointer), arrows also look, F manoeuvring pack, Q/E roll when floating |
| C | Cycle cameras: chase, 2D cockpit, 3D cockpit, then your own cameras (an orbiting External camera is built in) |
| **F2** | **Camera window**: pick any ship, cockpit, interior or base camera |
| **F3** | **Settings window**: body/ship labels (off by default), orbit line, key help, sound, volume, time warp. Saved to `~/.config/kosmos/settings.json` on exit |
| **F4** | **Property browser**: browse, filter, read and write the property tree (like FlightGear). Typing in its boxes does not fly the ship |
| **F11** | **Lua console** (like FlightGear's Nasal console): edit scripts with syntax highlighting, **Run** (Ctrl+Enter), **Save** (Ctrl+S, to `~/.config/kosmos/scripts/`). Choose the event that runs a script: manual, scenario load / ship change, every frame, timer, or when a property changes, and tick **Armed**. Scripts see `props` plus `event`, `dt`, `event_path`, `event_value`; `print` and errors go to the output pane. Examples live in `data/scripts/` |
| **F12** | **Multiplayer** window: host or join a game, browse the IRC lobby, chat |
| **Menu bar** | A **File / Edit / View / Tools / Help** bar is pinned along the top (untick View > *Keep menu bar visible* and it slides in only when the mouse touches the top edge). **File**: open a scenario (built-in list or a `.json` file), *Save scenario* (captures every ship's live position; orbits are saved as circular), restart, quit. **Edit**: undo/redo for the open editor, Settings, Properties. **View**: every window plus both editors. **Tools**: *Scenario editor*, *Base editor*, screenshot, *Start/Stop recording video* (H.264 + AAC sound, `kosmos-<time>.mp4` in the working directory, needs `ffmpeg` on the path), reload user content. **Help**: controls, about. **Esc** closes menus and windows first and quits only when none are open |
| B / F | Wheel brakes / cycle flaps (aircraft) |
| L | Landing gear up / down (retractable gear) |
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
| `--orbit`, `--orbit-body B`, `--orbit-alt KM` | Start in orbit |
| `--max-texture N` | Largest body texture accepted (default 16384; halved until the GPU accepts it) |
| `--gl`, `--vulkan`, `--d3d12` | Choose the graphics backend (D3D12 is Windows only). Vulkan prefers a discrete GPU (`SIMLIB_VULKAN_DEVICE_TYPE=discrete\|integrated\|any` overrides); the chosen GPU is printed at startup |
| `--user-camera N`, `--camera-editor` | Start looking through the Nth user camera, or open the Camera editor (View menu) |
| `--selftest SECS`, `--autopilot` | Headless test run |
| `--shot FILE`, `--shot-at SECS` | Save a screenshot |
| `--record SECS` | Record a video from the start for SECS seconds, then quit |
| `--host [PORT]` | Host a multiplayer game (default UDP port 7777) |
| `--join HOST[:PORT]` | Join a hosted game |
| `--name NAME` | Your name in multiplayer and the lobby |
| `--lobby` | Open the Multiplayer window at start |
| `--irc HOST`, `--channel #NAME`, `--advertise ADDR:PORT` | Lobby IRC server (default `salyut.ddns.net`, `#kosmos`) and the address you announce |
| `--no-clutter` | Leave out trees, buildings, rocks and outposts on the ground |
| `--perf` | Print frame rate and ground-scatter CPU time every few seconds |
| `--no-clouds` | Skip the cloud layers (faster start, helps on weak GPUs) |
| `--show-info` | Start with the on-screen text (title, telemetry, key reminders) showing; it is hidden by default (toggle with `I`). The bottom warp and time line is separate (View menu) |
| `--hide-info` | Hide the on-screen text and the orbit line and let the menu bar slide away (for screenshots); overrides `--show-info` |
| `--life-support off\|warn\|fatal` | What failing life support does to a crew: nothing, alarms only (default), or the crew dies and the flight ends. A scenario's `life_support` key overrides it |
| `--hud MODE` | Start with the head-up display in `orbit`, `surface` or `docking` mode (`H` cycles, `Shift+H` changes colour; shown in cockpit views only). Aircraft offer only the `surface` HUD unless the cockpit lists others in `"hud_modes"` |
| `--version` | Print the Kosmos version and exit |

## Versions

Versions are `major.build`. The major number is 0 while Kosmos is in beta, and the build number counts published binaries. The [VERSION](VERSION) file holds the number the next published build will carry, so a fresh binary already shows it in **Help > About**, in `--version`, and on the web pages. `scripts/publish-web.sh ARCHIVE...` copies built archives to the web site; if their SHA-256 differs from the published one it updates `SHA256SUMS.txt`, stamps the version on the landing page, download pages and docs, and then bumps `VERSION` to the next build. If the checksums are unchanged nothing happens.

    scripts/publish-web.sh build/package/kosmos-beta-Linux.tar.gz build-win/package/kosmos-beta-Windows.zip

## Galaxy

`galaxy.cpp` generates a deterministic catalogue of 24,409 stars: Sol, thirteen real stars at approximate positions (Alpha Centauri, Proxima, Barnard's, Wolf 359, Sirius, Lalande 21185, Procyon, Epsilon Eridani, Tau Ceti, TRAPPIST-1, Vega, Betelgeuse and Rigel), 400 invented nearby stars and a two-armed spiral disc.
Each star gets a procedural planetary system (planets, moons, belts, comets) with one home planet that has Earth-like terrain and atmosphere.
Press **N**, pick a star and press **Enter** to jump; a short hyperspace effect plays while the new system loads (the window stays open), and you arrive in a 200 km orbit around its home planet.

Hand-authored systems live in `data/systems/<name>.json` (same format as `sol.json`; the file name is the star's name in lower case with dashes). Sirius (with its white dwarf companion), Proxima, TRAPPIST-1, Betelgeuse and Rigel ship this way; every other star uses the generator.

Scenarios belong to a star system: a `"system": "Sirius"` entry in the scenario file (empty means Sol) makes starting it jump there first, and the scenario editor records the current system when it saves. `data/scenarios/Default/<Star>/` holds one Argo orbit scenario for each real star within quantum range of Sol (Alpha Centauri, Proxima, Barnard's Star, Wolf 359, Sirius, Lalande 21185, Procyon, Epsilon Eridani, Tau Ceti, TRAPPIST-1, Vega).

### Quantum jump drive

A ship with an engine of `"type": "hyperjump"` (the Argo has one; `"range_ly"` sets its reach, 50 by default) can use **View > Quantum navigation**: pick a star within range, optionally choose a specific planet or moon in the *Arrive at* box, and press *Engage quantum jump*. With no world chosen the ship arrives in orbit of the best world in the system (an Earth-like planet if there is one, else a Mars- or Venus-like rocky planet, else any rocky planet, else a moon of a gas giant, else the giant itself; `--orbit-body auto` does the same from the command line), and the window reopens at the new system. Ships without the drive see a "no quantum jump drive" notice. `--quantum` opens the window at startup.

### Invented worlds

Every generated planet and moon gets a procedural look from `planet_gen.cpp`: Mercury-like (craters), Venus-like (volcanic plains under haze), Earth-like (continents, oceans, ice, biomes), Mars-like (rust, canyons, volcanoes, caps) or gas giant (banded, with storms; ice giants are the same type with soft bands). `galaxy.cpp` picks the type and its parameters (ocean fraction, ice, relief, tint, bands) from the world's size, temperature and atmosphere; the seed makes it the same for everyone. Rocky worlds also get streaming terrain from the same generator, so what you see from orbit is what you land on. The home planet keeps its Earth-like terrain. Earth-, Venus- and Mars-like worlds also get a procedural cloud layer (thin and pinkish on Mars-like, thick and cream on Venus-like), and `--list-worlds mercury|venus|earth|mars|gas` lists the ones within quantum range.
Nothing is shipped for these worlds. The first time a system is entered, each far-view image is generated (2048x1024, 1024x512 for small bodies; a second or two each) and kept in `~/.cache/kosmos/planets` (`$XDG_CACHE_HOME/kosmos/planets`, or `%LOCALAPPDATA%\kosmos\cache\planets` on Windows). Delete the folder at any time; it is rebuilt on demand.

**Far-view detail.** The default far-view image is 2048x1024 (1024x512 for small bodies). Settings (F3) > "World texture", the `/sim/settings/world-texture` property or `--world-texture WIDTH` picks Automatic, 2048, 4096, 8192 or 16384 wide instead, and the change is applied at once (a few seconds for the larger sizes; each size is made once and kept). Measured on a Mars-like world, 4096 took about 2 s and a 34 MB PNG, 8192 about 6 s and 134 MB; 16384 is about four times that again (roughly 540 MB, and as much video memory). The cache keeps every width you have tried, so delete the folder to reclaim the space. Close to the ground the terrain colours come straight from the generator (or from baked tiles, below), so this only changes the view from afar.

`--gen-planet TYPE SEED out.png [WIDTH]` writes a preview image of a type (`mercury venus earth mars gas`) and exits.

#### High-resolution bake (optional)

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

## Addons

Ships live in `addons/<name>/` (spec JSON, Lua scripts, optional cockpit). Native DLL addons through a C ABI are not implemented (see the [roadmap](docs/native_addon_roadmap.md)); Lua covers scripting.

## Roadmap

- [Native addon modules (C interface)](docs/native_addon_roadmap.md): exploratory design for compiled vessel, MFD and rendering modules. Not scheduled.

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

An engine with `"group": "hover"` is driven by a separate hover throttle (`/controls/hover`) instead of the main throttle: Keypad + / − raise and lower it, Keypad 0 is full hover and Keypad . cuts it; the HUD shows `Hover %` on ships that have such engines. An aircraft-class ship with hover thrust holds itself level with RCS (SAS not off), since its control surfaces have no airflow to work with.

`plume` is the plume length in metres; `plume_width` and `plume_glow` (default 1) scale its width and brightness, for a very big engine. `plume_density` (default 1) scales how many particles it spawns, so a big plume looks smooth rather than dotty.

A rocket's `isp` is its vacuum value. Add `"isp_sl"` (sea-level Isp) and the engine behaves as in Orbiter: `thrust` is the vacuum thrust, propellant flow stays constant, and thrust and Isp fall toward `isp_sl` as the air thickens. Without `isp_sl` the Isp is the same everywhere.

Specs that only set `"air_breathing": true` still work and count as jets. `--shot-throttle X` holds a throttle during a `--shot`, for exhaust pictures.

## Importing Orbiter add-ons

`python3 tools/orbiter_addon_import.py <add-on root> --id <id>` starts a whole addon: it converts the meshes and drafts
`ship.json` from the add-on's C++ source, leaving `"TODO"` where it can't tell. See `docs/importing-orbiter-addons.md`.

`python3 tools/msh_import.py addons/<name>` converts every `.msh` under `addons/<name>/orbiter/Meshes` to OBJ/MTL in
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

**Cockpits.** Press **C** to cycle chase view, 2D cockpit, 3D cockpit and then your own cameras (`--cockpit`, `--cockpit3d`). A ship with a `cockpit` block in ship.json gets a dashboard with two MFDs under its instrument row; set `"mfds": false` to turn that off and `"mfd_left"`/`"mfd_right"` to name the default modes. In the 3D cockpit the MFDs are render-to-texture screens on a console in the ship frame, and the buttons are clicked by ray picking. For a full instrument panel give the cockpit a `layout` and a list of `instruments` (MFD modes placed on a flat board in panel units, framed MFDs or bare gauges); that board is both the 2D panel and the 3D cockpit panel, and the Skylark's six-pack plus engine MFD is the example. The panel can also carry live `controls` over its bitmap, in panel units: `button` (sets a property, lit while it matches), `toggle`, `slider` (dragged, for throttle and hover), `mfd` (a bitmap MFD button: `unit` and `button` 0-14) and `hud` (HUD mode). The Deltaglider and Shuttle-A panels use them. Mode scripts get a fifth `bare` argument in `draw`. The `instruments` addon has the six-pack modes (`adi`, `altimeter`, `asi`, `vsi`, `dg`, `turn`) and `engine`. Not done yet: Orbiter's other nine stock modes, and per-ship interior models.

### OrbitComp

**OrbitComp** is an orbital computer MFD in the spirit of Orbiter's TransX: an orbit plot with the target and closest approach, manoeuvre nodes (T+, Pe, Ap, AN, DN) with the post-burn orbit and burn time, and Hohmann and ejection plans that can be loaded into a node. It reads `/universe/bodies/body[i]/*` and publishes `/orbitcomp/*`; `--set PATH=VALUE` sets any property at start.

**MFD filtering.** A cockpit's `mfd_filter` (`auto`, `atmosphere`, `space`, `none`) chooses which MFDs are offered: with `auto`, aircraft that cannot leave the atmosphere do not get the orbital ones and spacecraft lose most of the atmospheric instruments. Addon MFD modes can set `"domain"` (`space` or `atmosphere`); `none` offers everything, for spaceplanes.

### Target-based MFDs

Align Planes, Sync Orbit and Transfer work against a target ship or body. Set it with the TGT+/TGT- buttons, `--target NAME`, or `/sim/target`. Telemetry appears under `/target/...`.

### Cameras

In the chase view, dragging with the mouse orbits the ship (the ship stays centred); the camera is kept above the terrain.

**C** cycles chase, 2D cockpit, 3D cockpit and then your own cameras, which include a built-in orbiting **External** camera that always looks at the ship. **View > Camera editor** (or the button in F2) makes more: *orbit* cameras circle a ship, planet or moon (right-drag swings, `[` `]` zoom, optional spin), and *mounted* cameras sit at an offset on a ship or body and face a direction or look at another ship or body. They are saved in `~/.config/kosmos/cameras.json`. `--user-camera N` starts through the Nth one and `--camera-editor` opens the editor.

### Window and full screen

The window can be resized and the view follows. `--fullscreen` (or **Alt+Enter**, a scenario's `"fullscreen"` setting, or the View menu) uses the full screen at the desktop resolution. While loading, a loading screen reports what is being prepared.

## Scenarios

JSON files in `data/scenarios/` (and `scenarios/` inside any addon) describe a start state:

```json
{"name": "...", "description": "...", "active": 0,
 "ships": [{"ship": "Wren", "body": "Earth", "orbit": {"altitude_km": 400, "inclination": 51.6, "phase": 0}}]}
```

Scenarios can sit in sub-folders; each folder becomes a submenu under File ▸ Open scenario (a leading `NN_` sets the order and is hidden, `_` shows as a space, e.g. `01_Earth/`). The menu re-reads the folders, so edits made outside Kosmos appear without a restart.

`data/scenarios/default.json` is loaded when Kosmos is run with no arguments (Kestrel on the Chippenham pad). Copy it to `~/.config/kosmos/scenarios/default.json` to change the default start; any start flag (`--scenario`, `--orbit-start`, `--orbit`, `--ship`) skips it.

A ship can use `"surface": {"pad" | "latitude", "longitude", "heading"}` instead of `orbit`. Run with `--scenario NAME`, list with `--list-scenarios`, and press F10 to load the next one. The defaults cover Earth, Moon, Mars, Venus and Mercury, plus an orbit scenario (`23_phobos` to `43_proteus`) for every other moon, including Io and Titan.

## Planet ephemeris

Planets whose VSOP87A file is in `data/vsop87/` (VSOP87A.mer ... .nep) start at their true heliocentric position and velocity for the Julian date
`epoch_jd` in `data/sol.json` (default J2000, 2451545.0); the state is converted to osculating elements and the planet then follows that Kepler orbit.
Other bodies use the elements in the json.

## Bases

**Active buildings.** A base object of type `building` places an addon building: a fixed ship (`"class": "building"` in a ship file listed under `"buildings"` in `addon.json`) with its own power, tanks, pressure vessels and 2D panel. Walk to its `door` on foot and press Enter to go inside, Enter again to leave. Gale Crater on Mars has a habitat, a power plant, a comms mast and an ISRU plant. Panel controls without artwork can be `label`, `readout`, `gauge` or `lamp`; components can be `isru` or `comms` and can have `wear_hours`. `--enter-building TEXT` starts inside one.

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

Navaids: `vtol`, `vor`, `ndb` (frequency below 1 MHz, tuned with the ADF MFD) and per-runway `ils`. A runway is drawn with markings and is levelled with the rest of the site; each `ils` entry puts a localizer and glideslope at the touchdown zone (`glide` degrees, default 3) for that landing direction. Base `cameras` appear in the F2 camera window; they follow the flown ship while it is nearby or above the local horizon (otherwise they look at the pad), unless `"track": false`, which uses `yaw` and `pitch` (degrees from the base heading).

A scenario stands a ship on a base with `"surface": {"base": "mysite"}`, optionally with `"runway": 0`, `"reverse": true`, `"along": metres past the threshold`, and `"altitude"` / `"speed"` to start in the air. Bundled: Chippenham, Twickenham, Helsinki and Wagga Wagga (scenarios 15-18).

## Aircraft

Every ship feels atmospheric drag (`drag_area`) and wind; a ship with `"class": "aircraft"` also flies on simlib's aerodynamic model of lifting surfaces. Besides `stages` and `engines` (give a jet `"type": "jet"` so thrust fades with air density), it describes:

- `surfaces`: lifting surfaces in the ship frame (nose is -Z, roof +Y). Each has `position`, `area`, `cl0`, `cl_alpha`, `cd0`, `induced_drag`, `stall_deg`, optional `forward`/`up` (a fin has `up: [1,0,0]`), and a `control` of `elevator` (pitch), `aileron` (roll), `rudder` (yaw) or `flaps`, with `gain_deg` the angle change at full deflection. W/S, Q/E and A/D drive the surfaces; with SAS on, a rate damper (`damping`) steadies them.
- `wheels`: contact points (`position`, optional `steer_deg` for the nose wheel, `rolling_resistance`). B brakes; a parked aircraft holds its brakes.
- `inertia` (optional): moments about X, Y, Z in kg m^2.
- `cameras` (optional, any ship): extra interior viewpoints with `name`, `position`, `yaw`, `pitch`, `fov`, listed in the F2 window.

The prop part `{"prop": "aeroplane", "span": 12}` gives the airliner mesh. See `addons/skylark` and scenarios 19 (takeoff) and 20 (ILS approach). Property tree: `/ships/ship/alpha`, `sideslip`, `dynamic-pressure`, `flight-path`, `rate-pitch`, `rate-yaw`, `rate-roll` (angular rates in deg/s; nose up, nose right and right bank are positive), and `/controls/brake`, `/controls/flaps`, `/controls/gear`. Ships with `"retractable_gear": true` (and `gear_time`) fold their gear away: it carries load only when fully down, and `/ships/ship/gear-position` reports the travel. A model part may name an `animations` JSON (simlib format, driven by `gear-position`), or a glTF model may carry a `gear` clip.

Headless test: `--scenario 19_skylark_takeoff --selftest 60 --selftest-throttle 1` flies a scripted takeoff and prints airspeed, climb and ILS deviations.

`--aircraft-autopilot-window`, `--spacecraft-autopilot-window`, `--settings-window`, `--property-window`, `--systems-window` (F1) and `--lua-window` open the F3/F4/F1/F11 windows at start; `--menu` pins the menu bar and `--scenario-editor` / `--base-editor` open the editors. `--check-data` loads every scenario and base, saves and reloads them, and reports any that do not round-trip. **Start date.** `--date "1969-07-16 13:32:00"` (GMT; also `T` separators, a trailing `Z`/`UTC` or an offset like `+01:00`) or `--mjd 40408.5648` starts the simulation on that date. With no date from the command line or the scenario, the simulation starts at the real current GMT date and time (the real sky and local daylight); give a scenario a `"date"` to fix it. The planets and Moon start where they really were (VSOP87 planets, Moon from its mean elements with node and perigee precession), and the home planet is turned so the Sun is at the right place for the time of day. A scenario can carry `"date"` or `"mjd"`; try `44_apollo11_launch` and `45_apollo11_tli`. The date applies at start only, and the Info line shows the current GMT date. The planets rotate: Earth, Mars and the Moon (locked to Earth) turn under you, so the Sun crosses the sky, landed ships and bases are carried round with the ground, and launches gain the real eastward speed. Earth and Mars have their real axial tilt (so seasons, and the Sun's height at the pole) and are turned for the date and time: Earth by Greenwich sidereal time, Mars by the IAU prime-meridian rate. A body's `pole` (J2000 RA/Dec) and `meridian` (w0, rate, node_ra) in `sol.json` set this. Limits: no precession of the poles, and the other worlds with terrain are not oriented by date.

`--no-relief` disables normal-map relief on bodies; `--no-detail` disables the fine ground-detail texture (`data/textures/detail_dirt.jpg` on Earth; a body can name its own with `detail`, `detail_tile` and `detail_strength` in `sol.json`, as the Moon and Mars do) blended into terrain colour; `--no-tiles` disables streamed Earth textures. Source packs go under `data/orbiter/` and `data/celestia/`; installed runtime textures go under `data/textures/` and optional overrides under `data/textures-hires/`. Only low-resolution fallback maps are included; large Celestia and Orbiter source packs are not shipped. Sol now includes 17 more moons with Celestia textures (`tools/celestia_import.py`). Scenario `21_mars_high_orbit` puts a tug 9,000 km above Mars. Settings live under `/sim/settings/` (`labels`, `ship-labels`, `help-text`, `clock`, `orbit-line`, `sound`, `volume`).

### Multiplayer (F12)

Up to 4 players share a sky as "ghost ships": each runs their own simulation and sees the others (drawn, labelled with name and distance, and listed in the HUD). There is no collision or docking between players. One player hosts (`--host`, or **Host a game** in the F12 window); others join by `host:port` or pick the game from the IRC lobby, which the host announces automatically. The host must make UDP port 7777 reachable (port forward or a VPN). Press **J** (or **Go to** beside a name in the F12 window) to move your ship next to another player, so everyone can gather at a scene: in space you appear about 150 m away matching their velocity, and beside a landed player you are set down on the ground 60 m away. Time warp is held at 1x while two or more players are connected. Clocks are not synchronised, so ghosts are positioned relative to the body they orbit; views that span two bodies can be a little off. The lobby uses plain-text IRC: don't type anything private.

### In-game editors (Tools menu)

Both editors are windows with Undo/Redo (one drag or one typed edit is one step) and a file browser for Load/Save.

- **Scenario editor**: name, description, flown ship, target and NAV1/NAV2 frequencies, and a list of ships (add, duplicate, delete, reorder). Each ship has a type, label, body and either an orbit (altitude, inclination, phase) or a surface start (latitude/longitude/heading, the launch pad, or a base with a runway, direction and distance past the threshold, optionally airborne). **From flight** fills it from the live ships, **Launch** runs it at once, **Save...** writes `~/.config/kosmos/scenarios/<id>.json`.
- **Base editor**: edits the bases already loaded, and every change to objects and cameras shows at once. Tabs: *Site* (id, body, latitude/longitude, heading, levelled radius, blend, elevation, landing pad), *Objects* (any prop type, position, heading, scale, rows, sizes, colours), *Runways* (with ILS), *Navaids* and *Cameras*. **Place at flown ship** copies the flown ship's position into the selected item, so you can fly to a spot and drop an object there. **Save** writes `~/.config/kosmos/bases/<id>.json`; **Export/Import** use the file browser. Moving a site or changing its levelled ground, and new beacons, take effect after a restart.

Files in `~/.config/kosmos/scenarios/` and `bases/` are loaded at start and replace shipped ones with the same id.
