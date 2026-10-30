// Mesh loading for ShipEdit's preview: Wavefront OBJ and binary glTF, positions and triangles only.
#include "model.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <stdexcept>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

namespace shipedit
{
    namespace
    {
        using Mat = std::array<float, 16>; // column-major, as glTF writes it

        Mat identity()
        {
            Mat m{};
            m[0] = m[5] = m[10] = m[15] = 1.0f;
            return m;
        }

        Mat multiply(const Mat &a, const Mat &b)
        {
            Mat r{};
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 4; ++row)
                {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; ++k)
                        sum += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
                    r[static_cast<std::size_t>(c * 4 + row)] = sum;
                }
            return r;
        }

        Mat node_matrix(const json &node)
        {
            if (node.contains("matrix") && node["matrix"].size() == 16)
            {
                Mat m;
                for (std::size_t i = 0; i < 16; ++i)
                    m[i] = node["matrix"][i].get<float>();
                return m;
            }
            float t[3] = {0, 0, 0}, s[3] = {1, 1, 1}, q[4] = {0, 0, 0, 1};
            if (node.contains("translation"))
                for (std::size_t i = 0; i < 3; ++i)
                    t[i] = node["translation"][i].get<float>();
            if (node.contains("scale"))
                for (std::size_t i = 0; i < 3; ++i)
                    s[i] = node["scale"][i].get<float>();
            if (node.contains("rotation"))
                for (std::size_t i = 0; i < 4; ++i)
                    q[i] = node["rotation"][i].get<float>();
            const float x = q[0], y = q[1], z = q[2], w = q[3];
            Mat m = identity();
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

        struct Glb
        {
            json doc;
            std::vector<unsigned char> bin;

            const unsigned char *accessor_data(int index, std::size_t &count, std::size_t &stride, int &component, int &components) const
            {
                const json &a = doc["accessors"].at(static_cast<std::size_t>(index));
                const json &view = doc["bufferViews"].at(a.at("bufferView").get<std::size_t>());
                count = a.at("count").get<std::size_t>();
                component = a.at("componentType").get<int>();
                const std::string type = a.at("type").get<std::string>();
                components = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : 4;
                static const int sizes[] = {1, 1, 2, 2, 0, 4, 4};
                const std::size_t item = static_cast<std::size_t>(sizes[component - 5120 < 7 && component >= 5120 ? component - 5120 : 0]) * static_cast<std::size_t>(components);
                stride = view.value("byteStride", static_cast<std::size_t>(0));
                if (stride == 0)
                    stride = item;
                const std::size_t offset = view.value("byteOffset", static_cast<std::size_t>(0)) + a.value("byteOffset", static_cast<std::size_t>(0));
                if (offset + (count ? (count - 1) * stride + item : 0) > bin.size())
                    throw std::runtime_error("accessor runs past the buffer");
                return bin.data() + offset;
            }
        };

        void add_primitive(const Glb &g, const json &prim, const Mat &m, Mesh &out)
        {
            if (!prim.contains("attributes") || !prim["attributes"].contains("POSITION"))
                return;
            std::size_t count = 0, stride = 0;
            int component = 0, components = 0;
            const unsigned char *p = g.accessor_data(prim["attributes"]["POSITION"].get<int>(), count, stride, component, components);
            if (component != 5126 || components != 3)
                return;
            const unsigned base = static_cast<unsigned>(out.xyz.size() / 3);
            for (std::size_t i = 0; i < count; ++i)
            {
                float v[3];
                std::memcpy(v, p + i * stride, sizeof v);
                out.xyz.push_back(m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12]);
                out.xyz.push_back(m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13]);
                out.xyz.push_back(m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14]);
            }
            std::vector<unsigned> idx;
            if (prim.contains("indices"))
            {
                std::size_t n = 0, st = 0;
                int comp = 0, comps = 0;
                const unsigned char *d = g.accessor_data(prim["indices"].get<int>(), n, st, comp, comps);
                for (std::size_t i = 0; i < n; ++i)
                {
                    unsigned v = 0;
                    if (comp == 5121)
                        v = d[i * st];
                    else if (comp == 5123)
                    {
                        std::uint16_t s;
                        std::memcpy(&s, d + i * st, 2);
                        v = s;
                    }
                    else
                        std::memcpy(&v, d + i * st, 4);
                    idx.push_back(v);
                }
            }
            else
                for (std::size_t i = 0; i < count; ++i)
                    idx.push_back(static_cast<unsigned>(i));
            const int mode = prim.value("mode", 4);
            if (mode != 4)
                return;
            for (std::size_t i = 0; i + 2 < idx.size(); i += 3)
                if (idx[i] < count && idx[i + 1] < count && idx[i + 2] < count)
                    for (std::size_t k = 0; k < 3; ++k)
                        out.triangles.push_back(base + idx[i + k]);
        }

        void walk(const Glb &g, int node_index, const Mat &parent, Mesh &out, int depth)
        {
            if (depth > 64)
                return;
            const json &node = g.doc["nodes"].at(static_cast<std::size_t>(node_index));
            const Mat m = multiply(parent, node_matrix(node));
            if (node.contains("mesh"))
                for (const json &prim : g.doc["meshes"].at(node["mesh"].get<std::size_t>()).value("primitives", json::array()))
                    add_primitive(g, prim, m, out);
            for (const json &child : node.value("children", json::array()))
                walk(g, child.get<int>(), m, out, depth + 1);
        }

        bool load_glb(const fs::path &file, Mesh &out, std::string &error)
        {
            std::ifstream in(file, std::ios::binary);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (bytes.size() < 20 || std::memcmp(bytes.data(), "glTF", 4) != 0)
            {
                error = "not a binary glTF file";
                return false;
            }
            Glb g;
            bool have_json = false;
            std::size_t pos = 12;
            while (pos + 8 <= bytes.size())
            {
                std::uint32_t len, type;
                std::memcpy(&len, &bytes[pos], 4);
                std::memcpy(&type, &bytes[pos + 4], 4);
                pos += 8;
                if (pos + len > bytes.size())
                    break;
                if (type == 0x4E4F534Au)
                {
                    g.doc = json::parse(bytes.begin() + static_cast<std::ptrdiff_t>(pos), bytes.begin() + static_cast<std::ptrdiff_t>(pos + len));
                    have_json = true;
                }
                else if (type == 0x004E4942u && g.bin.empty())
                    g.bin.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos), bytes.begin() + static_cast<std::ptrdiff_t>(pos + len));
                pos += len;
            }
            if (!have_json)
            {
                error = "no JSON chunk in the glTF file";
                return false;
            }
            try
            {
                const json scenes = g.doc.value("scenes", json::array());
                const std::size_t scene = g.doc.value("scene", static_cast<std::size_t>(0));
                if (scene < scenes.size())
                    for (const json &n : scenes[scene].value("nodes", json::array()))
                        walk(g, n.get<int>(), identity(), out, 0);
                else
                    for (const json &prim : g.doc["meshes"][0].value("primitives", json::array()))
                        add_primitive(g, prim, identity(), out);
            }
            catch (const std::exception &e)
            {
                error = e.what();
                return false;
            }
            return true;
        }

        bool load_obj(const fs::path &file, Mesh &out, std::string &error)
        {
            std::ifstream in(file);
            if (!in)
            {
                error = "cannot open the file";
                return false;
            }
            std::string line;
            while (std::getline(in, line))
            {
                if (line.size() > 2 && line[0] == 'v' && line[1] == ' ')
                {
                    float x = 0, y = 0, z = 0;
                    if (std::sscanf(line.c_str() + 2, "%f %f %f", &x, &y, &z) == 3)
                    {
                        out.xyz.push_back(x);
                        out.xyz.push_back(y);
                        out.xyz.push_back(z);
                    }
                }
                else if (line.size() > 2 && line[0] == 'f' && line[1] == ' ')
                {
                    std::istringstream ss(line.substr(2));
                    std::string tok;
                    std::vector<unsigned> face;
                    const long total = static_cast<long>(out.xyz.size() / 3);
                    while (ss >> tok)
                    {
                        long i = std::strtol(tok.c_str(), nullptr, 10);
                        i = i < 0 ? total + i : i - 1;
                        if (i >= 0 && i < total)
                            face.push_back(static_cast<unsigned>(i));
                    }
                    for (std::size_t k = 1; k + 1 < face.size(); ++k)
                    {
                        out.triangles.push_back(face[0]);
                        out.triangles.push_back(face[k]);
                        out.triangles.push_back(face[k + 1]);
                    }
                }
            }
            return true;
        }
    } // namespace

    bool load_mesh(const fs::path &file, Mesh &out, std::string &error)
    {
        out = Mesh();
        std::string ext = file.extension().string();
        for (char &c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool ok = false;
        if (ext == ".obj")
            ok = load_obj(file, out, error);
        else if (ext == ".glb")
            ok = load_glb(file, out, error);
        else
            error = "unsupported model format " + ext;
        if (ok && out.empty())
        {
            error = "no triangles found";
            return false;
        }
        return ok;
    }
} // namespace shipedit
