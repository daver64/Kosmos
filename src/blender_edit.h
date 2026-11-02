#pragma once

#include "clutter.h"

#include <filesystem>
#include <string>
#include <vector>

namespace kosmos
{
    /**
     * One "Edit in Blender" session. Blender is started on a mesh file with a small script (data/blender/kosmos_edit.py)
     * that imports it and writes it back whenever the modder saves. The original is kept as `<file>.bak` so the edit can
     * be cancelled. Kosmos itself only starts the process and watches whether it is still running; the edited file is
     * picked up by the normal model reload.
     */
    /** Which program edits a mesh: Blender (with data/blender/kosmos_edit.py) or KosmosCAD (`kosmoscad --edit-mesh FILE`). */
    struct MeshEditorChoice
    {
        bool cad = false;
        std::string exe;
        std::filesystem::path script; // Blender's helper script; unused for KosmosCAD
        std::string name() const { return cad ? "KosmosCAD" : "Blender"; }
    };
    /** `editor` is "blender" or "kosmoscad"; a blank KosmosCAD path means the copy in <root>/addons/kosmoscad/bin. */
    MeshEditorChoice mesh_editor_choice(const std::string &editor, const std::string &blender, const std::string &cad, const std::filesystem::path &root);

    class BlenderEdit
    {
    public:
        BlenderEdit() = default;
        BlenderEdit(const BlenderEdit &) = delete;
        BlenderEdit &operator=(const BlenderEdit &) = delete;
        ~BlenderEdit();

        bool start(const MeshEditorChoice &editor, const std::filesystem::path &mesh, std::string &error);
        /** "Blender" or "KosmosCAD": the editor of the current or last session. */
        const std::string &name() const { return name_; }
        /** True while Blender is running; polls the process. */
        bool active();
        /** Asks Blender to quit. */
        void stop();
        /** Puts the original file back from `<file>.bak`. */
        bool restore();
        const std::filesystem::path &file() const { return file_; }

    private:
        std::filesystem::path file_;
        std::string name_ = "Blender";
        long long process_ = 0; // pid on Linux, process handle on Windows
        bool running_ = false;
    };

    /**
     * Rewrites one part of a ship file to use `model` (a path relative to the addon), keeping its offset and keeping the
     * original as `<file>.bak` the first time. Fails for a stage built from kits or a part that is not at that index.
     */
    bool write_part_model(const std::filesystem::path &ship_file, int stage, int part, const std::string &model, std::string &error);

    /** One named object of a glTF scene; Blender shows each as its own object, ready to edit or join. */
    struct GlbNode
    {
        std::string name;
        sl::MeshData mesh;
    };

    /** Writes a mesh as a binary glTF with positions, normals, colours and UVs. */
    bool write_glb(const sl::MeshData &mesh, const std::filesystem::path &file, std::string &error);
    /** As above, with one scene object per node. */
    bool write_glb(const std::vector<GlbNode> &nodes, const std::filesystem::path &file, std::string &error);
}
