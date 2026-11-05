# KosmosMission

A GTK 3 mission planner for Kosmos (Linux). The views are OpenGL (a GtkGLArea, libepoxy).

Build: `cmake --build build --target kosmosmission` (the program lands in `addons/kosmosmission/bin`). Run it from the add-ons menu, or directly; it finds the Kosmos folder from its own location (`KOSMOS_ROOT` overrides).

- `core/` is the planning maths with no GTK or GL: `orbit` (Kepler and Lambert), `bodies` (`data/sol.json` ephemerides), `transfer` (Hohmann, Lambert grids and the burn-delay search, return to parent), `budget` (stage delta-v and the ascent estimate) and `mission` (legs, ledger, scenario JSON). The Lambert and plan logic follows `addons/stockmfd/orbitcomp.lua`.
- `src/` is the app (`main.cpp`) and the GL drawing (`gl_view.*`).
- Missions save as normal scenarios with a `"mission"` block (`version`, `ship_file`, `payload`, `margin`, `start`, `legs`).
- `data/mission/ascent.json` holds measured launch costs (`{"ship", "body", "dv", "note"}`) that replace the ascent estimate.
- Tests: `cmake --build build --target missioncore_test && build/missioncore_test`.
- `--demo` plans a launch to Mars; `KM_PORKCHOP=1 kosmosmission --demo` opens on the porkchop.

Limits: transfers between a moon and a planet are not planned directly (go through the parent); sibling-body transfers use a circular parking orbit with a free plane; local moon transfers assume the parking orbit lies in the moon's orbital plane.
