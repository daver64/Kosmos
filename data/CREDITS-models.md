# Model credits

The `nasa_*` parts in `parts/` and the Vesta shape in `bodies/vesta.glb` are made from models published by NASA's
[3D Resources](https://nasa3d.arc.nasa.gov/) site. NASA media is generally not subject to US copyright, but
[NASA media guidelines](https://www.nasa.gov/nasa-brand-center/images-and-media/) and any third-party rights apply.
NASA does not endorse Kosmos or any product made with these models. Individual sources and any identifiers still
need review.

Kosmos decoded, re-oriented, scaled, decimated (to about 30 000 triangles) and re-exported them
(see `tools/nasa_import.py`); the originals belong to NASA and the artists who made them.

- **Kit parts:** ACRIMSAT, Gemini Agena Target Vehicle, Apollo Lunar Module, Mercury capsule (Friendship 7), Pioneer 10,
  Viking lander.
- **Props:** Astronaut, launch gantry, habitat units 1 and 2, NASA Headquarters, Mission Control, radome, Space Exploration
  Vehicle, Vehicle Assembly Building.
- **Asteroid shape:** Vesta (shape model from the Dawn mission).
- **Terrain:** Gale Crater (`terrain/gale-crater.stl`, a scan of the crater and Mount Sharp from NASA's 3D Resources).
- **Orbiter meshes** (`parts/orbiter/`, `addons/orbiter-stations/`): Kennedy Space Center pad, VAB, tarmac and other scenery, plus the Mir, Wheel and Project Alpha ISS (`addons/orbiter-stations/`), converted from stock meshes of Orbiter Space Flight Simulator. The upstream project includes the [MIT licence](LICENSE-Orbiter.txt); verify the provenance and any additional conditions of each converted mesh and texture before redistribution.
- **Soyuz TMA spacecraft** (`addons/cvel-soyuz/`): converted from the CVE-lite
  SoyuzTMA add-on. Its included notices credit the Soyuz meshes and textures
  to Jason Benson and adaptation for animation to Jógvan Trondesen, and state
  that the Soyuz meshes and textures are public domain; Benson's quoted
  mesh permission requires credit.
  The Soyuz launcher meshes are credited to Urwumpe and the parachute meshes
  to D Henderson; their upstream notices specify the GNU LGPL without a
  version. See `addons/cvel-soyuz/source/README.md` for the correspondence,
  source meshes and licence text.

Masses, engines and sizes in the `part.json` files are approximate and are Kosmos's own.
