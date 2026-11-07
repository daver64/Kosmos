// Tests for the launcher data layer: cmake --build build --target launcher_test && build/launcher_test
#include "data.h"

#include <cstdio>
#include <cstdlib>
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

    {
        const fs::path dir = fs::temp_directory_path() / "kosmos-launcher-split-test";
        fs::create_directories(dir);
        std::ofstream(dir / "a", std::ios::binary) << "ab";
        std::ofstream(dir / "b", std::ios::binary) << "c";
        SplitPack pack;
        pack.pack.size = 3;
        pack.pack.sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        pack.parts.push_back({"a", sha256_file(dir / "a"), 2});
        pack.parts.push_back({"b", sha256_file(dir / "b"), 1});
        std::string error;
        CHECK(reconstruct_pack(pack, dir, dir / "result", error));
        CHECK(read_text(dir / "result") == "abc");
        std::swap(pack.parts[0], pack.parts[1]);
        CHECK(!reconstruct_pack(pack, dir, dir / "result", error) && !error.empty());
        std::swap(pack.parts[0], pack.parts[1]);
        std::ofstream(dir / "b", std::ios::binary) << "d";
        CHECK(!reconstruct_pack(pack, dir, dir / "result", error) && !error.empty());
        fs::remove(dir / "b");
        CHECK(!reconstruct_pack(pack, dir, dir / "result", error) && !error.empty());
        fs::remove(dir / "a");
        fs::remove(dir / "result");
        fs::remove(dir);
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
        const std::string hash(64, 'a');
        const std::string metadata = R"({"schema":1,"version":"0.51","assets":{)"
            R"("linux":{"file":"kosmos-beta-0.51-Linux.tar.gz","size":10,"sha256":")" + hash +
            R"("},"windows":{"file":"kosmos-beta-0.51-Windows.zip","size":10,"sha256":")" + hash +
            R"("},"sdk-linux":{"file":"kosmos-sdk.tar.gz","size":10,"sha256":")" + hash +
            R"("},"sdk-windows":{"file":"kosmos-sdk.zip","size":10,"sha256":")" + hash +
            R"("},"notes":{"file":"release-notes.md","size":10,"sha256":")" + hash +
            R"("}},"packs":[{"id":"hires","name":"Hi-res","file":"kosmos-hires.tar.gz","marker":"data/textures-hires","size":7,"sha256":")" + hash +
            R"(","parts":[{"file":"kosmos-hires.tar.gz.part001","size":4,"sha256":")" + hash +
            R"("},{"file":"kosmos-hires.tar.gz.part002","size":3,"sha256":")" + hash + R"("}]}]})";
        ReleaseManifest release;
        std::string error;
        CHECK(parse_release_manifest(metadata, release, error));
        CHECK(release.version == "0.51" && release.packs.size() == 1 && release.packs[0].parts.size() == 2);
        std::string bad = metadata;
        bad.replace(bad.find("\"size\":7"), 8, "\"size\":8");
        CHECK(!parse_release_manifest(bad, release, error) && !error.empty());
        bad = metadata;
        bad.replace(bad.find("part002"), 7, "part001");
        CHECK(!parse_release_manifest(bad, release, error));
        bad = metadata;
        bad.replace(bad.find("kosmos-hires.tar.gz.part001"), 27, "../x");
        CHECK(!parse_release_manifest(bad, release, error));
        bad = metadata;
        bad.replace(bad.find("part001"), 7, "part002");
        CHECK(!parse_release_manifest(bad, release, error));
    }

    {
        const std::string list = R"([
          {"tag_name":"v0.9","draft":false,"prerelease":false,"assets":[{"name":"manifest.json","state":"uploaded"}]},
          {"tag_name":"v0.11","draft":false,"prerelease":true,"assets":[{"name":"manifest.json","state":"uploaded"}]},
          {"tag_name":"v0.12","draft":true,"assets":[]},
          {"tag_name":"unrelated","draft":false,"assets":[]}
        ])";
        GitHubRelease release;
        std::string error;
        CHECK(select_github_release(list, release, error));
        CHECK(release.version == "0.11" && release.tag == "v0.11");
        CHECK(release.manifest_url == "https://github.com/daver64/Kosmos/releases/download/v0.11/manifest.json");
        CHECK(!select_github_release(R"({"message":"API rate limit exceeded"})", release, error) && !error.empty());
        CHECK(!select_github_release(R"([{"tag_name":"v0.12","draft":false,"assets":[]}])", release, error));
        CHECK(!select_github_release(R"([{"tag_name":"v0.11","draft":false,"assets":[{"name":"manifest.json","state":"uploaded"}]},{"tag_name":"v0.12","draft":false,"assets":[]}])", release, error));
        CHECK(!select_github_release(R"([{"tag_name":"v01.1","draft":false,"assets":[]}])", release, error));
        CHECK(!select_github_release(R"([{"tag_name":"v0.12","draft":true,"assets":[]}])", release, error));
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
        CHECK(log_exit_code("Exit code 3\nCommand: kosmos") == "3");
        CHECK(log_exit_code("nothing").empty());
    }

    std::printf(failures ? "%d failure(s)\n" : "launcher_test: all passed\n", failures);
    return failures ? 1 : 0;
}
