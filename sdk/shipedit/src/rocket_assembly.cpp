#include "rocket_assembly.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace shipedit
{
    RocketAssembly::RocketAssembly(const std::string &name)
    {
        state_.document.make_rocket(name);
    }

    bool RocketAssembly::ship_json(json &out, std::string &error) const
    {
        error.clear();
        if (state_.document.stage_count() == 0)
        {
            error = "Add a first stage to build the rocket.";
            return false;
        }
        json result = state_.document.root();
        json ports = json::array(), bays = json::array();
        for (std::size_t i = 0; i < state_.recipes.size(); ++i)
        {
            const State::Recipe &recipe = state_.recipes[i];
            std::vector<RocketPart> catalog;
            if (!rocket_parts(recipe.source, catalog, error))
                return false;
            const auto part = std::find_if(catalog.begin(), catalog.end(),
                                           [&](const RocketPart &p) { return p.id == recipe.part_id; });
            if (part == catalog.end())
            {
                error = "A previously selected rocket part is no longer available.";
                return false;
            }
            if (part->ports.empty())
                continue;
            json expanded;
            if (!recipe.source.expanded_root(expanded, error))
                return false;
            for (const std::string &name : part->ports)
            {
                const auto source_port = std::find_if(expanded["ports"].begin(), expanded["ports"].end(),
                    [&](const json &p) { return p.is_object() && p.contains("name") && p["name"] == name; });
                if (source_port == expanded["ports"].end())
                {
                    error = "The selected source port no longer exists.";
                    return false;
                }
                const int source_stage = source_port->value("stage", 0);
                const auto source_index = std::find(part->stages.begin(), part->stages.end(), source_stage);
                if (source_index == part->stages.end())
                {
                    error = "The selected source port has no carrying stage.";
                    return false;
                }
                const std::size_t offset = static_cast<std::size_t>(source_index - part->stages.begin());
                const int target_stage = state_.parts[i].stages[offset];
                const double dz = result["stages"][static_cast<std::size_t>(target_stage)].value("z", 0.0) -
                                  expanded["stages"][static_cast<std::size_t>(source_stage)].value("z", 0.0);
                json port = *source_port;
                if (!port.contains("position"))
                    port["position"] = json::array({0.0, 0.0, 0.0});
                if (!port["position"].is_array() || port["position"].size() != 3 ||
                    !std::all_of(port["position"].begin(), port["position"].end(), [](const json &v) {
                        return v.is_number() && std::isfinite(v.get<double>());
                    }))
                {
                    error = "Source port '" + name + "' has an invalid position.";
                    return false;
                }
                const std::string target_name = state_.parts[i].name + " / " + name + " (" + std::to_string(i + 1) + ")";
                port["name"] = target_name;
                port["stage"] = target_stage;
                port["position"][2] = port["position"][2].get<double>() + dz;
                ports.push_back(std::move(port));
                if (expanded.contains("payload_bays") && expanded["payload_bays"].is_array())
                    for (const json &source_bay : expanded["payload_bays"])
                        if (source_bay.is_object() && source_bay.contains("port") && source_bay["port"] == name)
                        {
                            json bay = source_bay;
                            bay["port"] = target_name;
                            if (!bay.contains("position"))
                                bay["position"] = json::array({0.0, 0.0, 0.0});
                            if (!bay["position"].is_array() || bay["position"].size() != 3 ||
                                !std::all_of(bay["position"].begin(), bay["position"].end(), [](const json &v) {
                                    return v.is_number() && std::isfinite(v.get<double>());
                                }))
                            {
                                error = "A catalogued payload bay has an invalid position.";
                                return false;
                            }
                            bay["position"][2] = bay["position"][2].get<double>() + dz;
                            bays.push_back(std::move(bay));
                        }
            }
        }
        if (!ports.empty())
            result["ports"] = std::move(ports);
        if (!bays.empty())
            result["payload_bays"] = std::move(bays);
        out = std::move(result);
        return true;
    }

    bool RocketAssembly::preview_json(json &out, std::string &error) const
    {
        json preview;
        if (!ship_json(preview, error))
            return false;
        for (json &stage : preview["stages"])
            if (stage.contains("parts") && stage["parts"].is_array())
                for (json &part : stage["parts"])
                    for (const char *key : {"model", "animations"})
                    {
                        if (!part.contains(key) || !part[key].is_string() || part[key].get<std::string>().empty())
                            continue;
                        const std::string relative = part[key].get<std::string>();
                        const fs::path file = state_.document.resolve(relative);
                        std::error_code ec;
                        const fs::path absolute = fs::absolute(file, ec);
                        if (file.empty() || ec || !fs::is_regular_file(absolute, ec) || ec)
                        {
                            error = "Cannot resolve preview asset: " + relative;
                            return false;
                        }
                        part[key] = absolute.string();
                    }
        out = std::move(preview);
        return true;
    }

    bool RocketAssembly::save_as(const fs::path &addons_dir, const std::string &id, const std::string &addon_name,
                                 const std::function<bool(const fs::path &, std::string &)> &validate,
                                 fs::path &written_dir, std::string &error) const
    {
        error.clear();
        written_dir.clear();
        if (id.empty() || id != slug(id))
        {
            error = "The addon id must be lower case letters, digits and dashes.";
            return false;
        }
        if (!validate)
        {
            error = "A ship validator is required before publishing an addon.";
            return false;
        }
        std::error_code ec;
        fs::create_directories(addons_dir, ec);
        if (ec)
        {
            error = "Cannot create addon directory: " + ec.message();
            return false;
        }
        fs::path staging;
        for (int n = 0; n < 1000; ++n)
        {
            const fs::path candidate = addons_dir / (".rocket-build-" + id + "-" + std::to_string(n));
            if (fs::create_directory(candidate, ec))
            {
                staging = candidate;
                break;
            }
            if (ec)
            {
                error = "Cannot create temporary addon directory: " + ec.message();
                return false;
            }
        }
        if (staging.empty())
        {
            error = "Cannot reserve a temporary addon directory.";
            return false;
        }
        const auto cleanup = [&](const fs::path &path) {
            std::error_code cleanup_error;
            fs::remove_all(path, cleanup_error);
            if (cleanup_error)
                error += "\nCannot remove " + path.string() + ": " + cleanup_error.message();
        };
        fs::path staged;
        Document copy = state_.document;
        json ship;
        if (!ship_json(ship, error))
        {
            cleanup(staging);
            return false;
        }
        copy.root() = std::move(ship);
        if (!copy.save_as(staging, id, addon_name, false, staged, error))
        {
            cleanup(staging);
            return false;
        }
        {
            std::ifstream in(staged / "addon.json");
            json manifest;
            try
            {
                in >> manifest;
            }
            catch (const json::exception &e)
            {
                error = "Cannot read generated addon manifest: " + std::string(e.what());
                cleanup(staging);
                return false;
            }
            in.close();
            manifest["rocket_builder_generated"] = true;
            std::ofstream out(staged / "addon.json", std::ios::trunc);
            out << manifest.dump(2) << "\n";
            out.close();
            if (!out)
            {
                error = "Cannot write generated addon manifest.";
                cleanup(staging);
                return false;
            }
        }
        if (!validate(staged / "ship.json", error))
        {
            if (error.empty())
                error = "Ship validation failed.";
            cleanup(staging);
            return false;
        }
        const fs::path dest = addons_dir / id;
        if (!fs::create_directory(dest, ec))
        {
            error = ec ? "Cannot create addon: " + ec.message() : "Addon already exists: " + dest.string();
            cleanup(staging);
            return false;
        }
        fs::directory_iterator it(staged, ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            const fs::path file = it->path();
            if (file.filename() == "addon.json")
                continue;
            fs::rename(file, dest / file.filename(), ec);
            if (ec)
                break;
        }
        if (!ec)
            fs::rename(staged / "addon.json", dest / "addon.json", ec);
        if (ec)
        {
            error = "Cannot publish addon: " + ec.message();
            cleanup(dest);
            cleanup(staging);
            return false;
        }
        written_dir = dest;
        cleanup(staging);
        if (!error.empty())
            return false;
        return true;
    }

    bool RocketAssembly::add(const Document &source, const std::string &part_id, int host,
                             std::string &error, std::string &warning)
    {
        error.clear();
        warning.clear();
        std::vector<RocketPart> catalog;
        if (!rocket_parts(source, catalog, error))
            return false;
        const auto found = std::find_if(catalog.begin(), catalog.end(),
                                        [&](const RocketPart &part) { return part.id == part_id; });
        if (found == catalog.end())
        {
            error = "The selected part is not in this ship's rocket catalog.";
            return false;
        }
        const RocketPart &part = *found;
        std::vector<RocketPartRole> roles;
        for (const AssemblyPart &existing : state_.parts)
            roles.push_back(existing.role);
        roles.push_back(part.role);
        if (!valid_rocket_roles(roles, error))
            return false;
        const bool booster = part.role == RocketPartRole::booster;
        const bool fairing = part.role == RocketPartRole::fairing;
        if (booster || fairing)
        {
            if (host < 0 || host >= static_cast<int>(state_.parts.size()) ||
                state_.parts[static_cast<std::size_t>(host)].stages.size() != 1)
            {
                error = "Select an existing axial stage as the attachment host.";
                return false;
            }
            const RocketPartRole host_role = state_.parts[static_cast<std::size_t>(host)].role;
            if ((booster && host_role != RocketPartRole::first) ||
                (fairing && host_role != RocketPartRole::second && host_role != RocketPartRole::upper))
            {
                error = booster ? "Boosters must attach to the first stage." : "A fairing must attach to a second or upper stage.";
                return false;
            }
        }
        if ((!fairing && part.stages.size() != 1) || (fairing && part.stages.size() < 2))
        {
            error = "This group cannot yet be assembled safely.";
            return false;
        }

        json expanded;
        if (!source.expanded_root(expanded, error))
            return false;
        const json &source_stages = expanded["stages"];
        if (fairing)
        {
            for (std::size_t i = 0; i < part.stages.size(); ++i)
            {
                const int index = part.stages[i];
                if ((i && index != part.stages[i - 1] + 1) ||
                    source_stages[static_cast<std::size_t>(index)].value("with_next", false) != (i + 1 < part.stages.size()))
                {
                    error = "This fairing's stages cannot be separated as a group.";
                    return false;
                }
            }
            const int host_stage = state_.parts[static_cast<std::size_t>(host)].stages[0];
            if (host_stage <= 0 || part.stages.front() <= 0)
            {
                error = "The fairing needs a preceding stage for its separation.";
                return false;
            }
            const bool source_coupled = source_stages[static_cast<std::size_t>(part.stages.front() - 1)].value("with_next", false);
            const bool target_coupled = state_.document.root()["stages"][static_cast<std::size_t>(host_stage - 1)].value("with_next", false);
            if (!source_coupled && target_coupled)
            {
                error = "The preceding stage already has a coupled separation; this fairing cannot be inserted safely.";
                return false;
            }
        }

        State next = state_;
        AssemblyPart added{part.name, part.role, {}};
        const double anchor_z = (booster || fairing)
                                    ? state_.document.root()["stages"][static_cast<std::size_t>(state_.parts[static_cast<std::size_t>(host)].stages[0])].value("z", 0.0)
                                    : 0.0;
        const double source_anchor_z = (booster || fairing) ? source_stages[static_cast<std::size_t>(part.anchor)].value("z", 0.0) : 0.0;
        for (int source_index : part.stages)
        {
            std::vector<double> positions;
            for (const json &stage : next.document.stages())
                positions.push_back(stage.value("z", 0.0));
            const int host_stage = (booster || fairing) ? next.parts[static_cast<std::size_t>(host)].stages[0] : -1;
            const int position = booster ? host_stage + 1 : fairing ? host_stage : -1;
            const std::optional<double> target_z = (booster || fairing)
                                                       ? std::optional<double>(anchor_z + source_stages[static_cast<std::size_t>(source_index)].value("z", 0.0) - source_anchor_z)
                                                       : std::nullopt;
            int inserted = -1;
            std::string import_warning;
            if (!next.document.import_stage(source, source_index, position, inserted, error, import_warning, booster, target_z))
                return false;
            if (booster && inserted != host_stage)
            {
                error = "The booster and its core cannot be ignited together.";
                return false;
            }
            if (!import_warning.empty())
                warning += import_warning;
            for (AssemblyPart &existing : next.parts)
                for (int &index : existing.stages)
                    if (index >= inserted)
                        ++index;
            for (int &index : added.stages)
                if (index >= inserted)
                    ++index;
            if (booster || fairing)
            {
                for (std::size_t i = 0; i < positions.size(); ++i)
                    next.document.set_stage_z(static_cast<int>(i) + (static_cast<int>(i) >= inserted ? 1 : 0), positions[i], true);
            }
            added.stages.push_back(inserted);
        }
        if (fairing && source_stages[static_cast<std::size_t>(part.stages.front() - 1)].value("with_next", false))
            next.document.stages()[static_cast<std::size_t>(added.stages.front() - 1)]["with_next"] = true;
        next.parts.push_back(std::move(added));
        next.recipes.push_back({source, part_id, (booster || fairing) ? host : -1});
        for (const char *key : {"systems", "vessels", "ports", "lights", "cameras", "surfaces", "wheels", "instruments", "weapons", "hardpoints", "payload_bays", "parachutes"})
            if (source.root().contains(key) && !source.root()[key].empty())
            {
                warning += "\nOnly catalogued ports are imported; other ship-wide equipment is not imported.";
                break;
            }
        undo_.push_back(std::move(state_));
        state_ = std::move(next);
        redo_.clear();
        return true;
    }

    bool RocketAssembly::remove(int index, std::string &error, std::string &warning)
    {
        error.clear();
        warning.clear();
        if (index < 0 || index >= static_cast<int>(state_.recipes.size()))
        {
            error = "Select a part to remove.";
            return false;
        }
        for (const State::Recipe &recipe : state_.recipes)
            if (recipe.host == index)
            {
                error = "Remove the attachments from this stage first.";
                return false;
            }
        RocketAssembly rebuilt(state_.document.name());
        for (std::size_t i = 0; i < state_.recipes.size(); ++i)
        {
            if (static_cast<int>(i) == index)
                continue;
            const State::Recipe &recipe = state_.recipes[i];
            const int host = recipe.host > index ? recipe.host - 1 : recipe.host;
            std::string added_warning;
            if (!rebuilt.add(recipe.source, recipe.part_id, host, error, added_warning))
                return false;
            warning += added_warning;
        }
        for (std::size_t i = 0, target = 0; i < state_.recipes.size(); ++i)
        {
            if (static_cast<int>(i) == index)
                continue;
            const auto &old_stages = state_.parts[i].stages;
            const auto &new_stages = rebuilt.state_.parts[target].stages;
            for (std::size_t j = 0; j < new_stages.size(); ++j)
            {
                const double old_z = state_.document.stages()[static_cast<std::size_t>(old_stages[j])].value("z", 0.0);
                const double new_z = rebuilt.state_.document.stages()[static_cast<std::size_t>(new_stages[j])].value("z", 0.0);
                if (j == 0)
                    rebuilt.state_.recipes[target].offset = old_z - new_z;
                rebuilt.state_.document.set_stage_z(new_stages[j], old_z, true);
            }
            ++target;
        }
        undo_.push_back(std::move(state_));
        state_ = std::move(rebuilt.state_);
        redo_.clear();
        return true;
    }

    bool RocketAssembly::move(int index, double dz, std::string &error)
    {
        error.clear();
        if (index < 0 || index >= static_cast<int>(state_.parts.size()))
        {
            error = "Select a part to reposition.";
            return false;
        }
        if (!std::isfinite(dz) || dz == 0.0)
        {
            error = "Enter a finite, nonzero displacement.";
            return false;
        }
        State next = state_;
        for (std::size_t i = 0; i < next.parts.size(); ++i)
        {
            if (static_cast<int>(i) != index && next.recipes[i].host != index)
                continue;
            const double offset = next.recipes[i].offset + dz;
            if (!std::isfinite(offset))
            {
                error = "The resulting displacement is too large.";
                return false;
            }
            next.recipes[i].offset = offset;
            for (int stage : next.parts[i].stages)
            {
                const double z = next.document.stages()[static_cast<std::size_t>(stage)].value("z", 0.0);
                if (!std::isfinite(z + dz))
                {
                    error = "The resulting stage position is too large.";
                    return false;
                }
                next.document.set_stage_z(stage, z + dz, true);
            }
        }
        undo_.push_back(std::move(state_));
        state_ = std::move(next);
        redo_.clear();
        return true;
    }

    bool RocketAssembly::undo()
    {
        if (undo_.empty())
            return false;
        redo_.push_back(std::move(state_));
        state_ = std::move(undo_.back());
        undo_.pop_back();
        return true;
    }

    bool RocketAssembly::redo()
    {
        if (redo_.empty())
            return false;
        undo_.push_back(std::move(state_));
        state_ = std::move(redo_.back());
        redo_.pop_back();
        return true;
    }
}
