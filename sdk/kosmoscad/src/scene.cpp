#include "scene.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace cad
{
    void Document::checkpoint()
    {
        undo_.push_back(scene);
        if (undo_.size() > 64)
            undo_.erase(undo_.begin());
        redo_.clear();
        dirty = true;
    }

    void Document::undo()
    {
        if (undo_.empty())
            return;
        redo_.push_back(std::move(scene));
        scene = std::move(undo_.back());
        undo_.pop_back();
        dirty = true;
    }

    void Document::redo()
    {
        if (redo_.empty())
            return;
        undo_.push_back(std::move(scene));
        scene = std::move(redo_.back());
        redo_.pop_back();
        dirty = true;
    }

    void Document::clear_history()
    {
        undo_.clear();
        redo_.clear();
    }

    int find_material(const Scene &scene, const std::string &name)
    {
        for (size_t i = 0; i < scene.materials.size(); ++i)
            if (scene.materials[i].name == name)
                return static_cast<int>(i);
        return -1;
    }

    std::string unique_material_name(const Scene &scene, const std::string &base)
    {
        std::string name = base.empty() ? "part" : base;
        for (int n = 2; find_material(scene, name) >= 0; ++n)
            name = (base.empty() ? "part" : base) + "_" + std::to_string(n);
        return name;
    }

    std::string unique_object_name(const Scene &scene, const std::string &base)
    {
        auto used = [&](const std::string &n)
        {
            for (const Object &o : scene.objects)
                if (o.name == n)
                    return true;
            return false;
        };
        std::string name = base.empty() ? "object" : base;
        for (int n = 2; used(name); ++n)
            name = (base.empty() ? "object" : base) + "_" + std::to_string(n);
        return name;
    }

    Mesh world_mesh(const Object &o)
    {
        Mesh m = o.mesh;
        apply_xform(m, o.xf);
        return m;
    }

    namespace
    {
        json v3_json(const V3 &v) { return json::array({v.x, v.y, v.z}); }

        V3 v3_from(const json &j, const V3 &fallback)
        {
            if (!j.is_array() || j.size() < 3)
                return fallback;
            return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
        }

        json mesh_json(const Mesh &m)
        {
            json j;
            json &v = j["v"] = json::array();
            for (const V3 &p : m.v)
            {
                v.push_back(p.x);
                v.push_back(p.y);
                v.push_back(p.z);
            }
            if (!m.uv.empty())
            {
                json &uv = j["uv"] = json::array();
                for (const V2 &t : m.uv)
                {
                    uv.push_back(t.u);
                    uv.push_back(t.v);
                }
            }
            json &f = j["f"] = json::array();
            for (const Face &face : m.f)
            {
                json e;
                e["m"] = face.mat;
                e["v"] = face.v;
                if (!face.uv.empty())
                    e["t"] = face.uv;
                f.push_back(std::move(e));
            }
            return j;
        }

        bool mesh_from_json(const json &j, Mesh &m, std::string &error)
        {
            const json &v = j.value("v", json::array());
            for (size_t i = 0; i + 2 < v.size(); i += 3)
                m.v.push_back({v[i].get<double>(), v[i + 1].get<double>(), v[i + 2].get<double>()});
            const json &uv = j.value("uv", json::array());
            for (size_t i = 0; i + 1 < uv.size(); i += 2)
                m.uv.push_back({uv[i].get<double>(), uv[i + 1].get<double>()});
            for (const json &e : j.value("f", json::array()))
            {
                Face face;
                face.mat = e.value("m", 0);
                face.v = e.value("v", std::vector<int>());
                face.uv = e.value("t", std::vector<int>());
                for (int i : face.v)
                    if (i < 0 || i >= static_cast<int>(m.v.size()))
                    {
                        error = "face refers to a missing vertex";
                        return false;
                    }
                if (face.uv.size() != face.v.size())
                    face.uv.clear();
                for (int i : face.uv)
                    if (i < 0 || i >= static_cast<int>(m.uv.size()))
                        face.uv.clear();
                if (face.v.size() >= 3)
                    m.f.push_back(std::move(face));
            }
            return true;
        }
    }

    json scene_to_json(const Scene &scene)
    {
        json j;
        j["format"] = "kosmoscad";
        j["version"] = 1;
        j["note"] = scene.ship_note;
        j["ship_file"] = scene.ship_file;
        if (!scene.ship_root.is_null())
            j["ship_root"] = scene.ship_root;
        json &mats = j["materials"] = json::array();
        for (const Material &m : scene.materials)
            mats.push_back({{"name", m.name}, {"colour", v3_json(m.colour)}, {"texture", m.texture}});
        json &objs = j["objects"] = json::array();
        for (const Object &o : scene.objects)
        {
            json e;
            e["name"] = o.name;
            e["pos"] = v3_json(o.xf.pos);
            e["rot"] = v3_json(o.xf.rot);
            e["scale"] = v3_json(o.xf.scale);
            e["visible"] = o.visible;
            e["smooth"] = o.smooth;
            e["stage"] = o.stage;
            if (!o.part.is_null())
                e["part"] = o.part;
            e["source"] = o.source;
            e["mesh"] = mesh_json(o.mesh);
            objs.push_back(std::move(e));
        }
        json &marks = j["markers"] = json::array();
        for (const Marker &m : scene.markers)
            marks.push_back({{"kind", m.kind}, {"name", m.name}, {"pos", v3_json(m.pos)}, {"rot", v3_json(m.rot)},
                             {"size", v3_json(m.size)}, {"stage", m.stage}, {"placed", m.placed}, {"source", m.source}, {"props", m.props}});
        return j;
    }

    bool scene_from_json(const json &j, Scene &scene, std::string &error)
    {
        try
        {
            if (j.value("format", "") != "kosmoscad")
            {
                error = "not a KosmosCAD scene";
                return false;
            }
            Scene s;
            s.materials.clear();
            s.ship_note = j.value("note", "");
            s.ship_file = j.value("ship_file", "");
            s.ship_root = j.value("ship_root", json());
            for (const json &m : j.value("materials", json::array()))
            {
                Material mat;
                mat.name = m.value("name", "part");
                mat.colour = v3_from(m.value("colour", json()), mat.colour);
                mat.texture = m.value("texture", "");
                s.materials.push_back(mat);
            }
            if (s.materials.empty())
                s.materials.push_back(Material{});
            for (const json &e : j.value("objects", json::array()))
            {
                Object o;
                o.name = e.value("name", "object");
                o.xf.pos = v3_from(e.value("pos", json()), {});
                o.xf.rot = v3_from(e.value("rot", json()), {});
                o.xf.scale = v3_from(e.value("scale", json()), {1, 1, 1});
                o.visible = e.value("visible", true);
                o.smooth = e.value("smooth", false);
                o.stage = e.value("stage", 0);
                o.part = e.value("part", json());
                o.source = e.value("source", "");
                if (!mesh_from_json(e.value("mesh", json::object()), o.mesh, error))
                    return false;
                for (Face &f : o.mesh.f)
                    f.mat = std::clamp(f.mat, 0, static_cast<int>(s.materials.size()) - 1);
                s.objects.push_back(std::move(o));
            }
            for (const json &e : j.value("markers", json::array()))
            {
                Marker m;
                m.kind = e.value("kind", "");
                m.name = e.value("name", "");
                m.pos = v3_from(e.value("pos", json()), {});
                m.rot = v3_from(e.value("rot", json()), {});
                m.size = v3_from(e.value("size", json()), {1, 1, 1});
                m.stage = e.value("stage", 0);
                m.placed = e.value("placed", true);
                m.source = e.value("source", "");
                m.props = e.value("props", json::object());
                s.markers.push_back(std::move(m));
            }
            scene = std::move(s);
            return true;
        }
        catch (const std::exception &ex)
        {
            error = ex.what();
            return false;
        }
    }

    bool save_kmodel(const Scene &scene, const fs::path &file, std::string &error)
    {
        std::ofstream out(file);
        if (!out)
        {
            error = "cannot write " + file.string();
            return false;
        }
        out << scene_to_json(scene).dump(1);
        return static_cast<bool>(out);
    }

    bool load_kmodel(const fs::path &file, Scene &scene, std::string &error)
    {
        std::ifstream in(file);
        if (!in)
        {
            error = "cannot read " + file.string();
            return false;
        }
        json j = json::parse(in, nullptr, false);
        if (j.is_discarded())
        {
            error = "not valid JSON";
            return false;
        }
        return scene_from_json(j, scene, error);
    }

    namespace
    {
        std::string safe_name(std::string s)
        {
            for (char &c : s)
                if (c == ' ' || c == '\t')
                    c = '_';
            return s.empty() ? "part" : s;
        }
    }

    bool export_obj(const Scene &scene, const fs::path &dir, const std::string &name, std::string &error,
                    const std::vector<int> &only, bool world)
    {
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::ofstream obj(dir / (name + ".obj")), mtl(dir / (name + ".mtl"));
        if (!obj || !mtl)
        {
            error = "cannot write into " + dir.string();
            return false;
        }

        struct Baked
        {
            Mesh mesh;
            bool smooth;
            int vbase = 0, tbase = 0, nbase = 0;
            std::vector<V3> normals;   // per face corner, in face order
        };
        std::vector<Baked> baked;
        for (size_t oi = 0; oi < scene.objects.size(); ++oi)
        {
            const Object &o = scene.objects[oi];
            if (o.mesh.empty() || (only.empty() ? !o.visible : std::find(only.begin(), only.end(), static_cast<int>(oi)) == only.end()))
                continue;
            Baked b;
            b.mesh = world ? world_mesh(o) : o.mesh;
            b.smooth = o.smooth;
            baked.push_back(std::move(b));
        }

        obj << "# Exported by KosmosCAD\nmtllib " << name << ".mtl\n";
        int vcount = 0, tcount = 0, ncount = 0;
        for (Baked &b : baked)
        {
            b.vbase = vcount;
            b.tbase = tcount;
            b.nbase = ncount;
            for (const V3 &p : b.mesh.v)
                obj << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
            for (const V2 &t : b.mesh.uv)
                obj << "vt " << t.u << ' ' << t.v << '\n';
            vcount += static_cast<int>(b.mesh.v.size());
            tcount += static_cast<int>(b.mesh.uv.size());
            // One normal per face corner: the face normal when flat, the average of the faces at the vertex when smooth.
            std::vector<V3> vertex_normal(b.mesh.v.size());
            if (b.smooth)
                for (size_t fi = 0; fi < b.mesh.f.size(); ++fi)
                {
                    const V3 n = face_normal(b.mesh, static_cast<int>(fi));
                    for (int vi : b.mesh.f[fi].v)
                        vertex_normal[vi] += n;
                }
            for (size_t fi = 0; fi < b.mesh.f.size(); ++fi)
            {
                const V3 fn = face_normal(b.mesh, static_cast<int>(fi));
                for (int vi : b.mesh.f[fi].v)
                {
                    const V3 n = b.smooth ? normalized(vertex_normal[vi]) : fn;
                    b.normals.push_back(n);
                    obj << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
                    ++ncount;
                }
            }
        }

        std::vector<bool> used(scene.materials.size(), false);
        for (size_t mi = 0; mi < scene.materials.size(); ++mi)
        {
            bool header = false;
            for (const Baked &b : baked)
            {
                int corner = 0;
                for (size_t fi = 0; fi < b.mesh.f.size(); ++fi)
                {
                    const Face &face = b.mesh.f[fi];
                    const int first_corner = corner;
                    corner += static_cast<int>(face.v.size());
                    if (face.mat != static_cast<int>(mi))
                        continue;
                    if (!header)
                    {
                        obj << "g " << safe_name(scene.materials[mi].name) << "\nusemtl " << safe_name(scene.materials[mi].name) << '\n';
                        header = true;
                        used[mi] = true;
                    }
                    obj << 'f';
                    for (size_t k = 0; k < face.v.size(); ++k)
                    {
                        obj << ' ' << b.vbase + face.v[k] + 1 << '/';
                        if (face.uv.size() == face.v.size())
                            obj << b.tbase + face.uv[k] + 1;
                        obj << '/' << b.nbase + first_corner + static_cast<int>(k) + 1;
                    }
                    obj << '\n';
                }
            }
        }

        for (size_t mi = 0; mi < scene.materials.size(); ++mi)
        {
            if (!used[mi])
                continue;
            const Material &m = scene.materials[mi];
            mtl << "newmtl " << safe_name(m.name) << "\nKd " << m.colour.x << ' ' << m.colour.y << ' ' << m.colour.z << "\nKs 0.3 0.3 0.3\n";
            if (!m.texture.empty() && fs::exists(m.texture))
            {
                const fs::path tex_dir = dir / "textures";
                fs::create_directories(tex_dir, ec);
                const fs::path target = tex_dir / fs::path(m.texture).filename();
                if (!fs::exists(target))
                    fs::copy_file(m.texture, target, ec);
                mtl << "map_Kd textures/" << target.filename().string() << '\n';
            }
            mtl << '\n';
        }
        return true;
    }

    namespace
    {
        struct MtlEntry
        {
            V3 colour{0.7, 0.72, 0.75};
            std::string texture;
        };

        std::map<std::string, MtlEntry> read_mtl(const fs::path &file)
        {
            std::map<std::string, MtlEntry> out;
            std::ifstream in(file);
            std::string line, current;
            while (std::getline(in, line))
            {
                std::istringstream ss(line);
                std::string key;
                ss >> key;
                if (key == "newmtl")
                {
                    ss >> current;
                    out[current];
                }
                else if (key == "Kd" && !current.empty())
                    ss >> out[current].colour.x >> out[current].colour.y >> out[current].colour.z;
                else if (key == "map_Kd" && !current.empty())
                {
                    std::string rest;
                    std::getline(ss, rest);
                    const size_t s = rest.find_first_not_of(' ');
                    if (s != std::string::npos)
                        out[current].texture = (file.parent_path() / rest.substr(s)).string();
                }
            }
            return out;
        }
    }

    bool import_obj(const fs::path &file, Scene &scene, std::string &error)
    {
        std::ifstream in(file);
        if (!in)
        {
            error = "cannot read " + file.string();
            return false;
        }
        std::vector<V3> pos;
        std::vector<V2> tex;
        std::map<std::string, MtlEntry> mtl;
        std::map<std::string, int> mat_index; // file material name -> scene index
        struct Group
        {
            std::string name;
            Mesh mesh;
            std::map<int, int> vmap, tmap; // file index -> mesh index
        };
        std::vector<Group> groups(1);
        groups[0].name = file.stem().string();
        int current_mat = 0;

        auto material_for = [&](const std::string &mname) -> int
        {
            auto it = mat_index.find(mname);
            if (it != mat_index.end())
                return it->second;
            // A material of the same name is the same part in Kosmos, so files that share names share the material.
            if (const int existing = find_material(scene, mname); existing >= 0)
                return mat_index[mname] = existing;
            Material m;
            m.name = mname;
            auto mt = mtl.find(mname);
            if (mt != mtl.end())
            {
                m.colour = mt->second.colour;
                m.texture = mt->second.texture;
            }
            scene.materials.push_back(m);
            return mat_index[mname] = static_cast<int>(scene.materials.size()) - 1;
        };

        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream ss(line);
            std::string key;
            ss >> key;
            if (key == "v")
            {
                V3 p;
                ss >> p.x >> p.y >> p.z;
                pos.push_back(p);
            }
            else if (key == "vt")
            {
                V2 t;
                ss >> t.u >> t.v;
                tex.push_back(t);
            }
            else if (key == "mtllib")
            {
                std::string f;
                ss >> f;
                for (auto &[k, v] : read_mtl(file.parent_path() / f))
                    mtl[k] = v;
            }
            else if (key == "usemtl")
            {
                std::string n;
                ss >> n;
                current_mat = material_for(n);
            }
            else if (key == "o" || key == "g")
            {
                std::string n;
                ss >> n;
                if (groups.size() == 1 && groups[0].mesh.empty())
                    groups[0].name = n.empty() ? groups[0].name : n;
                else if (key == "o")
                {
                    groups.emplace_back();
                    groups.back().name = n.empty() ? "object" : n;
                }
            }
            else if (key == "f")
            {
                Group &g = groups.back();
                Face face;
                face.mat = current_mat;
                std::string tok;
                bool all_uv = true;
                while (ss >> tok)
                {
                    int vi = 0, ti = 0;
                    const size_t s1 = tok.find('/');
                    vi = std::atoi(tok.substr(0, s1).c_str());
                    if (s1 != std::string::npos)
                    {
                        const size_t s2 = tok.find('/', s1 + 1);
                        const std::string t = tok.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
                        ti = t.empty() ? 0 : std::atoi(t.c_str());
                    }
                    if (vi < 0)
                        vi += static_cast<int>(pos.size()) + 1;
                    if (ti < 0)
                        ti += static_cast<int>(tex.size()) + 1;
                    if (vi < 1 || vi > static_cast<int>(pos.size()))
                        continue;
                    auto vit = g.vmap.find(vi);
                    if (vit == g.vmap.end())
                    {
                        vit = g.vmap.emplace(vi, static_cast<int>(g.mesh.v.size())).first;
                        g.mesh.v.push_back(pos[vi - 1]);
                    }
                    face.v.push_back(vit->second);
                    if (ti >= 1 && ti <= static_cast<int>(tex.size()))
                    {
                        auto tit = g.tmap.find(ti);
                        if (tit == g.tmap.end())
                        {
                            tit = g.tmap.emplace(ti, static_cast<int>(g.mesh.uv.size())).first;
                            g.mesh.uv.push_back(tex[ti - 1]);
                        }
                        face.uv.push_back(tit->second);
                    }
                    else
                        all_uv = false;
                }
                if (!all_uv)
                    face.uv.clear();
                if (face.v.size() >= 3)
                    g.mesh.f.push_back(std::move(face));
            }
        }
        size_t added = 0;
        for (Group &g : groups)
        {
            if (g.mesh.empty())
                continue;
            Object o;
            o.name = unique_object_name(scene, g.name);
            o.mesh = std::move(g.mesh);
            scene.objects.push_back(std::move(o));
            ++added;
        }
        if (!added)
        {
            error = "no faces in " + file.string();
            return false;
        }
        return true;
    }

    void add_triangles(Scene &scene, const std::string &name, const std::vector<float> &xyz, const std::vector<unsigned> &triangles)
    {
        Object o;
        o.name = unique_object_name(scene, name);
        for (size_t i = 0; i + 2 < xyz.size(); i += 3)
            o.mesh.v.push_back({xyz[i], xyz[i + 1], xyz[i + 2]});
        for (size_t i = 0; i + 2 < triangles.size(); i += 3)
        {
            Face f;
            f.v = {static_cast<int>(triangles[i]), static_cast<int>(triangles[i + 1]), static_cast<int>(triangles[i + 2])};
            o.mesh.f.push_back(std::move(f));
        }
        weld(o.mesh, 1e-6);
        scene.objects.push_back(std::move(o));
    }
}
