#pragma once

#include "rocket_catalog.h"

#include <functional>

namespace shipedit
{
    struct AssemblyPart
    {
        std::string name;
        RocketPartRole role;
        std::vector<int> stages;
    };

    class RocketAssembly
    {
    public:
        explicit RocketAssembly(const std::string &name);

        const Document &document() const { return state_.document; }
        const std::vector<AssemblyPart> &parts() const { return state_.parts; }
        bool ship_json(json &out, std::string &error) const;
        bool preview_json(json &out, std::string &error) const;
        bool save_as(const fs::path &addons_dir, const std::string &id, const std::string &addon_name,
                     const std::function<bool(const fs::path &, std::string &)> &validate,
                     fs::path &written_dir, std::string &error) const;

        // Attachments use an existing part index as their host. Axial stages ignore host.
        bool add(const Document &source, const std::string &part_id, int host,
                 std::string &error, std::string &warning);
        bool remove(int index, std::string &error, std::string &warning);
        bool move(int index, double dz, std::string &error);
        bool undo();
        bool redo();

    private:
        struct State
        {
            struct Recipe
            {
                Document source;
                std::string part_id;
                int host = -1;
                double offset = 0.0;
            };
            Document document;
            std::vector<AssemblyPart> parts;
            std::vector<Recipe> recipes;
        };
        State state_;
        std::vector<State> undo_, redo_;
    };
}
