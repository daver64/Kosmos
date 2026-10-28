#undef NDEBUG
#include "equipment.h"
#include "json.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

using namespace kosmos;
namespace fs = std::filesystem;

namespace
{
    void write(const fs::path &path, const std::string &text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path) << text;
    }

    std::vector<std::string> addon_folders(const fs::path &root)
    {
        std::vector<std::string> out;
        for (const fs::directory_entry &e : fs::directory_iterator(root / "addons"))
            if (e.is_directory())
                out.push_back(e.path().string());
        return out;
    }
} // namespace

int main()
{
    // Blast falloff: full at the centre, linear, nothing at or past the edge.
    assert(std::fabs(blast_fraction(0.0, 100.0) - 1.0) < 1e-12);
    assert(std::fabs(blast_fraction(25.0, 100.0) - 0.75) < 1e-12);
    assert(blast_fraction(100.0, 100.0) == 0.0 && blast_fraction(500.0, 100.0) == 0.0);
    assert(blast_fraction(1.0, 0.0) == 0.0);

    // Loader: defaults, overrides, shapes, a list file, a later addon replacing an id, and a broken file reported.
    const fs::path tmp = fs::temp_directory_path() / "kosmos_equipment_test";
    fs::remove_all(tmp);
    write(tmp / "a" / "equipment" / "one.json", R"({"equipment":[{"id":"gun","kind":"weapon","damage":0.5},{"id":"rifle","name":"Rifle","kind":"weapon","fire":"hitscan","rate":0,
        "shapes":[{"type":"cylinder","position":[0.1,-0.2,-0.3],"size":[0.1,0.1,0.4],"colour":[300,-5,20],"glow":true}]}]})");
    write(tmp / "a" / "equipment" / "two.json", R"({"equipment":[{"id":"bomb1","kind":"ordnance","blast_radius":70},{"id":"gun","kind":"weapon","damage":0.9}]})");
    write(tmp / "a" / "equipment" / "bad.json", "{ not json");
    write(tmp / "b" / "equipment" / "x.json", R"({"id":"bomb1","kind":"ordnance","blast_radius":99})");
    write(tmp / "c" / "equipment" / "noid.json", R"({"name":"nothing"})");
    EquipmentLibrary lib;
    std::vector<std::string> problems;
    lib.load({(tmp / "a").string(), (tmp / "b").string(), (tmp / "c").string(), (tmp / "missing").string()}, problems);
    assert(problems.size() == 1 && problems[0].find("bad.json") != std::string::npos);
    assert(lib.all().size() == 3);
    const EquipmentSpec *gun = lib.find("gun");
    assert(gun && gun->kind == EquipKind::weapon && std::fabs(gun->damage - 0.9) < 1e-12);
    const EquipmentSpec *rifle = lib.find("rifle");
    assert(rifle && rifle->name == "Rifle" && rifle->rate >= 0.05);
    assert(rifle->shapes.size() == 1 && rifle->shapes[0].type == ViewShape::cylinder && rifle->shapes[0].glow);
    assert(rifle->shapes[0].colour[0] == 255 && rifle->shapes[0].colour[1] == 0);
    assert(std::fabs(lib.find("bomb1")->blast_radius - 99.0) < 1e-12 && lib.find("bomb1")->kind == EquipKind::ordnance);
    assert(!lib.find("nothing") && !lib.find("nope"));
    fs::remove_all(tmp);

    // Shipped data: every addon loads cleanly, the standard kit exists, and every ship or building that
    // names equipment in a hardpoint or locker names something that is defined.
    const fs::path root = KOSMOS_ROOT;
    EquipmentLibrary shipped;
    problems.clear();
    shipped.load(addon_folders(root), problems);
    for (const std::string &p : problems)
        std::fprintf(stderr, "equipment problem: %s\n", p.c_str());
    assert(problems.empty());
    int standard = 0;
    for (const EquipmentSpec &e : shipped.all())
    {
        standard += e.standard ? 1 : 0;
        assert(e.mass > 0.0);
        if (e.kind == EquipKind::weapon)
            assert(e.fire == "hitscan" || e.fire == "bolt");
        if (e.kind == EquipKind::instrument)
            assert(e.sensor == "tricorder" || e.sensor == "geology" || e.sensor == "survey");
        if (e.kind == EquipKind::ordnance)
            assert(e.blast_radius > 0.0 && !e.prop.empty());
        if (e.kind == EquipKind::explosive)
            assert(e.blast_radius > 0.0);
    }
    assert(standard >= 3);

    int checked = 0;
    for (const fs::directory_entry &entry : fs::recursive_directory_iterator(root / "addons"))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json" || entry.path().parent_path().filename() == "equipment")
            continue;
        nlohmann::json j;
        try
        {
            std::ifstream in(entry.path());
            j = nlohmann::json::parse(in);
        }
        catch (...)
        {
            continue;
        }
        if (!j.is_object())
            continue;
        if (j.contains("locker"))
            for (const nlohmann::json &id : j["locker"])
            {
                ++checked;
                if (!shipped.find(id.get<std::string>()))
                    std::fprintf(stderr, "%s: unknown locker item %s\n", entry.path().c_str(), id.get<std::string>().c_str());
                assert(shipped.find(id.get<std::string>()));
            }
        if (j.contains("hardpoints"))
            for (const nlohmann::json &hp : j["hardpoints"])
            {
                ++checked;
                const EquipmentSpec *e = shipped.find(hp.value("equipment", ""));
                if (!e)
                    std::fprintf(stderr, "%s: unknown hardpoint equipment\n", entry.path().c_str());
                assert(e && e->kind == EquipKind::ordnance);
            }
    }
    assert(checked > 0);

    std::puts("equipment_test passed");
    return 0;
}
