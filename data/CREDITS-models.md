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
- **Asteroid shapes** (`bodies/*.glb`): NASA 3D Resources models of Vesta, Bennu, Geographos, Kleopatra, Itokawa, Toutatis, Eros, Mithra, Golevka and 1996 HW1, converted with `tools/nasa_import.py`. Eros and Itokawa are reconstructed from the two halves in NASA's printable files; Vesta uses the complete A model rather than the printable halves in B.
- **Terrain:** Gale Crater (`terrain/gale-crater.stl`, a scan of the crater and Mount Sharp from NASA's 3D Resources).
- **Apollo 11 landing site** (`terrain/apollo11.stl`): NASA's 30 km terrain model, with the original 60-fold vertical exaggeration removed in its placement.
- **Gateway** (`addons/nasa-gateway/gateway.glb`): NASA's [Gateway Core model](https://science.nasa.gov/3d-resources/gateway-lunar-space-station/), by Alberto G. Bertolin and Bradley W. Reynolds of NASA/JSC, exported with `scripts/export-gateway.py`. The station systems and docking adapter are approximate; NASA does not endorse Kosmos.
- **Curiosity rover** (`addons/nasa-curiosity/curiosity.glb`): NASA/JPL-Caltech's
  [Curiosity Rover (MSL) (Dirty)](https://github.com/nasa/NASA-3D-Resources/tree/master/3D%20Models/Curiosity%20Rover%20(MSL))
  model and textures, re-oriented and exported with `scripts/export-curiosity.py`. NASA does not endorse Kosmos.
- **Space Exploration Vehicle** (`addons/nasa-sev/sev.glb`): NASA's Space Exploration Vehicle model and textures, re-oriented and reduced with `scripts/export-sev.py`. NASA does not endorse Kosmos.
- **Orbiter meshes** (`parts/orbiter/`, `addons/orbiter-stations/`): Kennedy Space Center pad, VAB, tarmac and other scenery, plus the Mir, Wheel and Project Alpha ISS (`addons/orbiter-stations/`), converted from the stock meshes of Orbiter Space Flight Simulator (Martin Schweiger, MIT licence).
- **CVE-Lite Ares Spider** (`addons/cvel-ares/models/`): converted from the Orbiter add-on mesh by Rich Wall (2004); the original add-on credits Dave Rowbotham for programming. Distributed with permission; see `addons/cvel-ares/LICENSE-Ares-Spider.txt`.
- **CVE-Lite Salyut and Lunar Salyut** (`addons/salyut/models/`): converted from Dave Rowbotham's Orbiter meshes, LGPL; see `addons/salyut/CREDITS-Salyut.txt` and `addons/salyut/lgpl.txt`.

Masses, engines and sizes in the `part.json` files are approximate and are Kosmos's own.

The new asteroid orbital elements and physical measurements are from the
[JPL Small-Body Database](https://ssd.jpl.nasa.gov/tools/sbdb_lookup.html).
They are osculating elements propagated to the Sol file's J2000 epoch with a
two-body approximation, not precise long-term ephemerides. Geographos, Mithra
and 1996 HW1 use a 2,000 kg/m³ assumed density for gravity; Golevka uses the
database density and a spherical volume. Asteroid gravity and collision surfaces
remain spherical approximations, independent of the visible shape.
