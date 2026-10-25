#pragma once

#include "json.hpp"

#include <filesystem>
#include <string>

namespace kosmos
{
    /**
     * Rewrites the stages of a ship description that use `"kits"` into ordinary stages, so the rest of the loader never
     * sees a kit. A kit is a folder holding part.json and a mesh, in a frame where the mesh's bottom attach node is the
     * origin and the nose points along -Z. Kits stack nose-ward, bottom stage first; each adds its mass, propellant,
     * engines, gear, lights and ports at its place in the stack.
     *
     * Kit folders are searched in `<addon>/parts/`, then `<addons>/parts/` (shared by every addon), then
     * `<root>/data/parts/`; "other_addon:name" reaches into another addon.
     */
    bool expand_part_kits(nlohmann::json &ship, const std::filesystem::path &addon_dir, std::string &error);

    /** The part.json of a kit, or an empty path when it is not found. */
    std::filesystem::path find_part_kit(const std::filesystem::path &addon_dir, const std::string &name);
}
