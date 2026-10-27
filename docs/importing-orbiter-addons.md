# Importing an Orbiter add-on

The CVEL ships (Saturn V, Transtage, Soyuz TMA and the rest) were brought over by this procedure. Orbiter keeps masses,
thrust and fuel in compiled code, so the meshes convert automatically but `ship.json` needs a human.

## 1. Start with the helper

```
python3 tools/orbiter_addon_import.py data/orbiter/CVELSoyuzTMA --id cvel-soyuz-tma --name "CVEL Soyuz TMA"
```

It writes `addons/<id>/` with:

- `models/`: every `Meshes/*.msh` as OBJ/MTL with textures as PNG (`tools/msh_import.py`; DDS decoding needs Pillow).
- `addon.json`, with `TODO` for author and description.
- `ship.json`: a draft. When the add-on ships its C++ source the tool reads, by pattern, the empty mass (`SetEmptyMass`
  and `#define`s in the headers beside the `.cpp`), `CreatePropellantResource`, `CreateThruster`, `CreateDock`,
  `SetCameraOffset` and `AddMesh`. Anything it could not find is the string `"TODO"`.

Without source (a `.dll` only), the tool still converts the meshes and reads the mass and size from `Config/*.cfg`;
read the add-on's documentation for the rest.

## 2. Orbiter to Kosmos conventions

- Orbiter is left-handed with the nose at +Z; Kosmos has the nose at -Z. The tool negates Z in every position and
  direction. An Orbiter engine at `_V(0,0,-2.64)` firing `_V(0,0,1)` becomes `[0,0,2.64]` firing `[0,0,-1]`.
- Orbiter's `isp` is in m/s; Kosmos wants seconds (divide by 9.80665). The tool does this.
- Orbiter thrusters of a few hundred newtons are attitude jets. The tool does not list them as engines; it sets
  `rcs_thrust` to twice the largest.
- Orbiter's `CreateThruster` fuel resource may be shared by every thruster, so the draft sums all propellant resources.
  Check it against the add-on's documentation.

## 3. Finish by hand

Search the draft for `TODO` and decide:

- `class`: launcher, capsule, tug, probe, lander, station, transfer or aircraft.
- Each engine's `fuel` (kerosene, hydrogen, methane, hypergolic, solid, nuclear) and plume size. Add `isp_sl` for
  an engine with different sea-level and vacuum Isp.
- `drag_area`, `torque_accel`, `max_rate`, and the stage `z`, `length` and `radius`, which are used for inertia and
  debris. A mesh's extents are printed in the OBJ; measure them.
- `systems` and `vessels` (batteries, solar, tanks, a crew cabin) if you want life support and power.
- Multi-stage vehicles: the tool writes one stage. Split the engines and meshes into `stages` yourself. Orbiter's
  `SetMainStage`/`SetBoostStage` style code (see `addons/cvel-soyuz/fg.json`) maps to one Kosmos stage per configuration.
  Kosmos fires only the active stage's engines, so an engine that keeps burning across a separation (a core stage
  under strap-on boosters) is listed in both stages. Mesh offsets and engine positions are in the one ship frame (Z negated).
- A payload that Orbiter carries as a separate vessel (the Soyuz TMA on top of the launcher) becomes the last stage.
- Things the tool never reads: thruster groups, animations (solar panels, parachutes), panels, key handlers and anything
  computed at run time.

## 4. Check it

```
build/kosmos --ship "CVEL Soyuz TMA" --orbit --selftest 5
```

Add a scenario under `data/scenarios/` (see `data/scenarios/CVEL/`), then `scripts/make-thumbs.sh` for its launcher
preview. Keep the original licence and credits: Orbiter add-ons often carry the LGPL or a request for credit, so put
them in `addon.json` (`author`, `description`).
