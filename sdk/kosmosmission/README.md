# KosmosMission

A GTK 3 mission planner for Kosmos (Linux and Windows). The views are OpenGL (a GtkGLArea, libepoxy).

Build: `cmake --build build --target kosmosmission` (the program lands in `addons/kosmosmission/bin`). Run it from the add-ons menu, or directly; it finds the Kosmos folder from its own location (`KOSMOS_ROOT` overrides).

- `core/` is the planning maths with no GTK or GL: `orbit` (Kepler and Lambert), `bodies` (`data/sol.json` ephemerides), `transfer` (Hohmann, Lambert grids and the burn-delay search, return to parent), `budget` (stage delta-v, the ascent estimate and the ship.json payload volumes), `payload` (the fit check) and `mission` (legs, ledger, scenario JSON). The Lambert and plan logic follows `addons/stockmfd/orbitcomp.lua`.
- `src/` is the app (`main.cpp`) and the GL drawing (`gl_view.*`).
- Missions save as normal scenarios with a `"mission"` block (`version`, `ship_file`, `payload`, `payloads`, `margin`, `start`, `legs`).
- `data/mission/ascent.json` holds measured launch costs (`{"ship", "body", "dv", "note"}`) that replace the ascent estimate.
- Tests: `cmake --build build --target missioncore_test && build/missioncore_test`.
- The **Guide** button in the window walks through a complete mission (stack, ascent, Lambert or Hohmann, flying it with OrbitComp); the KosmosMission web page has the longer version.
- `--demo` plans a launch to Mars; `KM_PORKCHOP=1 kosmosmission --demo` opens on the porkchop. `--shot FILE` draws the window to a PNG and exits; `KM_SHIP=name` picks the demo ship and `KM_PAYLOAD=1` adds a Dragonfly and twelve Luna probes.
- Payload ships: a ship declares where it carries cargo with `"payload_bays": [{"name", "shape": "cylinder"|"box", "radius" (or "size": [width, height, length] for a box), "length", "max_mass" (optional), "position" (optional)}]` (the bay axis is the ship's z axis), and how big it is as cargo with `"payload_size": {"diameter", "length"}` or `[width, height, length]` (default: a cylinder from the stages' largest radius and total length). The fit check is conservative and tries each item every way round. Stacks (2-3 ships carried one on another) are built with Add to stack / Add stack. Bays take an optional `"port"`; saving a scenario docks the first unit of each bay to that port and notes what was left out.

Limits: transfers between a moon and a planet are not planned directly (go through the parent); sibling-body transfers use a circular parking orbit with a free plane; local moon transfers assume the parking orbit lies in the moon's orbital plane.
