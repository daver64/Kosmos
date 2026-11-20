#include "rocket_catalog.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>

namespace shipedit
{
    bool rocket_parts(const Document &source, std::vector<RocketPart> &parts, std::string &error)
    {
        error.clear();
        const json &root = source.root();
        if (!root.contains("rocket_builder"))
        {
            parts.clear();
            return true;
        }
        const json &builder = root["rocket_builder"];
        if (!builder.is_object() || !builder.contains("parts") || !builder["parts"].is_array())
        {
            error = "rocket_builder.parts must be an array.";
            return false;
        }
        if (root.contains("class") && !root["class"].is_string())
        {
            error = "Ship class must be a string.";
            return false;
        }
        const std::string kind = root.value("class", std::string());
        if (kind == "aircraft" || kind == "transfer")
        {
            error = "Aircraft and transfer ships cannot provide rocket stages.";
            return false;
        }
        json expanded;
        if (!source.expanded_root(expanded, error))
            return false;
        if (!expanded.contains("stages") || !expanded["stages"].is_array())
        {
            error = "Rocket part source stages must be an array.";
            return false;
        }
        const json &stages = expanded["stages"];
        const auto stage_reference = [&](const json &ref, int &index) {
            if (!ref.is_object() || !ref.contains("index") || !ref["index"].is_number_integer() ||
                !ref.contains("name") || !ref["name"].is_string())
                return false;
            const json &number = ref["index"];
            if ((!number.is_number_unsigned() && number.get<std::int64_t>() < 0) ||
                number.get<std::uint64_t>() >= stages.size() ||
                number.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                return false;
            index = number.get<int>();
            const json &stage = stages[static_cast<std::size_t>(index)];
            return stage.is_object() && stage.contains("name") && stage["name"].is_string() &&
                   stage["name"] == ref["name"];
        };
        std::vector<RocketPart> parsed;
        std::set<std::string> ids;
        for (const json &entry : builder["parts"])
        {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() ||
                !entry.contains("name") || !entry["name"].is_string() ||
                !entry.contains("role") || !entry["role"].is_string() ||
                !entry.contains("stages") || !entry["stages"].is_array() || entry["stages"].empty())
            {
                error = "Each rocket part needs an id, name, role and nonempty stages array.";
                return false;
            }
            RocketPart part;
            part.id = entry["id"].get<std::string>();
            part.name = entry["name"].get<std::string>();
            if (part.id.empty() || part.id != slug(part.id) || part.name.empty() || !ids.insert(part.id).second)
            {
                error = "Rocket part ids must be unique slugs and names must not be empty.";
                return false;
            }
            const std::string role = entry["role"].get<std::string>();
            if (role == "first") part.role = RocketPartRole::first;
            else if (role == "second") part.role = RocketPartRole::second;
            else if (role == "upper") part.role = RocketPartRole::upper;
            else if (role == "booster") part.role = RocketPartRole::booster;
            else if (role == "fairing") part.role = RocketPartRole::fairing;
            else
            {
                error = "Unknown rocket part role '" + role + "'.";
                return false;
            }
            std::set<int> seen;
            for (const json &ref : entry["stages"])
            {
                int index = -1;
                if (!stage_reference(ref, index) || !seen.insert(index).second)
                {
                    error = "Rocket part '" + part.id + "' has an invalid or repeated stage reference.";
                    return false;
                }
                part.stages.push_back(index);
            }
            if (part.role == RocketPartRole::booster || part.role == RocketPartRole::fairing)
            {
                if (!entry.contains("anchor") || !stage_reference(entry["anchor"], part.anchor) || seen.count(part.anchor))
                {
                    error = "Rocket part '" + part.id + "' needs a distinct, valid source anchor.";
                    return false;
                }
            }
            if (entry.contains("ports"))
            {
                if (!entry["ports"].is_array() || !expanded.contains("ports") || !expanded["ports"].is_array())
                {
                    error = "Rocket part '" + part.id + "' has invalid port metadata.";
                    return false;
                }
                std::set<std::string> port_names;
                for (const json &name : entry["ports"])
                {
                    if (!name.is_string() || name.get<std::string>().empty() || !port_names.insert(name.get<std::string>()).second)
                    {
                        error = "Rocket part '" + part.id + "' has an invalid or repeated port name.";
                        return false;
                    }
                    const std::string port_name = name.get<std::string>();
                    int matches = 0;
                    for (const json &port : expanded["ports"])
                        if (port.is_object() && port.contains("name") && port["name"].is_string() && port["name"] == port_name)
                        {
                            ++matches;
                            std::string geometry_error;
                            if (!valid_port_geometry(port, geometry_error))
                            {
                                error = "Rocket part '" + part.id + "' port '" + port_name + "' " + geometry_error;
                                return false;
                            }
                            bool carries = !port.contains("stage") && stages.size() == 1 && part.stages.size() == 1;
                            if (port.contains("stage") && port["stage"].is_number_integer())
                            {
                                const json &number = port["stage"];
                                if ((number.is_number_unsigned() || number.get<std::int64_t>() >= 0) &&
                                    number.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                                    carries = std::find(part.stages.begin(), part.stages.end(), number.get<int>()) != part.stages.end();
                            }
                            if (!carries)
                            {
                                error = "Rocket part '" + part.id + "' port '" + port_name + "' has no valid carrying stage.";
                                return false;
                            }
                        }
                    if (matches != 1)
                    {
                        error = "Rocket part '" + part.id + "' port '" + port_name + "' is missing or ambiguous.";
                        return false;
                    }
                    part.ports.push_back(port_name);
                }
            }
            parsed.push_back(std::move(part));
        }
        parts = std::move(parsed);
        return true;
    }

    bool valid_rocket_roles(const std::vector<RocketPartRole> &roles, std::string &error)
    {
        error.clear();
        bool first = false;
        bool above_first = false;
        bool attachment = false;
        for (RocketPartRole role : roles)
        {
            switch (role)
            {
            case RocketPartRole::first:
                if (first || above_first)
                {
                    error = "A rocket needs exactly one first stage at the bottom.";
                    return false;
                }
                first = true;
                break;
            case RocketPartRole::second:
                if (!first || above_first)
                {
                    error = "A second stage must be directly above the first stage.";
                    return false;
                }
                above_first = true;
                break;
            case RocketPartRole::upper:
                if (!first)
                {
                    error = "An upper stage needs a first stage below it.";
                    return false;
                }
                above_first = true;
                break;
            case RocketPartRole::booster:
            case RocketPartRole::fairing:
                attachment = true;
                break;
            default:
                error = "Unknown rocket part role.";
                return false;
            }
        }
        if (attachment && !first)
        {
            error = "Boosters and fairings need a first stage.";
            return false;
        }
        return true;
    }
}
