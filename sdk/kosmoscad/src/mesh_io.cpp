#include "mesh_io.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace cad
{
    using jv = nlohmann::json; // parsing; cad::json is the ordered flavour

    namespace
    {
        std::string lower(std::string s)
        {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        std::string ext_of(const fs::path &p) { return lower(p.extension().string()); }

        bool read_all(const fs::path &file, std::vector<unsigned char> &out)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in)
                return false;
            out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        std::string safe_name(const std::string &s)
        {
            std::string out;
            for (char c : s)
                out += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
            return out;
        }

        // ---- Orbiter .msh -------------------------------------------------------------------------------------

        std::vector<double> msh_numbers(const std::string &line)
        {
            std::vector<double> out;
            std::istringstream ss(line.substr(0, line.find(';')));
            std::string t;
            while (ss >> t)
                out.push_back(std::strtod(t.c_str(), nullptr)); // "-1.#IND" and friends read as 0
            return out;
        }

        std::vector<std::string> msh_words(const std::string &line)
        {
            std::vector<std::string> out;
            std::istringstream ss(line.substr(0, line.find(';')));
            std::string t;
            while (ss >> t)
                out.push_back(t);
            return out;
        }

        std::string trim(const std::string &s)
        {
            const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        }

        bool ends_with_display(const std::string &label)
        {
            auto ends = [&](const char *suffix)
            {
                const size_t n = std::strlen(suffix);
                return label.size() >= n && label.compare(label.size() - n, n, suffix) == 0;
            };
            return ends("DISPLAY") || ends("DISP");
        }

        fs::path find_orbiter_texture(const fs::path &dir, const std::string &name)
        {
            std::error_code ec;
            for (const char *rel : {"", "Textures", "../Textures", "../../Textures", "../Textures2", "../../Textures2", "../textures", "../../textures"})
            {
                const fs::path p = (dir / rel / name).lexically_normal();
                if (fs::exists(p, ec))
                    return p;
            }
            return {};
        }
    }

    bool is_mesh_file(const fs::path &file)
    {
        const std::string e = ext_of(file);
        return e == ".obj" || e == ".msh" || e == ".glb" || e == ".gltf";
    }

    bool import_msh(const fs::path &file, Scene &scene, std::string &error)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
        {
            error = "cannot read " + file.string();
            return false;
        }
        std::vector<std::string> lines;
        for (std::string l; std::getline(in, l);)
        {
            if (!l.empty() && l.back() == '\r')
                l.pop_back();
            lines.push_back(l);
        }
        if (lines.empty() || lines[0].rfind("MSHX1", 0) != 0)
        {
            error = "not an Orbiter MSHX1 mesh";
            return false;
        }

        struct Group
        {
            std::string label;
            int material = 0, texture = 0, flag = 0;
            bool nonormal = false;
            std::vector<std::array<double, 8>> verts;
            std::vector<std::array<int, 3>> tris;
        };
        struct Mat
        {
            std::string name;
            V3 diffuse{1, 1, 1};
        };
        std::vector<Group> groups;
        std::vector<Mat> mats;
        std::vector<std::string> textures;

        size_t i = 1;
        auto line_at = [&](size_t k) -> const std::string & { static const std::string empty; return k < lines.size() ? lines[k] : empty; };
        while (i < lines.size())
        {
            const std::vector<std::string> w = msh_words(lines[i++]);
            if (w.empty())
                continue;
            std::string key = w[0];
            for (char &c : key)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (key == "GROUPS" && w.size() > 1)
            {
                const int count = std::atoi(w[1].c_str());
                for (int g = 0; g < count && i < lines.size(); ++g)
                {
                    Group grp;
                    while (i < lines.size())
                    {
                        const std::string cur = lines[i++];
                        const std::vector<std::string> gw = msh_words(cur);
                        if (gw.empty())
                            continue;
                        std::string k = gw[0];
                        for (char &c : k)
                            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        if (k == "LABEL")
                            grp.label = trim(cur.substr(cur.find_first_not_of(" \t") + 5));
                        else if (k == "MATERIAL" && gw.size() > 1)
                            grp.material = std::atoi(gw[1].c_str());
                        else if (k == "TEXTURE" && gw.size() > 1)
                            grp.texture = std::atoi(gw[1].c_str());
                        else if (k == "FLAG" && gw.size() > 1)
                            grp.flag = std::atoi(gw[1].c_str());
                        else if (k == "NONORMAL")
                            grp.nonormal = true;
                        else if (k == "GEOM" && gw.size() > 2)
                        {
                            const int nv = std::atoi(gw[1].c_str()), nt = std::atoi(gw[2].c_str());
                            if (nv < 0 || nt < 0 || i + static_cast<size_t>(nv) + static_cast<size_t>(nt) > lines.size())
                            {
                                error = "truncated GEOM block";
                                return false;
                            }
                            for (int n = 0; n < nv; ++n)
                            {
                                std::vector<double> v = msh_numbers(lines[i++]);
                                std::array<double, 8> a{};
                                if (grp.nonormal)
                                {
                                    for (int c = 0; c < 3 && c < static_cast<int>(v.size()); ++c)
                                        a[c] = v[c];
                                    a[6] = v.size() > 3 ? v[3] : 0;
                                    a[7] = v.size() > 4 ? v[4] : 0;
                                }
                                else
                                    for (int c = 0; c < 8 && c < static_cast<int>(v.size()); ++c)
                                        a[c] = v[c];
                                grp.verts.push_back(a);
                            }
                            for (int n = 0; n < nt; ++n)
                            {
                                const std::vector<double> t = msh_numbers(lines[i++]);
                                if (t.size() >= 3)
                                    grp.tris.push_back({static_cast<int>(t[0]), static_cast<int>(t[1]), static_cast<int>(t[2])});
                            }
                            break;
                        }
                    }
                    groups.push_back(std::move(grp));
                }
            }
            else if (key == "MATERIALS" && w.size() > 1)
            {
                const int count = std::atoi(w[1].c_str());
                std::vector<std::string> names;
                for (int n = 0; n < count; ++n)
                    names.push_back(trim(line_at(i++)));
                std::map<std::string, V3> by_name;
                while (by_name.size() < names.size() && i < lines.size())
                {
                    const std::string cur = lines[i++];
                    const std::vector<std::string> mw = msh_words(cur);
                    if (!mw.empty() && (mw[0] == "MATERIAL" || mw[0] == "material"))
                    {
                        const std::string name = trim(cur.substr(cur.find_first_not_of(" \t") + 8));
                        const std::vector<double> d = msh_numbers(line_at(i));
                        i += 4;
                        by_name[name] = d.size() >= 3 ? V3(d[0], d[1], d[2]) : V3(1, 1, 1);
                    }
                }
                for (const std::string &n : names)
                {
                    Mat m;
                    m.name = n;
                    if (auto it = by_name.find(n); it != by_name.end())
                        m.diffuse = it->second;
                    mats.push_back(m);
                }
            }
            else if (key == "TEXTURES" && w.size() > 1)
            {
                const int count = std::atoi(w[1].c_str());
                for (int n = 0; n < count; ++n)
                {
                    const std::vector<std::string> tw = msh_words(line_at(i++));
                    textures.push_back(tw.empty() ? std::string() : tw[0]);
                }
            }
        }

        const fs::path dir = file.parent_path();
        size_t added = 0;
        int index = 0;
        for (const Group &g : groups)
        {
            ++index;
            const bool display = ends_with_display(g.label);
            if (g.tris.empty() || g.verts.empty() || ((g.flag & 2) && !display))
                continue; // Orbiter does not draw a group with the "obsolete" flag unless the vessel switches it on
            const Mat *mat = g.material > 0 && g.material <= static_cast<int>(mats.size()) ? &mats[g.material - 1] : nullptr;
            const std::string tex = g.texture > 0 && g.texture <= static_cast<int>(textures.size()) ? textures[g.texture - 1] : std::string();
            std::string base = safe_name(mat ? mat->name : "default");
            while (!base.empty() && base.front() == '_')
                base.erase(base.begin());
            if (base.empty())
                base = "default";
            std::string name = base;
            if (!tex.empty())
                name += "_" + safe_name(fs::path(tex).stem().string());
            if (display)
                name += "_" + safe_name(g.label);

            int m = find_material(scene, name);
            if (m < 0)
            {
                Material nm;
                nm.name = name;
                nm.colour = tex.empty() && mat ? mat->diffuse : V3(1, 1, 1);
                if (!tex.empty())
                    nm.texture = find_orbiter_texture(dir, tex).string();
                scene.materials.push_back(nm);
                m = static_cast<int>(scene.materials.size()) - 1;
            }

            Object o;
            o.name = unique_object_name(scene, g.label.empty() ? "group" + std::to_string(index) : safe_name(g.label));
            for (const auto &v : g.verts)
            {
                o.mesh.v.push_back({v[0], v[1], -v[2]});
                o.mesh.uv.push_back({v[6], display ? v[7] : 1.0 - v[7]}); // a render target is stored bottom-up
            }
            const int nv = static_cast<int>(g.verts.size());
            for (const auto &t : g.tris)
            {
                if (t[0] < 0 || t[1] < 0 || t[2] < 0 || t[0] >= nv || t[1] >= nv || t[2] >= nv)
                    continue;
                Face f;
                f.v = {t[0], t[1], t[2]};
                f.uv = f.v;
                f.mat = m;
                o.mesh.f.push_back(std::move(f));
            }
            if (o.mesh.empty())
                continue;
            weld(o.mesh, 1e-6);
            scene.objects.push_back(std::move(o));
            ++added;
        }
        if (!added)
        {
            error = "the mesh has no visible groups";
            return false;
        }
        return true;
    }

    // ---- glTF ----------------------------------------------------------------------------------------------

    namespace
    {
        using Mat4 = std::array<double, 16>; // column-major, as glTF stores it

        Mat4 identity()
        {
            Mat4 m{};
            m[0] = m[5] = m[10] = m[15] = 1;
            return m;
        }

        Mat4 multiply(const Mat4 &a, const Mat4 &b)
        {
            Mat4 r{};
            for (int c = 0; c < 4; ++c)
                for (int rr = 0; rr < 4; ++rr)
                    for (int k = 0; k < 4; ++k)
                        r[c * 4 + rr] += a[k * 4 + rr] * b[c * 4 + k];
            return r;
        }

        Mat4 node_matrix(const jv &n)
        {
            if (n.contains("matrix") && n["matrix"].is_array() && n["matrix"].size() == 16)
            {
                Mat4 m;
                for (int i = 0; i < 16; ++i)
                    m[i] = n["matrix"][i].get<double>();
                return m;
            }
            double t[3] = {0, 0, 0}, s[3] = {1, 1, 1}, q[4] = {0, 0, 0, 1};
            auto read = [&](const char *key, double *dst, int count)
            {
                if (n.contains(key) && n[key].is_array() && static_cast<int>(n[key].size()) == count)
                    for (int i = 0; i < count; ++i)
                        dst[i] = n[key][i].get<double>();
            };
            read("translation", t, 3);
            read("scale", s, 3);
            read("rotation", q, 4);
            const double len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (len > 1e-12)
                for (double &c : q)
                    c /= len;
            const double x = q[0], y = q[1], z = q[2], w = q[3];
            Mat4 m = identity();
            m[0] = (1 - 2 * (y * y + z * z)) * s[0];
            m[1] = (2 * (x * y + z * w)) * s[0];
            m[2] = (2 * (x * z - y * w)) * s[0];
            m[4] = (2 * (x * y - z * w)) * s[1];
            m[5] = (1 - 2 * (x * x + z * z)) * s[1];
            m[6] = (2 * (y * z + x * w)) * s[1];
            m[8] = (2 * (x * z + y * w)) * s[2];
            m[9] = (2 * (y * z - x * w)) * s[2];
            m[10] = (1 - 2 * (x * x + y * y)) * s[2];
            m[12] = t[0];
            m[13] = t[1];
            m[14] = t[2];
            return m;
        }

        double determinant3(const Mat4 &m)
        {
            return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
        }

        std::vector<unsigned char> base64_decode(const std::string &s)
        {
            static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::vector<unsigned char> out;
            unsigned acc = 0;
            int bits = 0;
            for (char c : s)
            {
                const size_t p = chars.find(c);
                if (p == std::string::npos)
                    continue;
                acc = (acc << 6) | static_cast<unsigned>(p);
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back(static_cast<unsigned char>((acc >> bits) & 0xFF));
                }
            }
            return out;
        }

        std::string uri_decode(const std::string &s)
        {
            std::string out;
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2])))
                {
                    out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
                    i += 2;
                }
                else
                    out += s[i];
            }
            return out;
        }

        struct Gltf
        {
            jv j;
            fs::path file;
            std::vector<std::vector<unsigned char>> buffers;

            bool view_bytes(int view, const unsigned char *&data, size_t &size) const
            {
                if (view < 0 || !j.contains("bufferViews") || view >= static_cast<int>(j["bufferViews"].size()))
                    return false;
                const jv &bv = j["bufferViews"][view];
                const int b = bv.value("buffer", 0);
                if (b < 0 || b >= static_cast<int>(buffers.size()))
                    return false;
                const size_t off = bv.value("byteOffset", 0), len = bv.value("byteLength", 0);
                if (off + len > buffers[b].size())
                    return false;
                data = buffers[b].data() + off;
                size = len;
                return true;
            }

            // Reads an accessor as doubles, `comps` per element; normalised integers are scaled to 0..1 (or -1..1).
            bool read(int index, std::vector<double> &out, int &comps) const
            {
                if (!j.contains("accessors") || index < 0 || index >= static_cast<int>(j["accessors"].size()))
                    return false;
                const jv &a = j["accessors"][index];
                const std::string type = a.value("type", "SCALAR");
                comps = type == "SCALAR" ? 1 : (type == "VEC2" ? 2 : (type == "VEC3" ? 3 : (type == "VEC4" ? 4 : 0)));
                const int ct = a.value("componentType", 0);
                const int csize = (ct == 5120 || ct == 5121) ? 1 : ((ct == 5122 || ct == 5123) ? 2 : ((ct == 5125 || ct == 5126) ? 4 : 0));
                const size_t count = a.value("count", 0);
                if (!comps || !csize)
                    return false;
                out.assign(count * comps, 0.0);
                if (!a.contains("bufferView"))
                    return true; // all zeros by definition
                const jv &bv = j["bufferViews"][a["bufferView"].get<int>()];
                const unsigned char *base = nullptr;
                size_t size = 0;
                if (!view_bytes(a["bufferView"].get<int>(), base, size))
                    return false;
                const size_t stride = bv.contains("byteStride") ? bv["byteStride"].get<size_t>() : static_cast<size_t>(comps * csize);
                const size_t start = a.value("byteOffset", 0);
                const bool norm = a.value("normalized", false);
                for (size_t e = 0; e < count; ++e)
                    for (int c = 0; c < comps; ++c)
                    {
                        const size_t at = start + e * stride + static_cast<size_t>(c * csize);
                        if (at + csize > size)
                            return false;
                        const unsigned char *p = base + at;
                        double v = 0;
                        switch (ct)
                        {
                        case 5120: v = static_cast<int8_t>(p[0]); if (norm) v = std::max(v / 127.0, -1.0); break;
                        case 5121: v = p[0]; if (norm) v /= 255.0; break;
                        case 5122: { int16_t s; std::memcpy(&s, p, 2); v = s; if (norm) v = std::max(v / 32767.0, -1.0); break; }
                        case 5123: { uint16_t s; std::memcpy(&s, p, 2); v = s; if (norm) v /= 65535.0; break; }
                        case 5125: { uint32_t s; std::memcpy(&s, p, 4); v = s; break; }
                        case 5126: { float s; std::memcpy(&s, p, 4); v = s; break; }
                        }
                        out[e * comps + c] = v;
                    }
                return true;
            }
        };

        // A texture file for a glTF image: an external uri beside the file, or the embedded bytes written to a temp folder.
        std::string image_path(const Gltf &g, int image)
        {
            if (!g.j.contains("images") || image < 0 || image >= static_cast<int>(g.j["images"].size()))
                return {};
            const jv &im = g.j["images"][image];
            if (im.contains("uri"))
            {
                const std::string uri = im["uri"].get<std::string>();
                if (uri.rfind("data:", 0) == 0)
                    return {};
                return (g.file.parent_path() / uri_decode(uri)).string();
            }
            if (im.contains("bufferView"))
            {
                const unsigned char *data = nullptr;
                size_t size = 0;
                if (!g.view_bytes(im["bufferView"].get<int>(), data, size))
                    return {};
                const std::string mime = im.value("mimeType", "image/png");
                std::error_code ec;
                const fs::path dir = fs::temp_directory_path(ec) / "kosmoscad-textures";
                fs::create_directories(dir, ec);
                const fs::path out = dir / (safe_name(g.file.stem().string()) + "_" + std::to_string(image) + (mime == "image/jpeg" ? ".jpg" : ".png"));
                std::ofstream f(out, std::ios::binary);
                f.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
                return f ? out.string() : std::string();
            }
            return {};
        }
    }

    bool import_gltf(const fs::path &file, Scene &scene, std::string &error)
    {
        std::vector<unsigned char> bytes;
        if (!read_all(file, bytes))
        {
            error = "cannot read " + file.string();
            return false;
        }
        Gltf g;
        g.file = file;
        std::string text;
        std::vector<unsigned char> glb_bin;
        auto u32 = [&](size_t at) { uint32_t v = 0; std::memcpy(&v, bytes.data() + at, 4); return v; };
        if (bytes.size() >= 12 && std::memcmp(bytes.data(), "glTF", 4) == 0)
        {
            size_t at = 12;
            while (at + 8 <= bytes.size())
            {
                const uint32_t len = u32(at), type = u32(at + 4);
                at += 8;
                if (at + len > bytes.size())
                    break;
                if (type == 0x4E4F534A && text.empty())
                    text.assign(reinterpret_cast<const char *>(bytes.data() + at), len);
                else if (type == 0x004E4942 && glb_bin.empty())
                    glb_bin.assign(bytes.begin() + static_cast<long>(at), bytes.begin() + static_cast<long>(at + len));
                at += (len + 3) & ~3u;
            }
        }
        else
            text.assign(bytes.begin(), bytes.end());
        g.j = jv::parse(text, nullptr, false);
        if (g.j.is_discarded() || !g.j.is_object())
        {
            error = "not a glTF file";
            return false;
        }
        if (g.j.contains("buffers"))
            for (size_t b = 0; b < g.j["buffers"].size(); ++b)
            {
                const jv &bj = g.j["buffers"][b];
                std::vector<unsigned char> data;
                if (!bj.contains("uri"))
                    data = glb_bin;
                else
                {
                    const std::string uri = bj["uri"].get<std::string>();
                    if (uri.rfind("data:", 0) == 0)
                    {
                        const size_t comma = uri.find(',');
                        data = base64_decode(comma == std::string::npos ? std::string() : uri.substr(comma + 1));
                    }
                    else if (!read_all(file.parent_path() / uri_decode(uri), data))
                    {
                        error = "missing buffer file " + uri;
                        return false;
                    }
                }
                g.buffers.push_back(std::move(data));
            }

        // glTF materials -> scene materials
        std::vector<int> mat_map;
        const size_t gm = g.j.contains("materials") ? g.j["materials"].size() : 0;
        for (size_t m = 0; m < gm; ++m)
        {
            const jv &mj = g.j["materials"][m];
            std::string name = safe_name(mj.value("name", "material" + std::to_string(m)));
            if (name.empty())
                name = "material" + std::to_string(m);
            Material mat;
            mat.name = name;
            auto tex_of = [&](const jv &slot) -> std::string
            {
                if (!slot.is_object() || !slot.contains("index") || !g.j.contains("textures"))
                    return {};
                const int ti = slot["index"].get<int>();
                if (ti < 0 || ti >= static_cast<int>(g.j["textures"].size()))
                    return {};
                return image_path(g, g.j["textures"][ti].value("source", -1));
            };
            if (mj.contains("normalTexture"))
                mat.normal_map = tex_of(mj["normalTexture"]);
            if (mj.contains("emissiveFactor") && mj["emissiveFactor"].is_array() && mj["emissiveFactor"].size() >= 3)
                mat.emissive = {mj["emissiveFactor"][0].get<double>(), mj["emissiveFactor"][1].get<double>(), mj["emissiveFactor"][2].get<double>()};
            if (mj.contains("pbrMetallicRoughness"))
            {
                const jv &p = mj["pbrMetallicRoughness"];
                mat.metallic = p.value("metallicFactor", 1.0);
                mat.roughness = p.value("roughnessFactor", 1.0);
                if (p.contains("metallicRoughnessTexture"))
                    mat.spec_map = tex_of(p["metallicRoughnessTexture"]);
                if (p.contains("baseColorFactor") && p["baseColorFactor"].is_array() && p["baseColorFactor"].size() >= 3)
                    mat.colour = {p["baseColorFactor"][0].get<double>(), p["baseColorFactor"][1].get<double>(), p["baseColorFactor"][2].get<double>()};
                else
                    mat.colour = {1, 1, 1};
                if (p.contains("baseColorTexture") && p["baseColorTexture"].contains("index") && g.j.contains("textures"))
                {
                    const int ti = p["baseColorTexture"]["index"].get<int>();
                    if (ti >= 0 && ti < static_cast<int>(g.j["textures"].size()))
                        mat.texture = image_path(g, g.j["textures"][ti].value("source", -1));
                }
            }
            int idx = find_material(scene, name);
            if (idx < 0)
            {
                scene.materials.push_back(mat);
                idx = static_cast<int>(scene.materials.size()) - 1;
            }
            mat_map.push_back(idx);
        }
        int default_mat = -1;

        size_t added = 0, skipped = 0;
        auto add_mesh_node = [&](const jv &node, int node_index, const Mat4 &world)
        {
            const int mi = node.value("mesh", -1);
            if (mi < 0 || !g.j.contains("meshes") || mi >= static_cast<int>(g.j["meshes"].size()))
                return;
            const jv &mj = g.j["meshes"][mi];
            Object o;
            const std::string label = node.value("name", mj.value("name", "mesh" + std::to_string(node_index)));
            o.name = unique_object_name(scene, safe_name(label).empty() ? "mesh" : safe_name(label));
            const bool flip = determinant3(world) < 0;
            if (!mj.contains("primitives"))
                return;
            for (const jv &pj : mj["primitives"])
            {
                if (pj.value("mode", 4) != 4 || !pj.contains("attributes") || !pj["attributes"].contains("POSITION"))
                {
                    ++skipped;
                    continue;
                }
                std::vector<double> pos, tex, ind;
                int pc = 0, tc = 0, ic = 0;
                if (!g.read(pj["attributes"]["POSITION"].get<int>(), pos, pc) || pc != 3)
                    continue;
                const bool has_uv = pj["attributes"].contains("TEXCOORD_0") && g.read(pj["attributes"]["TEXCOORD_0"].get<int>(), tex, tc) && tc == 2 && tex.size() / 2 == pos.size() / 3;
                const size_t nv = pos.size() / 3;
                if (pj.contains("indices"))
                {
                    if (!g.read(pj["indices"].get<int>(), ind, ic))
                        continue;
                }
                else
                    for (size_t k = 0; k < nv; ++k)
                        ind.push_back(static_cast<double>(k));
                int mat;
                if (pj.contains("material") && pj["material"].get<int>() >= 0 && pj["material"].get<int>() < static_cast<int>(mat_map.size()))
                    mat = mat_map[pj["material"].get<int>()];
                else
                {
                    if (default_mat < 0)
                    {
                        default_mat = find_material(scene, "default");
                        if (default_mat < 0)
                        {
                            Material dm;
                            dm.name = "default";
                            scene.materials.push_back(dm);
                            default_mat = static_cast<int>(scene.materials.size()) - 1;
                        }
                    }
                    mat = default_mat;
                }
                const int vbase = static_cast<int>(o.mesh.v.size()), ubase = static_cast<int>(o.mesh.uv.size());
                for (size_t k = 0; k < nv; ++k)
                {
                    const double x = pos[k * 3], y = pos[k * 3 + 1], z = pos[k * 3 + 2];
                    o.mesh.v.push_back({world[0] * x + world[4] * y + world[8] * z + world[12],
                                        world[1] * x + world[5] * y + world[9] * z + world[13],
                                        world[2] * x + world[6] * y + world[10] * z + world[14]});
                    if (has_uv)
                        o.mesh.uv.push_back({tex[k * 2], 1.0 - tex[k * 2 + 1]});
                }
                for (size_t k = 0; k + 2 < ind.size(); k += 3)
                {
                    const int a = static_cast<int>(ind[k]), b = static_cast<int>(ind[k + 1]), c = static_cast<int>(ind[k + 2]);
                    if (a < 0 || b < 0 || c < 0 || a >= static_cast<int>(nv) || b >= static_cast<int>(nv) || c >= static_cast<int>(nv))
                        continue;
                    Face f;
                    f.v = {vbase + a, vbase + b, vbase + c};
                    if (has_uv)
                        f.uv = {ubase + a, ubase + b, ubase + c};
                    if (flip)
                    {
                        std::reverse(f.v.begin(), f.v.end());
                        std::reverse(f.uv.begin(), f.uv.end());
                    }
                    f.mat = mat;
                    o.mesh.f.push_back(std::move(f));
                }
            }
            if (o.mesh.empty())
                return;
            weld(o.mesh, 1e-6);
            scene.objects.push_back(std::move(o));
            ++added;
        };

        const jv empty_array = jv::array();
        const jv &nodes = g.j.contains("nodes") ? g.j["nodes"] : empty_array;
        std::vector<int> roots;
        if (g.j.contains("scenes") && !g.j["scenes"].empty())
        {
            const jv &sc = g.j["scenes"][g.j.value("scene", 0) < static_cast<int>(g.j["scenes"].size()) ? g.j.value("scene", 0) : 0];
            if (sc.contains("nodes"))
                for (const jv &n : sc["nodes"])
                    roots.push_back(n.get<int>());
        }
        else
        {
            std::set<int> child;
            for (const jv &n : nodes)
                if (n.contains("children"))
                    for (const jv &c : n["children"])
                        child.insert(c.get<int>());
            for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
                if (!child.count(i))
                    roots.push_back(i);
        }
        std::function<void(int, const Mat4 &, int)> walk = [&](int index, const Mat4 &parent, int depth)
        {
            if (index < 0 || index >= static_cast<int>(nodes.size()) || depth > 64)
                return;
            const jv &n = nodes[index];
            const Mat4 world = multiply(parent, node_matrix(n));
            add_mesh_node(n, index, world);
            if (n.contains("children"))
                for (const jv &c : n["children"])
                    walk(c.get<int>(), world, depth + 1);
        };
        for (int r : roots)
            walk(r, identity(), 0);
        if (nodes.empty() && g.j.contains("meshes"))
            for (int m = 0; m < static_cast<int>(g.j["meshes"].size()); ++m)
                add_mesh_node(jv{{"mesh", m}}, m, identity());
        if (!added)
        {
            error = skipped ? "the file has no triangle meshes (only points or lines)" : "the file has no meshes";
            return false;
        }
        return true;
    }

    bool import_mesh_file(const fs::path &file, Scene &scene, std::string &error)
    {
        const std::string e = ext_of(file);
        if (e == ".obj")
            return import_obj(file, scene, error);
        if (e == ".msh")
            return import_msh(file, scene, error);
        if (e == ".glb" || e == ".gltf")
            return import_gltf(file, scene, error);
        error = "unknown mesh format '" + e + "' (use .obj, .msh, .glb or .gltf)";
        return false;
    }

    // ---- GLB writer -----------------------------------------------------------------------------------------

    bool export_glb(const Scene &scene, const fs::path &file, std::string &error)
    {
        using ordered = nlohmann::ordered_json;
        std::vector<unsigned char> bin;
        ordered views = ordered::array(), accessors = ordered::array(), meshes = ordered::array(), nodes = ordered::array();
        ordered materials = ordered::array(), images = ordered::array(), textures = ordered::array();
        std::vector<int> mat_map(scene.materials.size(), -1), tex_map(scene.materials.size(), -1);
        std::map<std::string, int> image_of;

        auto add_view = [&](const void *data, size_t size, int target) -> int
        {
            while (bin.size() % 4)
                bin.push_back(0);
            ordered v = {{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", size}};
            if (target)
                v["target"] = target;
            const unsigned char *p = static_cast<const unsigned char *>(data);
            bin.insert(bin.end(), p, p + size);
            views.push_back(v);
            return static_cast<int>(views.size()) - 1;
        };
        auto add_accessor = [&](int view, int component, size_t count, const char *type) -> ordered &
        {
            accessors.push_back({{"bufferView", view}, {"componentType", component}, {"count", count}, {"type", type}});
            return accessors.back();
        };

        auto material_index = [&](int m) -> int
        {
            if (m < 0 || m >= static_cast<int>(scene.materials.size()))
                m = 0;
            if (mat_map[m] >= 0)
                return mat_map[m];
            const Material &sm = scene.materials[m];
            ordered pbr = {{"baseColorFactor", {sm.colour.x, sm.colour.y, sm.colour.z, 1.0}}, {"metallicFactor", sm.metallic}, {"roughnessFactor", sm.roughness}};
            // Embeds a PNG or JPG and returns its glTF texture index, or -1 (DDS and missing files are skipped).
            auto embed = [&](const std::string &path) -> int
            {
                const std::string e = ext_of(path);
                if (path.empty() || (e != ".png" && e != ".jpg" && e != ".jpeg"))
                    return -1;
                const fs::path tp = resolve_texture(scene, path);
                std::vector<unsigned char> data;
                if (!read_all(tp, data))
                    return -1;
                auto it = image_of.find(tp.string());
                if (it == image_of.end())
                {
                    const int view = add_view(data.data(), data.size(), 0);
                    images.push_back({{"bufferView", view}, {"mimeType", e == ".png" ? "image/png" : "image/jpeg"}});
                    textures.push_back({{"source", static_cast<int>(images.size()) - 1}});
                    it = image_of.emplace(tp.string(), static_cast<int>(textures.size()) - 1).first;
                }
                return it->second;
            };
            if (const int t = embed(sm.texture); t >= 0)
            {
                pbr["baseColorTexture"] = {{"index", t}};
                pbr["baseColorFactor"] = {1.0, 1.0, 1.0, 1.0};
            }
            if (const int t = embed(sm.spec_map); t >= 0)
                pbr["metallicRoughnessTexture"] = {{"index", t}};
            ordered mat = {{"name", sm.name}, {"pbrMetallicRoughness", pbr}, {"doubleSided", true}};
            if (const int t = embed(sm.normal_map); t >= 0)
                mat["normalTexture"] = {{"index", t}};
            if (sm.emissive.x > 0 || sm.emissive.y > 0 || sm.emissive.z > 0)
                mat["emissiveFactor"] = {std::min(1.0, sm.emissive.x), std::min(1.0, sm.emissive.y), std::min(1.0, sm.emissive.z)};
            materials.push_back(mat);
            return mat_map[m] = static_cast<int>(materials.size()) - 1;
        };

        size_t written = 0;
        for (const Object &o : scene.objects)
        {
            if (!o.visible)
                continue;
            const Mesh wm = world_mesh(o);
            std::vector<V3> vn(wm.v.size());
            if (o.smooth)
            {
                for (size_t f = 0; f < wm.f.size(); ++f)
                {
                    const V3 n = face_normal(wm, static_cast<int>(f));
                    for (int vi : wm.f[f].v)
                        vn[vi] += n;
                }
                for (V3 &n : vn)
                    n = normalized(n);
            }
            std::map<int, std::vector<int>> by_material;
            for (size_t f = 0; f < wm.f.size(); ++f)
                by_material[wm.f[f].mat].push_back(static_cast<int>(f));
            ordered prims = ordered::array();
            for (const auto &[mat, faces] : by_material)
            {
                std::vector<float> pos, nrm, uv;
                std::vector<uint32_t> idx;
                V3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
                for (int fi : faces)
                {
                    const Face &f = wm.f[fi];
                    const V3 fn = normalized(face_normal(wm, fi));
                    const bool has_uv = f.uv.size() == f.v.size();
                    for (const Tri &t : triangulate_face(wm, fi))
                        for (int vi : t)
                        {
                            const V3 &p = wm.v[vi];
                            const V3 n = o.smooth ? vn[vi] : fn;
                            size_t k = 0;
                            while (k < f.v.size() && f.v[k] != vi)
                                ++k;
                            V2 u;
                            if (has_uv && k < f.uv.size() && f.uv[k] >= 0 && f.uv[k] < static_cast<int>(wm.uv.size()))
                                u = wm.uv[f.uv[k]];
                            idx.push_back(static_cast<uint32_t>(pos.size() / 3));
                            pos.insert(pos.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
                            nrm.insert(nrm.end(), {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
                            uv.insert(uv.end(), {static_cast<float>(u.u), static_cast<float>(1.0 - u.v)});
                            for (int a = 0; a < 3; ++a)
                            {
                                lo[a] = std::min(lo[a], p[a]);
                                hi[a] = std::max(hi[a], p[a]);
                            }
                        }
                }
                if (idx.empty())
                    continue;
                const int vp = add_view(pos.data(), pos.size() * 4, 34962), vnn = add_view(nrm.data(), nrm.size() * 4, 34962),
                          vt = add_view(uv.data(), uv.size() * 4, 34962), vi = add_view(idx.data(), idx.size() * 4, 34963);
                ordered &ap = add_accessor(vp, 5126, pos.size() / 3, "VEC3");
                ap["min"] = {lo.x, lo.y, lo.z};
                ap["max"] = {hi.x, hi.y, hi.z};
                const int a_pos = static_cast<int>(accessors.size()) - 1;
                add_accessor(vnn, 5126, nrm.size() / 3, "VEC3");
                const int a_nrm = static_cast<int>(accessors.size()) - 1;
                add_accessor(vt, 5126, uv.size() / 2, "VEC2");
                const int a_uv = static_cast<int>(accessors.size()) - 1;
                add_accessor(vi, 5125, idx.size(), "SCALAR");
                const int a_idx = static_cast<int>(accessors.size()) - 1;
                prims.push_back({{"attributes", {{"POSITION", a_pos}, {"NORMAL", a_nrm}, {"TEXCOORD_0", a_uv}}}, {"indices", a_idx}, {"material", material_index(mat)}, {"mode", 4}});
            }
            if (prims.empty())
                continue;
            meshes.push_back({{"name", o.name}, {"primitives", prims}});
            nodes.push_back({{"name", o.name}, {"mesh", static_cast<int>(meshes.size()) - 1}});
            ++written;
        }
        if (!written)
        {
            error = "there is nothing to write (no visible objects)";
            return false;
        }
        ordered scene_nodes = ordered::array();
        for (size_t i = 0; i < nodes.size(); ++i)
            scene_nodes.push_back(static_cast<int>(i));
        ordered root = {{"asset", {{"version", "2.0"}, {"generator", "KosmosCAD"}}}, {"scene", 0}, {"scenes", {{{"nodes", scene_nodes}}}}, {"nodes", nodes},
                        {"meshes", meshes}, {"materials", materials}, {"accessors", accessors}, {"bufferViews", views}, {"buffers", {{{"byteLength", bin.size()}}}}};
        if (!images.empty())
        {
            root["images"] = images;
            root["textures"] = textures;
        }
        std::string text = root.dump();
        while (text.size() % 4)
            text += ' ';
        while (bin.size() % 4)
            bin.push_back(0);
        const uint32_t total = static_cast<uint32_t>(12 + 8 + text.size() + 8 + bin.size());
        std::ofstream out(file, std::ios::binary);
        if (!out)
        {
            error = "cannot write " + file.string();
            return false;
        }
        auto put = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); };
        out.write("glTF", 4);
        put(2);
        put(total);
        put(static_cast<uint32_t>(text.size()));
        put(0x4E4F534A);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        put(static_cast<uint32_t>(bin.size()));
        put(0x004E4942);
        out.write(reinterpret_cast<const char *>(bin.data()), static_cast<std::streamsize>(bin.size()));
        if (!out)
        {
            error = "write failed for " + file.string();
            return false;
        }
        return true;
    }

    bool write_mesh_file(const Scene &scene, const fs::path &file, std::string &error)
    {
        const std::string e = ext_of(file);
        if (e == ".glb")
            return export_glb(scene, file, error);
        if (e == ".obj")
            return export_obj(scene, file.parent_path().empty() ? fs::path(".") : file.parent_path(), file.stem().string(), error);
        error = "KosmosCAD writes .glb and .obj parts back to Kosmos, not " + e;
        return false;
    }
}
