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

    bool BlenderEdit::start(const std::string &blender, const std::filesystem::path &script, const std::filesystem::path &mesh, std::string &error)
    {
        namespace fs = std::filesystem;
        if (running_ && active())
        {
            error = "Blender is already open on " + file_.filename().string();
            return false;
        }
        std::error_code ec;
        if (!fs::exists(script, ec))
        {
            error = "missing " + script.string();
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
#ifdef _WIN32
        std::string command = "\"" + blender + "\" --python \"" + script.string() + "\" -- \"" + mesh.string() + "\"";
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        {
            error = "cannot start '" + blender + "' (set the Blender path in the part editor)";
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = reinterpret_cast<long long>(pi.hProcess);
#else
        const std::string script_arg = script.string(), mesh_arg = mesh.string();
        char *argv[] = {const_cast<char *>(blender.c_str()), const_cast<char *>("--python"), const_cast<char *>(script_arg.c_str()), const_cast<char *>("--"), const_cast<char *>(mesh_arg.c_str()), nullptr};
        pid_t pid = 0;
        const int rc = posix_spawnp(&pid, blender.c_str(), nullptr, nullptr, argv, environ);
        if (rc != 0)
        {
            error = "cannot start '" + blender + "' (set the Blender path in the part editor)";
            return false;
        }
        process_ = pid;
#endif
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
        const std::size_t n = mesh.vertices.size(), ni = mesh.indices.size();
        if (n == 0 || ni == 0)
        {
            error = "the part has no geometry";
            return false;
        }
        std::vector<std::uint8_t> bin;
        auto append = [&](const void *data, std::size_t bytes)
        {
            const auto *p = static_cast<const std::uint8_t *>(data);
            bin.insert(bin.end(), p, p + bytes);
        };
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
        nlohmann::json views = nlohmann::json::array();
        auto view = [&](const void *data, std::size_t bytes, int target)
        {
            views.push_back({{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", bytes}, {"target", target}});
            append(data, bytes);
            return static_cast<int>(views.size()) - 1;
        };
        const int vp = view(positions.data(), positions.size() * 4, 34962), vn = view(normals.data(), normals.size() * 4, 34962),
                  vc = view(colours.data(), colours.size() * 4, 34962), vu = view(uvs.data(), uvs.size() * 4, 34962),
                  vi = view(mesh.indices.data(), ni * 4, 34963);
        using J = nlohmann::json;
        auto accessor = [&](int bufferView, int component, std::size_t count, const char *type)
        {
            J a = J::object();
            a["bufferView"] = bufferView;
            a["componentType"] = component;
            a["count"] = count;
            a["type"] = type;
            return a;
        };
        J position = accessor(vp, 5126, n, "VEC3");
        position["min"] = J::array({lo[0], lo[1], lo[2]});
        position["max"] = J::array({hi[0], hi[1], hi[2]});
        J gltf = J::object();
        gltf["asset"] = J::object({{"version", "2.0"}, {"generator", "Kosmos"}});
        gltf["scene"] = 0;
        gltf["scenes"] = J::array({J::object({{"nodes", J::array({0})}})});
        gltf["nodes"] = J::array({J::object({{"mesh", 0}, {"name", file.stem().string()}})});
        J attributes = J::object({{"POSITION", 0}, {"NORMAL", 1}, {"COLOR_0", 2}, {"TEXCOORD_0", 3}});
        J primitive = J::object({{"attributes", attributes}, {"indices", 4}, {"material", 0}});
        gltf["meshes"] = J::array({J::object({{"primitives", J::array({primitive})}})});
        J pbr = J::object({{"metallicFactor", 0.0}, {"roughnessFactor", 0.7}});
        gltf["materials"] = J::array({J::object({{"name", "kosmos"}, {"pbrMetallicRoughness", pbr}, {"doubleSided", true}})});
        gltf["accessors"] = J::array({position, accessor(vn, 5126, n, "VEC3"), accessor(vc, 5126, n, "VEC4"), accessor(vu, 5126, n, "VEC2"), accessor(vi, 5125, ni, "SCALAR")});
        gltf["bufferViews"] = views;
        gltf["buffers"] = J::array({J::object({{"byteLength", bin.size()}})});
        std::string json = gltf.dump();
        while (json.size() % 4)
            json.push_back(' ');
        while (bin.size() % 4)
            bin.push_back(0);
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
