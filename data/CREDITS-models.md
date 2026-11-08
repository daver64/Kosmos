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
- **Curiosity rover** (`addons/nasa-curiosity/curiosity.glb`): NASA/JPL-Caltech's
  [Curiosity Rover (MSL) (Dirty)](https://github.com/nasa/NASA-3D-Resources/tree/master/3D%20Models/Curiosity%20Rover%20(MSL))
  model and textures, re-oriented and exported with `scripts/export-curiosity.py`. NASA does not endorse Kosmos.
- **Orbiter meshes** (`parts/orbiter/`, `addons/orbiter-stations/`): Kennedy Space Center pad, VAB, tarmac and other scenery, plus the Mir, Wheel and Project Alpha ISS (`addons/orbiter-stations/`), converted from the stock meshes of Orbiter Space Flight Simulator (Martin Schweiger, MIT licence).
- **CVE-Lite Ares Spider** (`addons/cvel-ares/models/`): converted from the Orbiter add-on mesh by Rich Wall (2004); the original add-on credits Dave Rowbotham for programming. Distributed with permission; see `addons/cvel-ares/LICENSE-Ares-Spider.txt`.
- **CVE-Lite Salyut and Lunar Salyut** (`addons/salyut/models/`): converted from Dave Rowbotham's Orbiter meshes, LGPL; see `addons/salyut/CREDITS-Salyut.txt` and `addons/salyut/lgpl.txt`.

Masses, engines and sizes in the `part.json` files are approximate and are Kosmos's own.
