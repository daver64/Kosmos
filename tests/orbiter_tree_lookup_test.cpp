#undef NDEBUG
#include "planet_data.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
                          ("kosmos-tree-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path legacy = root / "Orbiter High Res Earth" / "Surf.tree";
    const fs::path canonical = root / "Earth" / "Archive" / "Surf.tree";
    const fs::path standard = root / "defaults" / "Moon" / "Archive" / "Elev.tree";
    for (const fs::path &file : {legacy, canonical, standard})
    {
        fs::create_directories(file.parent_path());
        std::ofstream(file) << "fixture";
    }
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Earth", "Surf") == canonical.string());
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Moon", "Elev") == standard.string());
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Earth", "Elev").empty());
    fs::remove(canonical);
    assert(kosmos::OrbiterTree::find_layer(root.string(), "Earth", "Surf") == legacy.string());
    fs::remove_all(root);
}
