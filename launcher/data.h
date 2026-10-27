// Data layer of the Kosmos launcher: reads scenarios, addons, settings and the hi-res pack state from disk and builds
// the command line that starts Kosmos. It uses only the standard library and nlohmann::json so it can be tested
// without a GUI and never links against Kosmos or simlib.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace launcher
{
    namespace fs = std::filesystem;

    struct ScenarioInfo
    {
        std::string id; // file stem, what --scenario takes
        std::string name, description, category, body;
        std::vector<std::string> ships;
        fs::path file, thumbnail; // thumbnail is empty when there is none
    };

    struct AddonInfo
    {
        std::string id, name, version, author, description, long_description;
        std::vector<std::string> ships;
        fs::path directory, thumbnail;
        bool enabled = true;
    };

    struct PackInfo
    {
        std::string id, name, description, file, sha256, marker; // marker: path under the install root that exists once installed
        long long size = 0;
        bool installed = false;
    };

    struct Profile
    {
        std::string backend = "auto"; // auto, gl, vulkan, d3d12
        bool fullscreen = false;
        int max_texture = 0;   // 0 = Kosmos default
        int world_texture = 0; // 0 = automatic
        std::string extra_args, last_scenario;
        // The folder that holds VERSION.txt, SHA256SUMS.txt and the archives.
        std::string server = "https://salyut.ddns.net/kosmos/download";
    };

    // The folder that holds data/, addons/ and the kosmos executable: beside the launcher, or the source tree in development.
    fs::path find_root(const fs::path &launcher_dir);
    fs::path find_kosmos(const fs::path &root);
    fs::path user_dir(); // ~/.config/kosmos or %APPDATA%\kosmos
    std::string installed_version(const fs::path &root);

    // The graphics keys of Kosmos's settings.json (gfx-style 0-2, gfx-clouds 0/1, gfx-light-radius, gfx-max-fps,
    // gfx-terrain-detail 0-3). Missing keys come back at their defaults; saving keeps every other key in the file.
    struct GraphicsSettings
    {
        int style = 0, clouds = 1, light_radius = 8000, max_fps = 60, terrain_detail = 1;
    };
    GraphicsSettings load_graphics(const fs::path &settings_file);
    bool save_graphics(const fs::path &settings_file, const GraphicsSettings &g);

    std::vector<AddonInfo> scan_addons(const fs::path &root);
    std::vector<ScenarioInfo> scan_scenarios(const fs::path &root, const std::vector<AddonInfo> &addons);
    // The built-in pack list; a packs.json from the server (see parse_packs) replaces it.
    std::vector<PackInfo> builtin_packs(const fs::path &root);
    std::vector<PackInfo> parse_packs(const std::string &json_text, const fs::path &root);

    Profile load_profile();
    bool save_profile(const Profile &profile);
    bool save_disabled_addons(const std::vector<AddonInfo> &addons);

    std::vector<std::string> build_arguments(const Profile &profile, const std::string &scenario_id);
    // Splits a command-line fragment on spaces, honouring double quotes.
    std::vector<std::string> split_arguments(const std::string &text);
    // Dotted version comparison: true when `latest` is newer than `current`.
    bool version_newer(const std::string &latest, const std::string &current);
    // Parses the body of a SHA256SUMS.txt and returns the hash for a file name, or an empty string.
    std::string find_checksum(const std::string &sums, const std::string &name);
    std::string read_text(const fs::path &file);
    // Lower-case hex SHA-256 of a file, or an empty string if it cannot be read.
    std::string sha256_file(const fs::path &file);
}
