// Tests for the launcher data layer: cmake --build build --target launcher_test && build/launcher_test
#include "data.h"

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>

static int failures = 0;
#define CHECK(x)                                                         \
    do                                                                   \
    {                                                                    \
        if (!(x))                                                        \
        {                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);     \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static void check_github_catalogue(const launcher::fs::path &root)
{
    const auto github = launcher::parse_packs(R"({"packs":[
      {"id":"earth","file":"earth.tar.gz","marker":"data/orbiter-defaults/earth","sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","url":"https://github.com/daver64/Kosmos/releases/latest/download/earth.tar.gz"},
      {"id":"pinned","file":"earth.tar.gz","marker":"data/earth","sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","url":"https://github.com/daver64/Kosmos/releases/download/v0.51/earth.tar.gz"},
      {"id":"bad-version","file":"earth.tar.gz","marker":"data/earth","sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","url":"https://github.com/daver64/Kosmos/releases/download/v0.51/../earth.tar.gz"},
      {"id":"bad-repo","file":"earth.tar.gz","marker":"data/earth","sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","url":"https://github.com/other/repo/releases/download/v0.51/earth.tar.gz"},
      {"id":"bad-url","file":"earth.tar.gz","marker":"data/x","url":"https://example.com/earth.tar.gz"},
      {"id":"bad-marker","file":"earth.tar.gz","marker":"/tmp/x"},
      {"id":"bad-parent","file":"earth.tar.gz","marker":"data/../x"}]})", root);
    if (github.size() != 2)
    {
        std::printf("Unexpected accepted GitHub packs:");
        for (const auto &pack : github)
            std::printf(" %s", pack.id.c_str());
        std::printf("\n");
    }
    CHECK(github.size() == 2);
    if (github.size() == 2)
    {
        CHECK(github[0].id == "earth");
        CHECK(github[1].id == "pinned");
        CHECK(github[0].url == "https://github.com/daver64/Kosmos/releases/latest/download/earth.tar.gz");
        CHECK(github[1].url == "https://github.com/daver64/Kosmos/releases/download/v0.51/earth.tar.gz");
    }
}

int main(int argc, char **argv)
{
    using namespace launcher;
    if (argc == 2 && std::string(argv[1]) == "--catalog-only")
    {
        check_github_catalogue(fs::temp_directory_path());
        return failures ? 1 : 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--ground-relief-profile-test")
    {
        std::error_code ec;
        if (!fs::equivalent(user_dir(), fs::path(argv[2]) / "kosmos", ec) ||
            !fs::is_regular_file(user_dir() / "ground-relief-test-marker"))
        {
            std::fprintf(stderr, "Ground relief profile test requires an isolated, marked config directory\n");
            return 1;
        }
        Profile profile = load_profile();
        CHECK(!profile.orbital_relief_ground);
        CHECK(profile.extra_args == "--no-orbital-relief-ground");
        profile.orbital_relief_ground = true;
        CHECK(save_profile(profile));
        profile = load_profile();
        CHECK(profile.orbital_relief_ground);
        CHECK(profile.extra_args == "--no-orbital-relief-ground");
        CHECK(build_arguments(profile, "").back() == "--orbital-relief-ground");
        profile.orbital_relief_ground = false;
        CHECK(save_profile(profile));
        CHECK(!load_profile().orbital_relief_ground);
        return failures ? 1 : 0;
    }
    {
        const fs::path dir = fs::temp_directory_path() /
            ("kosmos-pack-queue-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        const fs::path file = dir / "queue.json";
        std::vector<std::string> ids;
        std::string error;
        CHECK(load_pack_queue(file, ids, error) && ids.empty());
        CHECK(save_pack_queue(file, {"first", "second"}, error));
        CHECK(load_pack_queue(file, ids, error) && ids == std::vector<std::string>({"first", "second"}));
        CHECK(save_pack_queue(file, {"second"}, error));
        CHECK(load_pack_queue(file, ids, error) && ids == std::vector<std::string>({"second"}));
        std::ofstream(file) << R"(["same","same"])";
        CHECK(!load_pack_queue(file, ids, error) && ids.empty() && !error.empty());
        std::ofstream(file) << "{ invalid";
        CHECK(!load_pack_queue(file, ids, error) && !error.empty());
        CHECK(!save_pack_queue(dir / "missing" / "queue.json", {"x"}, error) && !error.empty());
        fs::remove_all(dir);
    }
    CHECK(version_newer("0.19", "0.18"));
    CHECK(version_newer("0.100", "0.99"));
    CHECK(!version_newer("0.18", "0.18"));
    CHECK(!version_newer("0.18", "0.19"));
    CHECK(version_newer("1.0", "0.19"));

    const std::string sums = "aaa  kosmos-beta-Linux.tar.gz\r\nbbb  kosmos-beta-Windows.zip\n";
    CHECK(find_checksum(sums, "kosmos-beta-Windows.zip") == "bbb");
    CHECK(find_checksum(sums, "kosmos-beta-Linux.tar.gz") == "aaa");
    CHECK(find_checksum(sums, "nope").empty());

    const auto args = split_arguments("--set \"/a b=1\" --hide-info");
    CHECK(args.size() == 3 && args[1] == "/a b=1");

    Profile p;
    p.backend = "vulkan";
    p.fullscreen = true;
    p.max_texture = 4096;
    p.extra_args = "--hide-info";
    p.legacy_orbiter_textures = "/opt/Orbiter/Textures";
    const auto cmd = build_arguments(p, "02_earth_orbit");
    const std::vector<std::string> want = {"--scenario", "02_earth_orbit", "--vulkan", "--fullscreen", "--max-texture", "4096",
                                           "--hide-info", "--no-orbital-relief-ground"};
    CHECK(cmd == want);
    CHECK(build_arguments(Profile(), "") == std::vector<std::string>({"--no-orbital-relief-ground"}));
    p.orbital_relief_ground = true;
    p.extra_args = "--no-orbital-relief-ground";
    const auto enabled = build_arguments(p, "");
    CHECK(enabled.size() >= 2 && enabled[enabled.size() - 2] == "--no-orbital-relief-ground" &&
          enabled.back() == "--orbital-relief-ground");
    p.orbital_relief_ground = false;
    p.extra_args = "--orbital-relief-ground";
    const auto disabled = build_arguments(p, "");
    CHECK(disabled.size() >= 2 && disabled[disabled.size() - 2] == "--orbital-relief-ground" &&
          disabled.back() == "--no-orbital-relief-ground");

    {
        const fs::path f = launcher::fs::temp_directory_path() / "launcher_sha_test.bin";
        std::ofstream(f, std::ios::binary) << "abc";
        CHECK(sha256_file(f) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        { std::ofstream o(f, std::ios::binary); o << std::string(1000, 'a'); }
        CHECK(sha256_file(f) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
        fs::remove(f);
    }

    const fs::path root = KOSMOS_SOURCE_DIR;
    const auto addons = scan_addons(root);
    CHECK(addons.size() > 5);
    for (const auto &expected : std::vector<std::pair<std::string, std::string>>{
             {"ares", "CVEL"}, {"cvel-deltaglider", "CVEL"}, {"probe", "CVEL"}, {"salyut", "CVEL"},
             {"kosmoscad", "KosmosLink Apps"}, {"kosmosmap", "KosmosLink Apps"}, {"kosmosmission", "KosmosLink Apps"},
             {"shipedit", "Tools"}, {"instruments", "Instruments"}, {"gale-base", "Buildings"},
             {"nasa-voyager", "Ships"}, {"field-kit", "Other"}})
    {
        bool matched = false;
        for (const AddonInfo &a : addons)
            matched = matched || (a.id == expected.first && a.category == expected.second);
        CHECK(matched);
    }
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path dir = fs::temp_directory_path() / ("launcher-addon-category-" + std::to_string(stamp));
        fs::create_directories(dir / "addons" / "custom");
        std::ofstream(dir / "addons" / "custom" / "addon.json")
            << R"({"id":"custom-category-test","name":"Custom","category":"My collection","ships":["ship.json"]})";
        const auto custom = scan_addons(dir);
        CHECK(custom.size() == 1 && custom[0].category == "My collection");
        fs::remove_all(dir);
    }
    const auto scenarios = scan_scenarios(root, addons);
    CHECK(scenarios.size() > 10);
    bool found = false;
    for (const ScenarioInfo &s : scenarios)
        found = found || (s.id == "dragonfly_orbit" && s.category.rfind("CVEL", 0) == 0 && !s.ships.empty() && s.body == "Earth");
    CHECK(found);
    for (const auto &expected : std::vector<std::pair<std::string, std::string>>{
             {"apollo11_saturn_v_t_minus_5", "apollo11_saturn_v_t_minus_5-1794113694264053257-0.png"},
             {"saturn_v_23l_kennedy", "saturn_v523L.png"},
             {"saturn_v_mlv1_kennedy", "saturn_vmlv1.png"},
             {"saturn_v_mlv1_nerva_kennedy", "saturn_vmlv1_nerva.png"},
             {"saturn_v_mlv2_kennedy", "saturn_v_mlv2.png"},
             {"saturn_v_mlv3_kennedy", "saturn_v_mlv3.png"}})
    {
        bool thumbnail_found = false;
        const fs::path thumbnail = root / "scenarios/CVEL/CVEL-SaturnV/thumbs" / expected.second;
        for (const ScenarioInfo &s : scenarios)
            thumbnail_found = thumbnail_found || (s.id == expected.first && s.thumbnail == thumbnail);
        CHECK(thumbnail_found);
        CHECK(fs::is_regular_file(thumbnail));
    }

    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path dir = fs::temp_directory_path() / ("launcher-scenario-watch-" + std::to_string(stamp));
        const fs::path folder = dir / "scenarios" / "Nested";
        const fs::path addon_folder = dir / "addon" / "scenarios";
        fs::create_directories(folder / "thumbs");
        fs::create_directories(addon_folder);
        const fs::path file = folder / "launcher_watch_unique.json";
        std::ofstream(file) << R"({"name":"Before","ships":[]})";
        AddonInfo addon;
        addon.name = "Test addon";
        addon.directory = dir / "addon";
        const auto first = scan_scenario_changes(dir, {addon}, 7);
        CHECK(first.generation == 7);
        std::ofstream(file) << R"({"name":"After","ships":[]})";
        const auto renamed = scan_scenario_changes(dir, {addon}, 7);
        CHECK(!same_scenario_list(first.scenarios, renamed.scenarios));
        const fs::path image = folder / "thumbs" / "launcher_watch_unique.png";
        std::ofstream(image) << "first";
        const auto added_image = scan_scenario_changes(dir, {addon}, 7);
        CHECK(!same_scenario_list(renamed.scenarios, added_image.scenarios));
        fs::last_write_time(image, fs::last_write_time(image) + std::chrono::seconds(2));
        const auto changed_image = scan_scenario_changes(dir, {addon}, 7);
        CHECK(same_scenario_list(added_image.scenarios, changed_image.scenarios));
        CHECK(added_image.thumbnail_times != changed_image.thumbnail_times);
        const fs::path addon_scenario = addon_folder / "launcher_addon_watch_unique.json";
        std::ofstream(addon_scenario) << R"({"name":"Addon","ships":[]})";
        const auto added = scan_scenario_changes(dir, {addon}, 7);
        CHECK(!same_scenario_list(changed_image.scenarios, added.scenarios));
        addon.enabled = false;
        const auto disabled = scan_scenario_changes(dir, {addon}, 7);
        CHECK(!same_scenario_list(added.scenarios, disabled.scenarios));
        addon.enabled = true;
        fs::remove(image);
        const auto removed_image = scan_scenario_changes(dir, {addon}, 7);
        CHECK(!same_scenario_list(added.scenarios, removed_image.scenarios));
        fs::remove(file);
        const auto removed = scan_scenario_changes(dir, {addon}, 7);
        CHECK(!same_scenario_list(removed_image.scenarios, removed.scenarios));
        fs::remove_all(dir);
    }

    const auto packs = parse_packs(R"({"packs":[{"id":"x","file":"x.tar.gz","marker":"data"},{"id":"bad","file":"../x","marker":"data"}]})", root);
    CHECK(packs.size() == 1 && packs[0].installed);
    check_github_catalogue(root);
    {
        const auto layers = orbiter_hires_packs(root);
        CHECK(layers.size() == 15);
        for (const PackInfo &layer : layers)
        {
            CHECK((layer.tree || layer.minor_bodies) && layer.sha256.size() == 64 && layer.size > 0);
            CHECK(layer.marker.rfind("import/orbiter/", 0) == 0);
            CHECK(layer.url.rfind("https://cdn.orbithangar.com/orbiter-textures/", 0) == 0);
            CHECK(layer.remove == layer.marker);
        }
    }
    {
        const fs::path dir = fs::temp_directory_path() /
                             ("kosmos-tree-install-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        const fs::path part = dir / "Surf.tree.part", target = dir / "Surf.tree";
        std::error_code ec;
        const std::string abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        std::string error;
        std::ofstream(part) << "bad";
        CHECK(!install_downloaded_tree(part, target, abc, error) && !fs::exists(part) && !error.empty());
        std::ofstream(part) << "abc";
        CHECK(install_downloaded_tree(part, target, abc, error) && fs::exists(target) && !fs::exists(part));
        std::ofstream(part) << "abc";
        CHECK(!install_downloaded_tree(part, target, abc, error) && fs::exists(part));
        CHECK(sha256_file(target) == abc);
        fs::remove(part, ec);
        fs::remove(target, ec);
        fs::remove(dir, ec);
    }
    {
        const launcher::fs::path tmp = launcher::fs::temp_directory_path() / "kosmos_gfx_test" / "settings.json";
        launcher::fs::remove_all(tmp.parent_path());
        CHECK(load_graphics(tmp).max_fps == 60);
        launcher::fs::create_directories(tmp.parent_path());
        std::ofstream(tmp) << R"({"volume":33,"gfx-style":9})";
        CHECK(load_graphics(tmp).style == 2);
        GraphicsSettings g;
        g.style = 1;
        g.clouds = 0;
        g.max_fps = 30;
        g.earth_relief = 1;
        g.weather = 0;
        g.weather_live = 1;
        g.weather_preset = "custom";
        g.weather_metar = "KOSM 011200Z 27015KT 9999 FEW040 15/10 Q1015";
        CHECK(save_graphics(tmp, g));
        const GraphicsSettings back = load_graphics(tmp);
        CHECK(back.style == 1 && back.clouds == 0 && back.max_fps == 30 && back.earth_relief == 1 && back.weather == 0 && back.weather_live == 1 && back.weather_rain == 1 && back.weather_preset == "custom" && back.weather_metar.rfind("KOSM", 0) == 0);
        std::ifstream in(tmp);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"volume\": 33") != std::string::npos);
        in.close();
        launcher::fs::remove_all(tmp.parent_path());
    }

    {
        CHECK(scrub_paths("open /home/alice/x and C:\\Users\\alice\\y", "/home/alice") == "open ~/x and C:\\Users\\user\\y");
        CHECK(scrub_paths("C:\\Users\\bob\\kosmos C:/Users/bob/z", "C:\\Users\\bob") == "~\\kosmos ~/z");
        CHECK(scrub_paths("keep", "") == "keep");
        CHECK(log_tail("short", 100) == "short");
        const std::string cut = log_tail(std::string(50, 'a') + "END", 10);
        CHECK(cut.rfind("[earlier output cut]\n", 0) == 0 && cut.size() == 21 + 10 && cut.substr(cut.size() - 3) == "END");
        CHECK(report_url("https://x.net/kosmos/download") == "https://x.net/kosmos/report/submit.php");
        CHECK(report_url("https://x.net/kosmos/download/") == "https://x.net/kosmos/report/submit.php");
        CHECK(report_page_url("https://x.net/kosmos/download") == "https://x.net/kosmos/report/");
        ReportContent r;
        r.log = "bad \xff byte";
        r.note = "n";
        const std::string body = report_payload(r);
        CHECK(body.find("\"note\":\"n\"") != std::string::npos && body.find("\"log\"") != std::string::npos);
        CHECK(log_exit_code("Exit code 3\nCommand: kosmos") == "3");
        CHECK(log_exit_code("nothing").empty());
    }

    {
        namespace fs = launcher::fs;
        const fs::path base = fs::temp_directory_path() / "kosmos-update-test";
        fs::remove_all(base);
        const auto put = [](const fs::path &p, const std::string &text)
        {
            fs::create_directories(p.parent_path());
            std::ofstream(p) << text;
        };
        const auto get = [](const fs::path &p)
        {
            std::ifstream in(p);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        put(base / "root/VERSION.txt", "0.1");
        put(base / "root/kosmos", "old");
        put(base / "root/addons/mine/addon.json", "user");
        put(base / "root/data/orbiter-defaults/keep.txt", "scenery");
        put(base / "stage/kosmos-beta-0.2-Linux/VERSION.txt", "0.2");
        put(base / "stage/kosmos-beta-0.2-Linux/kosmos", "new");
        put(base / "stage/kosmos-beta-0.2-Linux/addons/ship/addon.json", "ship");
        std::string error;
        CHECK(launcher::apply_update(base / "stage", base / "root", error) && error.empty());
        CHECK(get(base / "root/kosmos") == "new" && get(base / "root/VERSION.txt") == "0.2");
        CHECK(get(base / "root/addons/ship/addon.json") == "ship" && get(base / "root/addons/mine/addon.json") == "user");
        CHECK(get(base / "root/data/orbiter-defaults/keep.txt") == "scenery");
        CHECK(launcher::installed_version(base / "root") == "0.2");
        put(base / "root/kosmos.old", "x");
        put(base / "root/.update-leftovers.txt", "kosmos.old\n");
        launcher::clean_update_leftovers(base / "root");
        CHECK(!fs::exists(base / "root/kosmos.old") && !fs::exists(base / "root/.update-leftovers.txt"));
        fs::create_directories(base / "empty");
        CHECK(!launcher::apply_update(base / "empty", base / "root", error) && !error.empty());
        put(base / "root/CMakeLists.txt", "");
        CHECK(launcher::is_source_checkout(base / "root") && !launcher::apply_update(base / "stage", base / "root", error));
        fs::remove_all(base);
    }

    std::printf(failures ? "%d failure(s)\n" : "launcher_test: all passed\n", failures);
    return failures ? 1 : 0;
}
