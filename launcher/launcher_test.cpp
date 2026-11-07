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

int main()
{
    using namespace launcher;
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
    const auto cmd = build_arguments(p, "02_earth_orbit");
    const std::vector<std::string> want = {"--scenario", "02_earth_orbit", "--vulkan", "--fullscreen", "--max-texture", "4096", "--hide-info"};
    CHECK(cmd == want);
    CHECK(build_arguments(Profile(), "").empty());

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
    const auto scenarios = scan_scenarios(root, addons);
    CHECK(scenarios.size() > 10);
    bool found = false;
    for (const ScenarioInfo &s : scenarios)
        found = found || (s.id == "dragonfly_orbit" && s.category.rfind("CVEL", 0) == 0 && !s.ships.empty() && s.body == "Earth");
    CHECK(found);

    const auto packs = parse_packs(R"({"packs":[{"id":"x","file":"x.tar.gz","marker":"data"},{"id":"bad","file":"../x","marker":"data"}]})", root);
    CHECK(packs.size() == 1 && packs[0].installed);
    {
        const auto github = parse_packs(R"({"packs":[
          {"id":"earth","file":"earth.tar.gz","marker":"data/orbiter-defaults/earth","sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","url":"https://github.com/daver64/Kosmos/releases/latest/download/earth.tar.gz"},
          {"id":"bad-url","file":"earth.tar.gz","marker":"data/x","url":"https://example.com/earth.tar.gz"},
          {"id":"bad-marker","file":"earth.tar.gz","marker":"/tmp/x"},
          {"id":"bad-parent","file":"earth.tar.gz","marker":"data/../x"}]})", root);
        CHECK(github.size() == 1 && github[0].id == "earth");
        CHECK(github[0].url == "https://github.com/daver64/Kosmos/releases/latest/download/earth.tar.gz");
    }
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

    std::printf(failures ? "%d failure(s)\n" : "launcher_test: all passed\n", failures);
    return failures ? 1 : 0;
}
