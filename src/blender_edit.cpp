#include "blender_edit.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>

#include "json.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace kosmos
{
    BlenderEdit::~BlenderEdit()
    {
        // Leave a running Blender alone: the modder may still have unsaved work in it.
#ifdef _WIN32
        if (process_)
            CloseHandle(reinterpret_cast<HANDLE>(process_));
#endif
    }

    MeshEditorChoice mesh_editor_choice(const std::string &editor, const std::string &blender, const std::string &cad, const std::filesystem::path &root)
    {
        namespace fs = std::filesystem;
        MeshEditorChoice c;
        c.cad = editor == "kosmoscad";
        if (c.cad)
        {
            if (!cad.empty())
                c.exe = cad;
            else
            {
#ifdef _WIN32
                const fs::path bundled = root / "addons" / "kosmoscad" / "bin" / "kosmoscad.exe";
#else
                const fs::path bundled = root / "addons" / "kosmoscad" / "bin" / "kosmoscad";
#endif
                std::error_code ec;
                c.exe = fs::exists(bundled, ec) ? bundled.string() : "kosmoscad";
            }
        }
        else
        {
            c.exe = blender.empty() ? "blender" : blender;
            c.script = root / "data" / "blender" / "kosmos_edit.py";
        }
        return c;
    }

    bool BlenderEdit::start(const MeshEditorChoice &editor, const std::filesystem::path &mesh, std::string &error)
    {
        namespace fs = std::filesystem;
        const std::string &exe = editor.exe;
        if (running_ && active())
        {
            error = name_ + " is already open on " + file_.filename().string();
            return false;
        }
        std::error_code ec;
        if (!editor.cad && !fs::exists(editor.script, ec))
        {
            error = "missing " + editor.script.string();
            return false;
        }
        if (!fs::exists(mesh, ec))
        {
            error = "missing " + mesh.string();
            return false;
        }
        fs::path backup = mesh;
        backup += ".bak";
        // The backup is of the file as this session found it, so Cancel never undoes an earlier session's work.
        fs::copy_file(mesh, backup, fs::copy_options::overwrite_existing, ec);
        file_ = mesh;
        const std::string hint = editor.cad ? "cannot start '" + exe + "' (build KosmosCAD, or set its path in the part editor)" : "cannot start '" + exe + "' (set the Blender path in the part editor)";
#ifdef _WIN32
        std::string command = editor.cad ? "\"" + exe + "\" --edit-mesh \"" + mesh.string() + "\"" : "\"" + exe + "\" --python \"" + editor.script.string() + "\" -- \"" + mesh.string() + "\"";
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        {
            error = hint;
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = reinterpret_cast<long long>(pi.hProcess);
#else
        const std::string script_arg = editor.script.string(), mesh_arg = mesh.string();
        char *blender_argv[] = {const_cast<char *>(exe.c_str()), const_cast<char *>("--python"), const_cast<char *>(script_arg.c_str()), const_cast<char *>("--"), const_cast<char *>(mesh_arg.c_str()), nullptr};
        char *cad_argv[] = {const_cast<char *>(exe.c_str()), const_cast<char *>("--edit-mesh"), const_cast<char *>(mesh_arg.c_str()), nullptr};
        pid_t pid = 0;
        const int rc = posix_spawnp(&pid, exe.c_str(), nullptr, nullptr, editor.cad ? cad_argv : blender_argv, environ);
        if (rc != 0)
        {
            error = hint;
            return false;
        }
        process_ = pid;
#endif
        name_ = editor.name();
        running_ = true;
        return true;
    }

    bool BlenderEdit::active()
    {
        if (!running_)
            return false;
#ifdef _WIN32
        if (WaitForSingleObject(reinterpret_cast<HANDLE>(process_), 0) == WAIT_TIMEOUT)
            return true;
        CloseHandle(reinterpret_cast<HANDLE>(process_));
#else
        int status = 0;
        if (waitpid(static_cast<pid_t>(process_), &status, WNOHANG) == 0)
            return true;
#endif
        process_ = 0;
        running_ = false;
        return false;
    }

    void BlenderEdit::stop()
    {
        if (!running_)
            return;
#ifdef _WIN32
        TerminateProcess(reinterpret_cast<HANDLE>(process_), 0);
#else
        kill(static_cast<pid_t>(process_), SIGTERM);
#endif
    }

    bool BlenderEdit::restore()
    {
        namespace fs = std::filesystem;
        fs::path backup = file_;
        backup += ".bak";
        std::error_code ec;
        if (!fs::exists(backup, ec))
            return false;
        fs::copy_file(backup, file_, fs::copy_options::overwrite_existing, ec);
        return !ec;
    }

    bool write_part_model(const std::filesystem::path &ship_file, int stage, int part, const std::string &model, std::string &error)
    {
        nlohmann::json json;
        {
            std::ifstream in(ship_file);
            if (!in)
            {
                error = "cannot open " + ship_file.string();
                return false;
            }
            json = nlohmann::json::parse(in, nullptr, false);
        }
        if (!json.is_object() || !json.contains("stages") || stage < 0 || stage >= static_cast<int>(json["stages"].size()))
        {
            error = "stage " + std::to_string(stage) + " not found in " + ship_file.filename().string();
            return false;
        }
        nlohmann::json &js = json["stages"][static_cast<std::size_t>(stage)];
        if (js.contains("kits"))
        {
            error = "this stage is built from kits; edit the kit instead";
            return false;
        }
        if (!js.contains("parts") || part < 0 || part >= static_cast<int>(js["parts"].size()))
        {
            error = "part " + std::to_string(part) + " not found in " + ship_file.filename().string();
            return false;
        }
        nlohmann::json &entry = js["parts"][static_cast<std::size_t>(part)];
        if (entry.contains("model") && entry["model"] == model)
            return true;
        nlohmann::json replacement = {{"model", model}};
        if (entry.contains("offset"))
            replacement["offset"] = entry["offset"];
        entry = replacement;
        std::filesystem::path backup = ship_file;
        backup += ".bak";
        std::error_code ec;
        if (!std::filesystem::exists(backup, ec))
            std::filesystem::copy_file(ship_file, backup, ec);
        std::ofstream out(ship_file);
        if (!out)
        {
            error = "cannot write " + ship_file.string();
            return false;
        }
        out << json.dump(1) << "\n";
        return true;
    }

    bool write_glb(const sl::MeshData &mesh, const std::filesystem::path &file, std::string &error)
    {
        return write_glb({{file.stem().string(), mesh}}, file, error);
    }

    bool write_glb(const std::vector<GlbNode> &nodes, const std::filesystem::path &file, std::string &error)
    {
        using J = nlohmann::json;
        std::vector<std::uint8_t> bin;
        J views = J::array(), accessors = J::array(), meshes = J::array(), scene_nodes = J::array(), node_list = J::array();
        auto add_view = [&](const void *data, std::size_t bytes, int target)
        {
            while (bin.size() % 4)
                bin.push_back(0);
            views.push_back(J::object({{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", bytes}, {"target", target}}));
            const auto *p = static_cast<const std::uint8_t *>(data);
            bin.insert(bin.end(), p, p + bytes);
            return static_cast<int>(views.size()) - 1;
        };
        auto add_accessor = [&](int view, int component, std::size_t count, const char *type)
        {
            accessors.push_back(J::object({{"bufferView", view}, {"componentType", component}, {"count", count}, {"type", type}}));
            return static_cast<int>(accessors.size()) - 1;
        };
        for (const GlbNode &node : nodes)
        {
            const sl::MeshData &mesh = node.mesh;
            const std::size_t n = mesh.vertices.size(), ni = mesh.indices.size();
            if (n == 0 || ni == 0)
                continue;
            std::vector<float> positions, normals, colours, uvs;
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (const sl::Vertex3D &v : mesh.vertices)
            {
                for (int i = 0; i < 3; ++i)
                {
                    positions.push_back(v.position[i]);
                    normals.push_back(v.normal[i]);
                    lo[i] = std::min(lo[i], v.position[i]);
                    hi[i] = std::max(hi[i], v.position[i]);
                }
                for (int i = 0; i < 4; ++i)
                    colours.push_back(v.colour[i]);
                uvs.push_back(v.uv[0]);
                uvs.push_back(v.uv[1]);
            }
            const int ap = add_accessor(add_view(positions.data(), positions.size() * 4, 34962), 5126, n, "VEC3");
            accessors[static_cast<std::size_t>(ap)]["min"] = J::array({lo[0], lo[1], lo[2]});
            accessors[static_cast<std::size_t>(ap)]["max"] = J::array({hi[0], hi[1], hi[2]});
            const int an = add_accessor(add_view(normals.data(), normals.size() * 4, 34962), 5126, n, "VEC3");
            const int ac = add_accessor(add_view(colours.data(), colours.size() * 4, 34962), 5126, n, "VEC4");
            const int au = add_accessor(add_view(uvs.data(), uvs.size() * 4, 34962), 5126, n, "VEC2");
            const int ai = add_accessor(add_view(mesh.indices.data(), ni * 4, 34963), 5125, ni, "SCALAR");
            J attributes = J::object({{"POSITION", ap}, {"NORMAL", an}, {"COLOR_0", ac}, {"TEXCOORD_0", au}});
            J primitive = J::object({{"attributes", attributes}, {"indices", ai}, {"material", 0}});
            meshes.push_back(J::object({{"name", node.name}, {"primitives", J::array({primitive})}}));
            node_list.push_back(J::object({{"mesh", static_cast<int>(meshes.size()) - 1}, {"name", node.name}}));
            scene_nodes.push_back(static_cast<int>(node_list.size()) - 1);
        }
        if (meshes.empty())
        {
            error = "there is no geometry to export";
            return false;
        }
        J gltf = J::object();
        gltf["asset"] = J::object({{"version", "2.0"}, {"generator", "Kosmos"}});
        gltf["scene"] = 0;
        gltf["scenes"] = J::array({J::object({{"nodes", scene_nodes}})});
        gltf["nodes"] = node_list;
        gltf["meshes"] = meshes;
        J pbr = J::object({{"metallicFactor", 0.0}, {"roughnessFactor", 0.7}});
        gltf["materials"] = J::array({J::object({{"name", "kosmos"}, {"pbrMetallicRoughness", pbr}, {"doubleSided", true}})});
        gltf["accessors"] = accessors;
        gltf["bufferViews"] = views;
        while (bin.size() % 4)
            bin.push_back(0);
        gltf["buffers"] = J::array({J::object({{"byteLength", bin.size()}})});
        std::string json = gltf.dump();
        while (json.size() % 4)
            json.push_back(' ');
        const std::uint32_t magic = 0x46546C67, version = 2, json_type = 0x4E4F534A, bin_type = 0x004E4942;
        const std::uint32_t json_len = static_cast<std::uint32_t>(json.size()), bin_len = static_cast<std::uint32_t>(bin.size());
        const std::uint32_t total = 12 + 8 + json_len + 8 + bin_len;
        std::ofstream out(file, std::ios::binary);
        if (!out)
        {
            error = "cannot write " + file.string();
            return false;
        }
        auto put = [&](const void *d, std::size_t bytes) { out.write(static_cast<const char *>(d), static_cast<std::streamsize>(bytes)); };
        put(&magic, 4);
        put(&version, 4);
        put(&total, 4);
        put(&json_len, 4);
        put(&json_type, 4);
        put(json.data(), json.size());
        put(&bin_len, 4);
        put(&bin_type, 4);
        put(bin.data(), bin.size());
        return static_cast<bool>(out);
    }
}
