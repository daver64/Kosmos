#pragma once

#include "clutter.h"

#include <filesystem>
#include <string>

namespace kosmos
{
    /**
     * One "Edit in Blender" session. Blender is started on a mesh file with a small script (data/blender/kosmos_edit.py)
     * that imports it and writes it back whenever the modder saves. The original is kept as `<file>.bak` so the edit can
     * be cancelled. Kosmos itself only starts the process and watches whether it is still running; the edited file is
     * picked up by the normal model reload.
     */
    class BlenderEdit
    {
    public:
        BlenderEdit() = default;
        BlenderEdit(const BlenderEdit &) = delete;
        BlenderEdit &operator=(const BlenderEdit &) = delete;
        ~BlenderEdit();

        bool start(const std::string &blender, const std::filesystem::path &script, const std::filesystem::path &mesh, std::string &error);
        /** True while Blender is running; polls the process. */
        bool active();
        /** Asks Blender to quit. */
        void stop();
        /** Puts the original file back from `<file>.bak`. */
        bool restore();
        const std::filesystem::path &file() const { return file_; }

    private:
        std::filesystem::path file_;
        long long process_ = 0; // pid on Linux, process handle on Windows
        bool running_ = false;
    };

    /** Writes a mesh as a binary glTF with positions, normals, colours and UVs. */
    bool write_glb(const sl::MeshData &mesh, const std::filesystem::path &file, std::string &error);
}
