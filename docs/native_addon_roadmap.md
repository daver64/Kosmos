# Native addon modules: roadmap

**Status:** superseded. In-process native modules were not built. Native addons shipped in 0.23 as KosmosLink, a separate program connected over a loopback socket, documented in the SDK manual (`sdk/docs/MANUAL.md`). This document is kept as the record of the original in-process design.

## Problem
Addons are currently data (JSON, OBJ/GLB) plus sandboxed Lua. Orbiter-style addons can also ship compiled code: custom vessel behaviour, MFD and HUD drawing, and per-frame logic. The aim is to explore a **strictly C ABI**, so modules can be written in C, C++, Rust, Zig and so on, and Kosmos never exposes its C++ types.

Decisions so far: full Orbiter-style scope (vessel modules, MFD and HUD drawing, access to rendering), Linux and Windows, off by default behind a Settings checkbox with a warning, allowed in multiplayer.

## What exists today (relevant hooks)
- `Sim::step(frame)`: Lua `update` event, `read_controls()`, `Ship::apply_controls`, `world.step`, `nav.couple`, `Ship::after_step`.
- `sl::PropertyTree` (`sim.props`) is the shared state. HUD, cockpit panels, settings and Lua already go through it.
- `Controls` and `Telemetry` (`src/ship.h`) are plain structs, so they map cleanly to C.
- MFD: `MfdSystem::register_mode(addon, id, path)` runs Lua; draw output is a command list (line, rect, fillrect, circle, disc, tri, text). It is already close to a C canvas.
- Addon discovery: `Sim::scan_addons()` reads `addon.json` (ships, scenarios, scripts, MFD modes).
- simlib has **no C API** and its rendering is C++ (glm, scene3d, model3d, draw). Kosmos needs a C wrapper layer.

## Proposed design

### 1. Public header `include/kosmos_module.h` (pure C99, versioned)
- `KOSMOS_API_VERSION` as major and minor. Every struct starts with `uint32_t size`, so fields can be appended without breaking old modules.
- Plain-C math types: `kvec3 {double x,y,z}`, `kquat`, `kcolour`. Handles are opaque `uint64_t` (ship, body, mesh, texture, property). No raw pointers cross the boundary except callback `user` data.
- Module exports:
  - `int kosmos_module_init(const kosmos_host *host, kosmos_module *out)`, called once; it fills in a table of callbacks.
  - `void kosmos_module_shutdown(void)`.
- The host is a **function table** (`kosmos_host`) passed to init, so modules do not link against the executable. This works on Windows with no import library.

### 2. Host API groups (all C functions in the table)
| Group | Examples |
|---|---|
| Log and info | `log`, `api_version`, `addon_dir`, `sim_time`, `warp` |
| Property tree | `prop_get/set_{bool,int,double,string}`, `prop_exists`, `prop_watch(path, cb)` |
| Universe | body count, name, radius, mass, position, `body_state(handle)` |
| Vessels | `ship_count`, `active_ship`, `ship_state()` (position, velocity, attitude, mass, telemetry), `ship_set_controls`, `ship_apply_force/torque`, `stage`, `spawn(spec_name)`, `ship_set_thruster`, `ship_set_mass_props` |
| Events | callbacks: pre_step, post_step, stage, dock/undock, crash, scenario_load, scenario_unload |
| 2D canvas | line, rect, circle, tri, text, image into a panel (MFD, HUD overlay, cockpit panel) |
| Rendering | see section 4 |
| Input | read key or axis, register a key binding |
| Audio | play a sound file, set a loop |

### 3. Module kinds
- **Plugin**: global callbacks (`pre_step`, `post_step`, `frame`, `draw_hud`) with no vessel of its own.
- **Vessel module**: an `addon.json` ship entry gets `"module": "name"` and `"class": "<exported class id>"`. The module supplies:
  - `create(ship) -> instance` and `destroy(instance)`
  - `pre_step(instance, dt, controls*)`, which may rewrite controls or apply forces
  - `post_step(instance, dt)`
  - `on_event(instance, event)`
  - `draw_panel(instance, panel_id, canvas)`

  The ship JSON stays the source of geometry, mass and engines, so a module changes behaviour and does not replace the data model.
- **MFD mode module**: registers a mode by id; the module gets a canvas and draws it, the same as the Lua modes do now.

### 4. "Full access to rendering" over C
Exposing simlib verbatim is not possible, because it is C++ with templates and glm. The plan is a **curated, versioned C wrapper** over the parts simlib already has.
- Resources: `mesh_load(path)`, `texture_load(path)`, `material_*`, `mesh_create(vertices, indices)`.
- Draw: `draw_mesh(mesh, transform, material)` in world space, queued into the frame's scene pass, plus light add/remove and particle emitters.
- Hooks: `pre_world_draw`, `post_world_draw` and `draw_overlay` (screen space). The host owns the GL, Vulkan or D3D state, so modules never touch the graphics API and stay portable across backends.
- Anything lower level is deferred. If needed later it ships as an optional `kosmos_host_ex` table, so the base ABI stays small.

### 5. Loading and safety
- Manifest: `addon.json` gains `"modules": [{"file":"foo", "api":"1.0", "kind":"plugin|vessel|mfd"}]`. The file is found as `foo.so` / `libfoo.so` on Linux and `foo.dll` on Windows, with an optional per-arch subfolder.
- Loader: `dlopen`/`LoadLibrary` (`RTLD_LOCAL`), `dlsym` of the two exports, and an API major-version check. It refuses a mismatched major, and logs and skips on any failure. Nothing is loaded when the setting is off.
- Setting `/sim/settings/native-modules` (default false): a checkbox in F3 Settings with a warning ("native modules run with full permissions"), visible in the F4 tree. The module list (file, path, hash) is logged.
- Native code cannot be sandboxed. The protections are the opt-in, the visible module list, and error isolation: no exception crosses the C boundary, and a module that faults is disabled where the platform allows. Unload is `shutdown` then `dlclose`.
- Multiplayer: allowed. Netplay still only shares ship state, so a module that alters physics may desync players; the manual will document this.
- Threading: all callbacks run on the main thread, as in Orbiter, so no locking is needed.

### 6. Build, packaging and docs
- `include/kosmos_module.h` is installed with the release packages (`scripts/release.sh` and the Windows build).
- Example modules in `examples/modules/` (C only): a HUD plugin, a vessel with a simple autopilot, and an MFD mode, with a small CMake and Makefile.
- `--check-data` lists modules and reports missing files and version mismatches.
- Docs: a "Native modules" section in `modding.html`, a C API reference page, a security note in `manual.html` (Settings), and a note in `developers.html`.
- Tests: a small test module loaded by `--selftest` (init, a few frames, check a property it wrote).

## Phases and risks
1. **Header and loader**: lowest risk, and makes the rest testable.
2. **Property and vessel API** plus pre_step/post_step events: needs a stable `Controls`/`Telemetry` ABI (fixed-width, size-prefixed).
3. **Vessel modules**: instance lifecycle across scenario reload, ship deletion, staging, docking and netplay-spawned ships.
4. **2D canvas, MFD and HUD hooks**: reuse the MFD command list.
5. **3D draw hooks**: the biggest unknown. Hook points in the render loop (`src/app_draw_world.cpp`, around the planet and ship draw) must give module meshes depth, lighting and haze like built-ins. The handle-and-queue approach avoids exposing GL state.
6. **Docs, examples, packaging, Windows**.

Key risks: ABI stability (mitigated by `size`-prefixed structs and a minor-version policy); a module crash taking down the game (cannot be fully avoided); floating-origin issues for forces applied in world space (the API must state which frame, body-relative doubles); and the rendering wrapper growing into a full simlib mirror.

## Out of scope for now
Sandboxing or signing native code, hot-reload of modules, and raw GL, Vulkan or D3D handles.
