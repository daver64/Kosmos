#include "body_catalog.h"

#include <json.hpp>

#include <cmath>
#include <unordered_set>

namespace kmap
{
    bool read_body_catalog(std::istream &input, std::vector<BodyInfo> &bodies, std::string &error)
    {
        const auto json = nlohmann::json::parse(input, nullptr, false);
        if (!json.is_object() || !json.contains("bodies") || !json["bodies"].is_array())
        {
            error = "invalid system body list";
            return false;
        }
        std::vector<BodyInfo> parsed;
        std::unordered_set<std::string> names;
        for (const auto &item : json["bodies"])
        {
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string() ||
                !item.contains("radius") || !item["radius"].is_number() ||
                (item.contains("parent") && !item["parent"].is_string()))
            {
                error = "invalid body in system list";
                return false;
            }
            BodyInfo body;
            body.name = item["name"].get<std::string>();
            body.radius = item["radius"].get<double>();
            if (item.contains("parent"))
                body.parent = item["parent"].get<std::string>();
            if (body.name.empty() || !std::isfinite(body.radius) || body.radius <= 0.0 || !names.insert(body.name).second)
            {
                error = "invalid or duplicate body name or radius";
                return false;
            }
            parsed.push_back(std::move(body));
        }
        if (parsed.empty())
        {
            error = "system body list is empty";
            return false;
        }
        bodies = std::move(parsed);
        error.clear();
        return true;
    }
}
