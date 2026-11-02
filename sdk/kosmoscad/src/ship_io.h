// Reads a ship.json into a KosmosCAD scene and writes it back.
//  - Model parts (OBJ) become objects; the part's offset, rotate and scale become the object's transform.
//  - Engines, gear, ports, vessels, wheels and systems become markers.
//  - Everything else (props and profiles, cockpit, animations, scripts, weapons...) stays in `scene.ship_root` and is written back untouched.
// Saving writes each object to `<ship folder>/models/cad_<name>.obj` (the original OBJ files are never overwritten) and rebuilds the
// part, engine, port, vessel, wheel and system lists from the scene.
#pragma once

#include "scene.h"

namespace cad
{
    /** `ship_json` is the ship file; model paths in it are relative to its folder. Parts whose model cannot be read are left in place. */
    bool load_ship(const fs::path &ship_json, Scene &scene, std::string &error);
    /** Writes `scene` as the ship file `ship_json` (the folder must exist). Models go into `<folder>/models/`. */
    bool save_ship(const Scene &scene, const fs::path &ship_json, std::string &error);
    /** The ship.json that would be written, with no files created (used by tests and the JSON preview). */
    json build_ship_json(const Scene &scene, const std::vector<std::string> &model_paths);
    std::string slug(const std::string &text);
}
