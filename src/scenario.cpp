#include "scenario.h"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kosmos
{
    bool parse_scenario(const std::string &text, const std::string &path, Scenario &out, std::string &error)
    {
        nlohmann::json json;
        try
        {
            json = nlohmann::json::parse(text);
            out = Scenario{};
            out.path = path;
            out.id = std::filesystem::path(path).stem().string();
            out.name = json.value("name", out.id);
            out.description = json.value("description", "");
            out.active = json.value("active", 0);
            out.target = json.value("target", "");
            for (const nlohmann::json &f : json.value("nav", nlohmann::json::array()))
                out.nav.push_back(f.get<double>());
            for (const nlohmann::json &js : json.at("ships"))
            {
                ScenarioShip s;
                s.ship = js.at("ship").get<std::string>();
                s.label = js.value("name", "");
                s.body = js.value("body", "Earth");
                if (js.contains("orbit"))
                {
                    const nlohmann::json &o = js["orbit"];
                    s.altitude_km = o.value("altitude_km", 200.0);
                    s.inclination = o.value("inclination", 0.0);
                    s.phase = o.value("phase", 0.0);
                    s.apoapsis_km = o.value("apoapsis_km", 0.0);
                    s.toward = o.value("toward", "");
                }
                else if (js.contains("surface"))
                {
                    const nlohmann::json &o = js["surface"];
                    s.landed = true;
                    s.pad = o.value("pad", false);
                    s.base = o.value("base", "");
                    s.runway = o.value("runway", -1);
                    s.reverse = o.value("reverse", false);
                    s.along = o.value("along", s.along);
                    s.altitude = o.value("altitude", 0.0);
                    s.speed = o.value("speed", 0.0);
                    s.latitude = o.value("latitude", 0.0);
                    s.longitude = o.value("longitude", 0.0);
                    s.heading = o.value("heading", 90.0);
                }
                else
                    throw std::runtime_error("ship needs \"orbit\" or \"surface\"");
                out.ships.push_back(std::move(s));
            }
            if (out.ships.empty())
                throw std::runtime_error("no ships");
            out.active = std::clamp(out.active, 0, static_cast<int>(out.ships.size()) - 1);
        }
        catch (const std::exception &e)
        {
            error = path + ": " + e.what();
            return false;
        }
        return true;
    }

    bool load_scenario(const std::string &path, Scenario &out, std::string &error)
    {
        std::ifstream in(path);
        if (!in)
        {
            error = "cannot open " + path;
            return false;
        }
        std::stringstream text;
        text << in.rdbuf();
        return parse_scenario(text.str(), path, out, error);
    }

    std::string scenario_to_json(const Scenario &sc)
    {
        nlohmann::json json;
        json["name"] = sc.name;
        if (!sc.description.empty())
            json["description"] = sc.description;
        json["active"] = sc.active;
        if (!sc.target.empty())
            json["target"] = sc.target;
        if (!sc.nav.empty())
            json["nav"] = sc.nav;
        json["ships"] = nlohmann::json::array();
        for (const ScenarioShip &s : sc.ships)
        {
            nlohmann::json js;
            js["ship"] = s.ship;
            if (!s.label.empty())
                js["name"] = s.label;
            js["body"] = s.body;
            if (!s.landed)
            {
                js["orbit"] = {{"altitude_km", s.altitude_km}, {"inclination", s.inclination}, {"phase", s.phase}};
                if (s.apoapsis_km > 0.0)
                    js["orbit"]["apoapsis_km"] = s.apoapsis_km;
                if (!s.toward.empty())
                    js["orbit"]["toward"] = s.toward;
            }
            else
            {
                nlohmann::json o;
                if (!s.base.empty())
                {
                    o["base"] = s.base;
                    if (s.runway >= 0)
                    {
                        o["runway"] = s.runway;
                        o["reverse"] = s.reverse;
                        o["along"] = s.along;
                    }
                }
                else if (s.pad)
                    o["pad"] = true;
                else
                {
                    o["latitude"] = s.latitude;
                    o["longitude"] = s.longitude;
                    o["heading"] = s.heading;
                }
                if (s.altitude > 0.0)
                {
                    o["altitude"] = s.altitude;
                    o["speed"] = s.speed;
                }
                js["surface"] = o;
            }
            json["ships"].push_back(js);
        }
        return json.dump(2) + "\n";
    }

    bool save_scenario(const std::string &path, const Scenario &scenario, std::string &error)
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path);
        if (!out)
        {
            error = "cannot write " + path;
            return false;
        }
        out << scenario_to_json(scenario);
        return true;
    }

    std::vector<Scenario> load_scenarios_in(const std::string &folder, std::vector<std::string> &errors)
    {
        namespace fs = std::filesystem;
        std::vector<Scenario> result;
        if (!fs::is_directory(folder))
            return result;
        std::vector<fs::path> files;
        for (const fs::directory_entry &e : fs::directory_iterator(folder))
            if (e.is_regular_file() && e.path().extension() == ".json")
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path &f : files)
        {
            Scenario s;
            std::string error;
            if (load_scenario(f.string(), s, error))
                result.push_back(std::move(s));
            else
                errors.push_back(error);
        }
        return result;
    }
} // namespace kosmos
