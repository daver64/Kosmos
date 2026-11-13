#include <chrono>
#include <fstream>
// ShipEdit's document model: every shipped ship must round-trip untouched, edits must behave, save-as must produce a loadable copy.
#include "model.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace shipedit;

namespace
{
    int failures = 0;

    void check(bool ok, const std::string &what)
    {
        std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok)
            ++failures;
    }

    const AddonInfo *find_addon(const std::vector<AddonInfo> &addons, const std::string &id)
    {
        for (const AddonInfo &a : addons)
            if (a.id == id)
                return &a;
        return nullptr;
    }
} // namespace

int main()
{
    const fs::path root = KOSMOS_ROOT;
    const std::vector<AddonInfo> addons = scan_addons(root / "addons");
    check(addons.size() > 10, "addons are found");

    // Every shipped ship loads and writes back as the very same tree, key order included.
    int ships = 0, identical = 0;
    for (const AddonInfo &addon : addons)
        for (const std::string &file : addon.ships)
        {
            if (!fs::exists(addon.dir / file))
                continue;
            ++ships;
            Document doc;
            std::string error;
            if (!doc.load(addon, file, error))
            {
                check(false, addon.id + "/" + file + " loads: " + error);
                continue;
            }
            const json again = json::parse(doc.root().dump(1));
            std::ifstream in(addon.dir / file);
            json original;
            in >> original;
            if (again.dump() == original.dump())
                ++identical;
            else
                check(false, addon.id + "/" + file + " round-trips");
        }
    check(ships > 15 && identical == ships, "all " + std::to_string(ships) + " shipped ships round-trip");

    // Editing a copy of the ICBM.
    const AddonInfo *icbm = find_addon(addons, "cvel-icbm");
    check(icbm != nullptr, "the ICBM addon is present");
    if (!icbm)
        return 1;
    Document doc;
    std::string error;
    check(doc.load(*icbm, "ship.json", error), "ICBM loads");
    const int stages = doc.stage_count();
    check(stages == 3, "the ICBM has three stages");
    const auto stats = doc.stats();
    check(stats.size() == 3 && stats[0].delta_v > 500.0 && stats[0].twr > 1.0, "stage numbers are computed");
    check(stats[0].mass_at_ignition > stats[1].mass_at_ignition, "the first stage carries the one above it");
    check(!doc.validate().empty(), "an unrenamed copy is flagged");

    doc.checkpoint();
    doc.root()["name"] = "Test Stack";
    check(doc.validate().empty(), "a renamed copy has no problems: " + (doc.validate().empty() ? std::string() : doc.validate()[0]));
    check(doc.undo() && doc.name() == "CVEL Sentinel ICBM", "undo restores the name");
    check(doc.redo() && doc.name() == "Test Stack", "redo brings it back");

    const double z1 = doc.stages()[1].value("z", 0.0);
    const json engine_z = doc.stages()[1]["engines"].empty() ? json() : doc.stages()[1]["engines"][0]["position"][2];
    doc.checkpoint();
    doc.set_stage_z(1, z1 - 2.0, true);
    check(doc.stages()[1].value("z", 0.0) == z1 - 2.0, "stage z moves");
    if (engine_z.is_number())
        check(doc.stages()[1]["engines"][0]["position"][2].get<double>() == engine_z.get<double>() - 2.0, "engines move with the stage");
    doc.undo();

    doc.auto_pack();
    const double gap = doc.stages()[0].value("z", 0.0) - doc.stages()[0].value("length", 0.0) * 0.5 - (doc.stages()[1].value("z", 0.0) + doc.stages()[1].value("length", 0.0) * 0.5);
    check(std::abs(gap) < 1.0e-9, "auto-pack stacks stage 2 against stage 1");

    const int copy = doc.duplicate_stage(0);
    check(copy == 1 && doc.stage_count() == 4, "stage duplicates");
    check(doc.move_stage(1, 1) && doc.stage_count() == 4, "stage moves down the list");
    doc.remove_stage(1);
    check(doc.stage_count() == 3, "stage is removed");

    // A model from outside the source addon is queued and copied.
    const fs::path sat = root / "addons" / "cvel-saturn-v" / "models" / "sat5stg1.obj";
    doc.add_model_part(0, sat);
    const json &last_part = doc.stages()[0]["parts"].back();
    check(last_part["model"] == "models/sat5stg1.obj" && doc.pending_copies().count("models/sat5stg1.obj") == 1, "an outside model is queued");
    check(!doc.resolve("models/sat5stg1.obj").empty(), "the queued model resolves");

    // Mesh loading.
    Mesh mesh;
    check(load_mesh(sat, mesh, error) && mesh.triangles.size() > 100, "an OBJ loads: " + error);
    const fs::path glb = root / "addons" / "cvel-piper-cub" / "models" / "piper_pa18.glb";
    if (fs::exists(glb))
        check(load_mesh(glb, mesh, error) && mesh.triangles.size() > 100, "a GLB loads: " + error);

    // Save as.
    const fs::path tmp = fs::temp_directory_path() / "shipedit_test_out";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    fs::path written;
    check(!doc.save_as(tmp, "Bad Id", "x", false, written, error), "a bad id is refused");
    check(!doc.save_as(icbm->dir.parent_path(), "cvel-icbm", "x", true, written, error), "the source addon is never overwritten");
    check(doc.save_as(tmp, "test-stack", "Test Stack Addon", false, written, error), "save-as writes: " + error);
    check(fs::exists(written / "ship.json") && fs::exists(written / "addon.json") && fs::exists(written / "icbm.lua"), "the copy carries the source's files");
    check(fs::exists(written / "models" / "sat5stg1.obj"), "the queued model was copied in");
    check(!doc.save_as(tmp, "test-stack", "x", false, written, error), "an existing folder is refused");

    // Imported stages carry their models, material and animation files into a separate namespace.
    const AddonInfo *saturn = find_addon(addons, "cvel-saturn-v");
    const AddonInfo *soyuz = find_addon(addons, "cvel-soyuz");
    const AddonInfo *kitdemo = find_addon(addons, "kitdemo");
    Document rocket, source;
    rocket.make_blank("Imported rocket");
    rocket.stages().clear();
    int imported = -1;
    std::string warning;
    check(saturn && source.load(*saturn, "ship.json", error), "Saturn V stage source loads");
    check(rocket.import_stage(source, 0, -1, imported, error, warning) && imported == 0, "Saturn first stage imports: " + error);
    check(rocket.stages()[0]["z"] == 0.0 && !rocket.pending_copies().empty(), "first imported stage is centred and assets are queued");
    const auto first_files = rocket.pending_copies();
    check(rocket.undo() && rocket.stage_count() == 0 && rocket.pending_copies().empty(), "undo removes the imported stage and queued assets");
    check(rocket.redo() && rocket.stage_count() == 1 && rocket.pending_copies() == first_files, "redo restores the stage and queued assets");
    check(soyuz && source.load(*soyuz, "fg.json", error), "Soyuz stage source loads");
    Document parallel_rocket;
    parallel_rocket.make_blank("Parallel stages");
    parallel_rocket.stages().clear();
    check(parallel_rocket.import_stage(source, 0, -1, imported, error, warning) &&
              parallel_rocket.import_stage(source, 1, -1, imported, error, warning, true),
          "strap-on boosters and core can be imported in parallel");
    check(std::abs(parallel_rocket.stages()[0]["z"].get<double>() + parallel_rocket.stages()[0]["length"].get<double>() * 0.5 -
                   parallel_rocket.stages()[1]["z"].get<double>() - parallel_rocket.stages()[1]["length"].get<double>() * 0.5) < 1e-8,
          "overlapping stages have aligned tails");
    check(parallel_rocket.undo() &&
              parallel_rocket.import_stage(source, 2, -1, imported, error, warning) &&
              parallel_rocket.import_stage(source, 1, 1, imported, error, warning, true),
          "an overlapping stage can be inserted under an existing upper stage");
    check(std::abs(parallel_rocket.stages()[1]["z"].get<double>() - parallel_rocket.stages()[1]["length"].get<double>() * 0.5 -
                   parallel_rocket.stages()[2]["z"].get<double>() - parallel_rocket.stages()[2]["length"].get<double>() * 0.5) < 1e-8,
          "upper stage moves clear of the overlapping core");
    check(rocket.import_stage(source, 6, -1, imported, error, warning) && imported == 1, "animated Soyuz stage imports: " + error);
    const json &soyuz_stage = rocket.stages()[1];
    check(soyuz_stage.contains("rcs_visual") &&
              std::abs(soyuz_stage["rcs_visual"]["front_z"].get<double>() -
                       (source.stages()[6]["rcs_visual"]["front_z"].get<double>() +
                        soyuz_stage["z"].get<double>() - source.stages()[6]["z"].get<double>())) < 1e-8,
          "RCS visual moves with the imported stage");
    check(rocket.save_as(tmp / "imports", "imported-rocket", "Imported rocket", false, written, error), "imported rocket saves: " + error);
    const fs::path first_model = written / rocket.stages()[0]["parts"][0]["model"].get<std::string>();
    check(fs::exists(first_model) && fs::exists(first_model.parent_path() / "sat5stg1.mtl") &&
              fs::exists(first_model.parent_path() / "textures" / "S1C.png"),
          "imported OBJ, referenced MTL and texture are saved");
    const fs::path animation = written / soyuz_stage["parts"][0]["animations"].get<std::string>();
    check(fs::exists(animation), "imported animation is saved");
    check(kitdemo && source.load(*kitdemo, "ship.json", error) &&
              rocket.import_stage(source, 0, -1, imported, error, warning) && !rocket.stages()[2].contains("kits"),
          "part-kit stages expand when imported: " + error);
    const double old_top_z = rocket.stages()[1]["z"].get<double>();
    json expanded_kit;
    check(source.expanded_root(expanded_kit, error), "kit stage expands for placement");
    const double expanded_length = expanded_kit["stages"][0]["length"].get<double>();
    check(rocket.import_stage(source, 0, 1, imported, error, warning) && imported == 1,
          "a stage can be dropped between stages: " + error);
    check(std::abs(rocket.stages()[2]["z"].get<double>() - (old_top_z - expanded_length)) < 1e-8,
          "insertion moves the upper stages to make room");
    check(rocket.undo() && rocket.stage_count() == 3 && rocket.stages()[1]["z"] == old_top_z,
          "undo restores the stack after insertion");
    const std::string first_name = rocket.stages()[0]["name"].get<std::string>();
    const std::string last_name = rocket.stages()[2]["name"].get<std::string>();
    check(rocket.arrange_stage(2, 0, error) && rocket.stages()[0]["name"] == last_name && rocket.stages()[1]["name"] == first_name,
          "dragging a stage reorders the stack");
    check(rocket.undo() && rocket.stages()[0]["name"] == first_name && rocket.stages()[2]["name"] == last_name,
          "undo restores a rearranged stack");
    Document existing;
    check(existing.load(*soyuz, "fg.json", error) &&
              !existing.import_stage(source, 0, 1, imported, error, warning) && existing.stage_count() == 7,
          "insertion refuses to invalidate a ship's stage-dependent equipment");
    check(!existing.arrange_stage(0, 1, error), "arranging refuses a ship with stage-dependent equipment");
    Document parachute_ship = rocket;
    parachute_ship.root()["parachutes"] = json::array({{{"position", json::array({0, 0, 1})}}});
    check(!parachute_ship.arrange_stage(0, 1, error), "arranging refuses a ship with positioned parachutes");
    const fs::path fixture = tmp / "fixture" / "addons" / "asset-source";
    fs::create_directories(fixture / "models" / "materials");
    fs::create_directories(fixture / "models" / "textures");
    std::ofstream(fixture / "ship.json") << R"({"name":"Asset source","stages":[{"name":"One","z":5,"length":2,"radius":1,"parts":[{"model":"models/body.obj"}],"engines":[{"thrust":1}]}]})";
    std::ofstream(fixture / "models" / "body.obj") << "mtllib materials/custom.mtl\n";
    std::ofstream(fixture / "models" / "materials" / "custom.mtl") << "newmtl a\nmap_Kd textures/colour.png\nnorm textures/missing.png\n";
    std::ofstream(fixture / "models" / "textures" / "colour.png") << "texture";
    AddonInfo synthetic{"asset-source", "Asset source", fixture, {"ship.json"}};
    Document synthetic_source, synthetic_rocket;
    synthetic_rocket.make_blank("Asset test");
    synthetic_rocket.stages().clear();
    check(synthetic_source.load(synthetic, "ship.json", error) &&
              synthetic_rocket.import_stage(synthetic_source, 0, -1, imported, error, warning) &&
              warning.find("missing.png") != std::string::npos,
          "nonstandard MTL dependencies are found and missing textures are reported");
    check(synthetic_rocket.stages()[0]["engines"][0]["position"][2] == -5.0,
          "an engine with an implicit position moves with its stage");
    check(synthetic_rocket.save_as(tmp / "imports", "asset-test", "Asset test", false, written, error) &&
              fs::exists(written / "stage-assets-1/addons/asset-source/models/materials/custom.mtl") &&
              fs::exists(written / "stage-assets-1/addons/asset-source/models/textures/colour.png"),
          "custom material and texture paths survive export");
    synthetic_source.stages()[0]["parts"][0]["model"] = "models/missing.obj";
    const int before_failure = synthetic_rocket.stage_count();
    const auto files_before_failure = synthetic_rocket.pending_copies();
    check(!synthetic_rocket.import_stage(synthetic_source, 0, -1, imported, error, warning) &&
              synthetic_rocket.stage_count() == before_failure && synthetic_rocket.pending_copies() == files_before_failure,
          "a missing model fails without changing the stack or queued assets");
    {
        std::string gltf = R"({"images":[{"uri":"textures/a%20b.png"}]})";
        while (gltf.size() % 4)
            gltf += ' ';
        std::ofstream glb(fixture / "models" / "body.glb", std::ios::binary);
        auto u32 = [&](unsigned value) {
            for (int i = 0; i < 4; ++i)
                glb.put(static_cast<char>((value >> (i * 8)) & 255));
        };
        glb.write("glTF", 4);
        u32(2);
        u32(static_cast<unsigned>(20 + gltf.size()));
        u32(static_cast<unsigned>(gltf.size()));
        glb.write("JSON", 4);
        glb.write(gltf.data(), static_cast<std::streamsize>(gltf.size()));
    }
    std::ofstream(fixture / "models" / "textures" / "a b.png") << "texture";
    synthetic_source.stages()[0]["parts"][0]["model"] = "models/body.glb";
    check(synthetic_rocket.import_stage(synthetic_source, 0, -1, imported, error, warning) &&
              synthetic_rocket.save_as(tmp / "imports", "glb-test", "GLB test", false, written, error) &&
              fs::exists(written / "stage-assets-2/addons/asset-source/models/textures/a b.png"),
          "external GLB image URIs are copied and decoded");
    std::ofstream(fixture / "models" / "absolute.obj") << "mtllib " << (fixture / "models" / "materials" / "custom.mtl").string() << "\n";
    synthetic_source.stages()[0]["parts"][0]["model"] = "models/absolute.obj";
    const int before_absolute = synthetic_rocket.stage_count();
    const auto files_before_absolute = synthetic_rocket.pending_copies();
    check(!synthetic_rocket.import_stage(synthetic_source, 0, -1, imported, error, warning) &&
              error.find("absolute material path") != std::string::npos &&
              synthetic_rocket.stage_count() == before_absolute && synthetic_rocket.pending_copies() == files_before_absolute,
          "absolute embedded paths are rejected without changing the rocket");
    for (const AddonInfo &addon : addons)
        for (const std::string &file : addon.ships)
        {
            Document library;
            if (!library.load(addon, file, error))
                continue;
            for (int i = 0; i < library.stage_count(); ++i)
            {
                Document destination;
                destination.make_blank("Library check");
                destination.stages().clear();
                if (!destination.import_stage(library, i, -1, imported, error, warning))
                    check(false, addon.id + "/" + file + " stage " + std::to_string(i + 1) + ": " + error);
            }
        }

    const std::vector<AddonInfo> reread = scan_addons(tmp);
    check(reread.size() == 1 && reread[0].id == "test-stack" && reread[0].name == "Test Stack Addon", "the new addon is found");
    Document back;
    check(!reread.empty() && back.load(reread[0], "ship.json", error) && back.name() == "Test Stack" && back.stage_count() == 3, "the saved ship loads again");
    back.root()["name"] = "Test Stack 2";
    check(back.validate().empty(), "the saved ship validates: " + (back.validate().empty() ? std::string() : back.validate()[0]));

    // Save in place: the addon is backed up first, then the ship file is rewritten.
    const fs::path backups = fs::temp_directory_path() / "shipedit_test_backups";
    fs::remove_all(backups);
    fs::path backup;
    check(!back.save_in_place(reread[0].dir / "backups", backup, error), "a backup inside the addon is refused");
    check(back.save_in_place(backups, backup, error), "save in place: " + error);
    Document after, old;
    AddonInfo backed = reread[0];
    backed.dir = backup;
    check(after.load(reread[0], "ship.json", error) && after.name() == "Test Stack 2", "the addon now holds the edit");
    check(old.load(backed, "ship.json", error) && old.name() == "Test Stack", "the backup keeps the previous version");
    check(fs::exists(backup / "icbm.lua"), "the backup is a whole copy");
    // Another program rewriting the ship file after it was loaded blocks a plain Save.
    {
        std::ofstream touch(reread[0].dir / "ship.json", std::ios::app);
        touch << " ";
    }
    fs::last_write_time(reread[0].dir / "ship.json", fs::file_time_type::clock::now() + std::chrono::seconds(5));
    check(back.changed_on_disk() && !back.save_in_place(backups, backup, error), "a stale document is not saved over a newer file");
    check(back.save_in_place(backups, backup, error, true), "a forced save goes through: " + error);
    Document loose;
    loose.make_blank("Loose");
    check(!loose.save_in_place(backups, backup, error), "a ship with no addon cannot be saved in place");
    fs::remove_all(backups);

    Document blank;
    blank.make_blank("Blank");
    check(blank.stage_count() == 1 && blank.save_as(tmp, "blank-ship", "Blank", false, written, error), "a blank ship saves");
    fs::remove_all(tmp);

    std::printf(failures ? "%d FAILED\n" : "all shipedit tests passed\n", failures);
    return failures ? 1 : 0;
}
