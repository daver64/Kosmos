# Model credits

The `nasa_*` parts in `parts/` and the Vesta shape in `bodies/vesta.glb` are made from models published by NASA's
[3D Resources](https://science.nasa.gov/3d-resources/) site. NASA describes its 3D Resources assets as free to download
and use. NASA media is generally not subject to US copyright, but
[NASA media guidelines](https://www.nasa.gov/nasa-brand-center/images-and-media/) and any third-party rights apply.
NASA does not endorse Kosmos or any product made with these models. Individual third-party conditions still need review.

Kosmos decoded, re-oriented, scaled, decimated (to about 30 000 triangles) and re-exported them
(see `tools/nasa_import.py`); the originals belong to NASA and the artists who made them.

- **Kit parts:** ACRIMSAT, Gemini Agena Target Vehicle, Apollo Lunar Module, Mercury capsule (Friendship 7), Pioneer 10,
  Viking lander.
- **Props:** Astronaut, launch gantry, habitat units 1 and 2, NASA Headquarters, Mission Control, radome, Space Exploration
  Vehicle, Vehicle Assembly Building.
- **Asteroid shape:** Vesta (shape model from the Dawn mission).
- **Terrain:** Gale Crater (`terrain/gale-crater.stl`, a scan of the crater and Mount Sharp from NASA's 3D Resources).
- **Orbiter-derived visuals:** Deltaglider, Shuttle-A, Dragonfly and Mir use
  meshes and textures converted from the Orbiter source repository, distributed
  under its MIT licence in `data/LICENSE-Orbiter.txt`. Sources are
  `Src/Vessel/DeltaGlider/Meshes/deltaglider.msh`,
  `Src/Vessel/DeltaGlider/Meshes/deltaglider_vc.msh`,
  `Src/Vessel/ShuttleA/Meshes/ShuttleA.msh`,
  `Src/Vessel/ShuttleA/Meshes/ShuttleA_vc.msh`,
  `Src/Vessel/Dragonfly/Meshes/Dragonfly.msh`, `Meshes/Mir.msh`, and their
  corresponding `Textures` folders. `tools/msh_import.py` converts geometry
  and DDS textures and removes coincident triangles;
  `tools/import_orbiter_cockpits.py` adapts the VC displays for Kosmos.
  ISS and Wheel retain
  procedural placeholders; separately sourced panel art and scenery remain excluded.
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
- **CVEL Energia-M** (`addons/cvel-energia-m/`): original meshes by Erik
  Anderson (Sputnik); the CVEL author confirms Erik's fleet meshes are covered
  by the fleet licence and may be included. The source notice credits Dave
  Rowbotham for booster and logic integration. Original meshes, texture and
  notice are in `addons/cvel-energia-m/source/`.
- **CVEL Proton-K** (`addons/cvel-proton/`): the original notice credits
  D Henderson, Urwumpe (second stage) and Alex v Hohlov (first and third
  stages), and releases all meshes under the Artistic Licence. Source meshes,
  notice and licence are in `addons/cvel-proton/source/`; the missing
  second-stage texture is represented by its original material colour.
- **CVEL Saturn V and Apollo** (`addons/cvel-saturn-v/`): meshes credited to
  TomPA and Sputnik in the original `source/SaturnV.txt` notice. The CVEL
  author reports that both gave permission to bundle their meshes with CVEL,
  and that the textures came with the models. Kosmos converts the source meshes
  and textures with `tools/import_cvel_saturn_v.py` and removes overlapping
  faces. The notice's LGPL statement applies to the original C++ module code,
  not a stated licence for the meshes.
- **CVEL Saturn V MLV1, MLV2, MLV3 and V-23L**: converted from the corresponding CVEL source
  distributions. Their notices credit TomPA and Sputnik; the CVEL author
  reports their permission to bundle the meshes and accompanying textures.
  Four absent MLV3 fin textures use the source material instead.
- **CVEL Atlas III** (`addons/cvel-atlas/`): meshes by Alex Hohlov, released
  under the Artistic Licence. The model package includes matching bitmap
  textures and the licence text. The original source, notice and licence,
  plus a description of Kosmos's conversion and coincident-face cleanup, are
  in `addons/cvel-atlas/source/`.
- **CVEL Delta IV Heavy** (`addons/cvel-delta-iv/`): meshes by Alex Hohlov
  (STS-107), used with permission according to the original CVEL notice in
  `addons/cvel-delta-iv/source/`. The CVEL author confirms Alex gave permission
  to use his meshes in CVEL downloads. The source distribution's accompanying
  textures are converted along with the meshes.
- **CVEL Titans** (`addons/cvel-titan/`), by Erik Anderson (Sputnik): the original `Titans.pdf` credits
  Scott Oehlerking and the Project Gemini team for Titan II meshes and textures,
  STS-107 for Titan IVB and Agena meshes and textures, and John Graves for
  Transtage and Titan solid motor meshes. The CVEL author confirms the same
  permission to use these meshes in CVEL downloads. Original notices are in
  `addons/cvel-titan/source/`. The Centaur nozzle texture is from Orbiter
  (see `data/LICENSE-Orbiter.txt`).

## NASA source inventory

The local originals listed below matched the Git blob IDs in NASA's
[NASA-3D-Resources](https://github.com/nasa/NASA-3D-Resources) repository
(root tree `11ebb4ee043715aefbba6aeec8a61746fad67fa7`) when checked on
2026-11-07. Paths are relative to that repository. The mappings to stock parts
are recorded by `tools/nasa_import.py`; comparing the original files does not
independently verify every converted output or establish every item's conditions.

| Kosmos output | NASA source |
|---|---|
| `parts/nasa_acrimsat/` | `3D Models/Active Cavity Irradiance Monitor Satellite (AcrimSAT) (B)/Active Cavity Irradiance Monitor Satellite (AcrimSAT) (B).glb` |
| `parts/nasa_agena/` | `3D Models/Agena Target Vehicle/Agena Target Vehicle.glb` |
| `parts/nasa_lunar_module/` | `3D Models/Apollo Lunar Module/Apollo Lunar Module.glb` |
| `parts/nasa_mercury_capsule/` | `3D Models/Atlas 6 (Friendship 7)/Atlas 6 (Friendship 7).glb` |
| `parts/nasa_pioneer10/` | `3D Models/Pioneer 10/Pioneer 10.glb` |
| `parts/nasa_viking_lander/` | `3D Models/Viking Lander/Viking Lander.glb` |
| `parts/nasa_astronaut/` | `3D Models/Astronaut/Astronaut.glb` |
| `parts/nasa_gantry/` | `3D Models/Gantry/Gantry.glb` |
| `parts/nasa_habitat_unit_1/` | `3D Models/Habitat Demonstration Unit/Habitat Demonstration Unit (part 1).glb` |
| `parts/nasa_habitat_unit_2/` | `3D Models/Habitat Demonstration Unit/Habitat Demonstration Unit (part 2).glb` |
| `parts/nasa_headquarters/` | `3D Models/Headquarters Building/Headquarters Building.glb` |
| `parts/nasa_mission_control/` | `3D Models/JSC Mission Control Room/JSC Mission Control Room.glb` |
| `parts/nasa_radome/` | `3D Models/Radome/Radome.glb` |
| `parts/nasa_exploration_vehicle/` | `3D Models/Space Exploration Vehicle/Space Exploration Vehicle.glb` |
| `parts/nasa_vab/` | `3D Models/Vehicle Assembly Building (VAB)/Vehicle Assembly Building (VAB).glb` |
| `bodies/vesta.glb` | `3D Printing/Asteroid 4 Vesta (A)/Asteroid 4 Vesta (A).stl` |
| `terrain/gale-crater.stl` | `3D Printing/Gale Crater/Gale Crater.stl`; bundled file and local original both have SHA-256 `35acd7fa8365746c781166d15e1d0d51becb4a1af815ec470971a63f52a01ba6`. |

The local `Voyager Probe (B).glb` and `Pioneer 10.glb` originals also match
NASA's catalog, but the separately converted `addons/nasa-voyager/` and
`addons/nasa-pioneer10/` outputs were not independently traced through their
conversion process in this check.

Masses, engines and sizes in the `part.json` files are approximate and are Kosmos's own.
