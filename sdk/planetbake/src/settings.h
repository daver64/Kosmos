#pragma once

#include <string>

namespace planetbake
{
    struct Settings
    {
        std::string editor = "krita"; // program (and any options) a tile is opened with; the file name is added at the end
    };

    /** ~/.config/planetbake (%APPDATA%\planetbake on Windows). */
    std::string settings_dir();
    /** ~/.cache/planetbake (%LOCALAPPDATA%\planetbake on Windows): downloads, converted sources, tile working copies. */
    std::string cache_dir();

    Settings load_settings();
    bool save_settings(const Settings &settings, std::string &error);

    /** Starts the editor on a file and returns at once. */
    bool launch_editor(const std::string &editor, const std::string &file, std::string &error);
}
