// Tests for the launcher data layer: cmake --build build --target launcher_test && build/launcher_test
#include "data.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

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
        const fs::path f = fs::temp_directory_path() / "launcher_sha_test.bin";
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
        found = found || (s.id == "09_dragonfly_orbit" && s.category == "CVEL" && !s.ships.empty() && s.body == "Earth");
    CHECK(found);

    const auto packs = parse_packs(R"({"packs":[{"id":"x","file":"x.tar.gz","marker":"data"},{"id":"bad","file":"../x","marker":"data"}]})", root);
    CHECK(packs.size() == 1 && packs[0].installed);

    std::printf(failures ? "%d failure(s)\n" : "launcher_test: all passed\n", failures);
    return failures ? 1 : 0;
}
