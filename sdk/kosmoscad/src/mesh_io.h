// KosmosCAD mesh file formats beyond OBJ: Orbiter .msh and glTF/GLB readers, a GLB writer, and the in-place read/write
// used when Kosmos opens a part in KosmosCAD ("Edit in KosmosCAD"). Headless, so it is unit-tested.
#pragma once

#include "scene.h"

namespace cad
{
    /** True for the extensions import_mesh_file understands: .obj .msh .glb .gltf. */
    bool is_mesh_file(const fs::path &file);
    /** Reads an Orbiter MSHX1 mesh: one object per group, Z negated (Orbiter is left-handed, nose +Z) and V flipped, as tools/msh_import.py does.
     *  Textures are looked up beside the file, then in sibling Textures folders (Orbiter's layout). */
    bool import_msh(const fs::path &file, Scene &scene, std::string &error);
    /** Reads a glTF 2.0 file (.glb or .gltf): one object per mesh node with its node transform baked in; base colours and textures become materials. */
    bool import_gltf(const fs::path &file, Scene &scene, std::string &error);
    /** Picks the reader from the extension (.obj, .msh, .glb, .gltf). */
    bool import_mesh_file(const fs::path &file, Scene &scene, std::string &error);
    /** Writes every visible object as a binary glTF: one node per object, one primitive per material, textures embedded. */
    bool export_glb(const Scene &scene, const fs::path &file, std::string &error);
    /** Writes the scene back to a mesh file Kosmos reads: .glb, or .obj (with its .mtl and textures beside it). */
    bool write_mesh_file(const Scene &scene, const fs::path &file, std::string &error);
}
