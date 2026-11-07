# Model credits

The `nasa_*` parts in `parts/` and the Vesta shape in `bodies/vesta.glb` are made from models published by NASA's
[3D Resources](https://science.nasa.gov/3d-resources/) site. NASA describes its 3D Resources assets as free to download
and use. NASA media is generally not subject to US copyright, but
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
- **Orbiter-derived visuals:** ISS, Mir and Wheel use procedural placeholders. Converted Orbiter scenery meshes and textures were removed while their individual redistribution terms remain unresolved.
- **Soyuz TMA spacecraft** (`addons/cvel-soyuz/`): converted from the CVE-lite
  SoyuzTMA add-on. Its included notices credit the Soyuz meshes and textures
  to Jason Benson and adaptation for animation to Jógvan Trondesen, and state
  that the Soyuz meshes and textures are public domain; Benson's quoted
  mesh permission requires credit.
  The Soyuz launcher meshes are credited to Urwumpe and the parachute meshes
  to D Henderson; their upstream notices specify the GNU LGPL without a
  version. See `addons/cvel-soyuz/source/README.md` for the correspondence,
  source meshes and licence text.
- **Fregat** (`addons/cvel-fregat/`): converted from the CVE-lite Fregat
  mesh by D Henderson, released under the GNU LGPL according to its upstream
  notice. The original mesh, notice and licence text are in
  `addons/cvel-fregat/source/`.

Masses, engines and sizes in the `part.json` files are approximate and are Kosmos's own.
