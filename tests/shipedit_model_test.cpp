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
