# Model credits

The `nasa_*` parts in `parts/` and the Vesta shape in `bodies/vesta.glb` are made from models published by NASA's
[3D Resources](https://nasa3d.arc.nasa.gov/) site. NASA media is generally not copyrighted and may be used freely, but
NASA does not endorse Kosmos or any product made with these models.

Kosmos decoded, re-oriented, scaled, decimated (to about 30 000 triangles) and re-exported them
(see `tools/nasa_import.py`); the originals belong to NASA and the artists who made them.

- **Kit parts:** ACRIMSAT, Gemini Agena Target Vehicle, Apollo Lunar Module, Mercury capsule (Friendship 7), Pioneer 10,
  Viking lander.
- **Props:** Astronaut, launch gantry, habitat units 1 and 2, NASA Headquarters, Mission Control, radome, Space Exploration
  Vehicle, Vehicle Assembly Building.
- **Asteroid shape:** Vesta (shape model from the Dawn mission).
- **Terrain:** Gale Crater (`terrain/gale-crater.stl`, a scan of the crater and Mount Sharp from NASA's 3D Resources).
- **Orbiter meshes** (`parts/orbiter/`, `addons/orbiter-stations/`): Kennedy Space Center pad, VAB, tarmac and other scenery, plus the Mir, Wheel and Project Alpha ISS (`addons/orbiter-stations/`), converted from the stock meshes of Orbiter Space Flight Simulator (Martin Schweiger, MIT licence).

Masses, engines and sizes in the `part.json` files are approximate and are Kosmos's own.
