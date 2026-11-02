// KosmosCAD scene: objects (mesh + transform), a shared material list, equipment markers, undo/redo, and the file formats
// (.kmodel JSON scene, OBJ/MTL import and export). No GTK or cairo in here, so it is unit-tested headless.
#pragma once

#include "mesh.h"

#include <json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace cad
{
    using json = nlohmann::ordered_json;
    namespace fs = std::filesystem;

    struct Material
    {
        std::string name = "part"; // becomes the OBJ usemtl name, which Kosmos uses as the part name for animation
        V3 colour{0.7, 0.72, 0.75};
        std::string texture;       // path to an image, relative to the model's folder (absolute only for an unsaved scene); empty for none
        std::string normal_map;    // tangent-space normal map (OBJ map_Bump, glTF normalTexture); empty for none
        std::string spec_map;      // glTF-style metallic-roughness image (G roughness, B metallic); empty for none
        double metallic = 0.0;     // 0..1
        double roughness = 0.6;    // 0 mirror .. 1 matt
        V3 emissive{0, 0, 0};      // light the surface gives off, as a colour
    };

    struct Object
    {
        std::string name = "object";
        Mesh mesh;
        Xform xf;
        bool visible = true;
        bool smooth = false;       // export averaged vertex normals instead of flat ones
        int stage = 0;             // the ship.json stage whose parts list it belongs to
        json part;                 // the ship.json part entry it came from (keeps animations and other keys); null for a new object
        std::string source;        // "stage:index" of that part, so saving can replace it
    };

    /** A piece of ship equipment shown as a box or cylinder gizmo: an engine, tank, reactor, vessel, port, gear leg and so on. */
    struct Marker
    {
        std::string kind;          // catalogue key, see equipment.h
        std::string name;
        V3 pos, rot, size{1, 1, 1};
        json props = json::object(); // the ship.json entry's own keys (thrust, capacity_kg, watts ...), edited in the form
        int stage = 0;
        bool placed = true;        // false for a ship.json system that has no position yet; it is listed but not drawn
        std::string source;        // "section:stage:index" it was loaded from
    };

    struct Scene
    {
        std::vector<Material> materials{Material{}};
        std::vector<Object> objects;
        std::vector<Marker> markers;
        std::string ship_note;     // free text kept in the .kmodel
        std::string ship_file;     // the ship.json this scene was opened from or last saved to
        std::string base_dir;      // folder that relative texture paths are relative to (not saved; set by the app from the file location)
        json ship_root;            // that ship.json minus the parts, engines, ports... that became objects and markers (see ship_io.h)
    };

    /** Scene plus an undo/redo history of whole-scene snapshots (scenes are small, so this is simple and exact). */
    class Document
    {
    public:
        Scene scene;
        fs::path file;             // the .kmodel it was loaded from or saved to
        bool dirty = false;

        /** Call before each change; an interactive drag calls it once at the start. */
        void checkpoint();
        bool can_undo() const { return !undo_.empty(); }
        bool can_redo() const { return !redo_.empty(); }
        void undo();
        void redo();
        void clear_history();

    private:
        std::vector<Scene> undo_, redo_;
    };

    json scene_to_json(const Scene &scene);
    bool scene_from_json(const json &j, Scene &scene, std::string &error);
    bool save_kmodel(const Scene &scene, const fs::path &file, std::string &error);
    bool load_kmodel(const fs::path &file, Scene &scene, std::string &error);

    /** Writes `<dir>/<name>.obj` and `.mtl` with every visible object in world space, faces grouped by material
     *  (one usemtl block per material, so Kosmos sees one animatable part per material). Textures are copied to `<dir>/textures/`.
     *  `only` lists object indices to write (all visible ones when empty); `world` false writes the mesh in its own space. */
    bool export_obj(const Scene &scene, const fs::path &dir, const std::string &name, std::string &error,
                    const std::vector<int> &only = {}, bool world = true);
    /** Adds the objects and materials of an OBJ (and its MTL) to the scene; one object per o/g group, or one for the file. */
    bool import_obj(const fs::path &file, Scene &scene, std::string &error);
    /** Adds a triangle soup (such as a GLB read by ShipEdit's loader) as one object. */
    void add_triangles(Scene &scene, const std::string &name, const std::vector<float> &xyz, const std::vector<unsigned> &triangles);

    /** The file a material's image path points at: absolute paths as they are, relative ones against `scene.base_dir`. */
    fs::path resolve_texture(const Scene &scene, const std::string &path);
    /** Makes every material image live in `base` or below it and stored as a relative path: images already under `base` just get
     *  a relative path, others are copied to `<base>/textures/` (a clashing name gets a number). Sets `scene.base_dir = base`.
     *  Returns how many files were copied; images that cannot be found are left alone. */
    int localise_textures(Scene &scene, const fs::path &base);

    int find_material(const Scene &scene, const std::string &name);
    /** A material name that is not yet used: "part", "part_2", ... */
    std::string unique_material_name(const Scene &scene, const std::string &base);
    std::string unique_object_name(const Scene &scene, const std::string &base);
    /** The object's mesh with its transform applied. */
    Mesh world_mesh(const Object &o);
}
