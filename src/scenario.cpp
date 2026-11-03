#include "scenario.h"

#include "json.hpp"
#include "universe.h"

#include <algorithm>
#include <cctype>
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
            out.system = json.value("system", "");
            out.life_support = json.value("life_support", "");
            out.fullscreen = json.value("fullscreen", false);
            out.weather = json.value("weather", "");
            out.throttle = json.value("throttle", 0.0);
            out.autopilot = json.value("autopilot", false);
            out.mfd_left = json.value("mfd_left", "");
            out.mfd_right = json.value("mfd_right", "");
            out.enter_building = json.value("enter_building", "");
            if (json.contains("set"))
                for (auto it = json["set"].begin(); it != json["set"].end(); ++it)
                    out.sets.push_back(it.key() + "=" + (it.value().is_string() ? it.value().get<std::string>() : it.value().dump()));
            if (json.contains("mjd"))
                out.epoch_jd = json["mjd"].get<double>() + mjd_offset;
            else if (json.contains("date"))
            {
                const nlohmann::json &d = json["date"];
                out.date_text = d.is_string() ? d.get<std::string>() : std::to_string(d.get<double>());
                if (!parse_epoch(out.date_text, out.epoch_jd))
                    throw std::runtime_error("cannot read date \"" + out.date_text + "\" (use YYYY-MM-DD HH:MM:SS or an MJD)");
            }
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
                    s.escape_to = o.value("escape_to", "");
                    s.escape_days = o.value("escape_days", 0.0);
                    s.portal_m = o.value("portal_m", 0.0);
                    if (js.contains("state"))
                    {
                        const nlohmann::json &st = js["state"];
                        if (st.contains("r") && st.contains("v") && st.contains("q") && st["r"].size() == 3 && st["v"].size() == 3 && st["q"].size() == 4)
                        {
                            s.has_state = true;
                            for (std::size_t k = 0; k < 3; ++k)
                            {
                                s.state_position[k] = st["r"][k].get<double>();
                                s.state_velocity[k] = st["v"][k].get<double>();
                            }
                            for (std::size_t k = 0; k < 4; ++k)
                                s.state_orientation[k] = st["q"][k].get<double>();
                        }
                    }
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
                if (js.contains("dock"))
                {
                    const nlohmann::json &o = js["dock"];
                    s.dock_to = o.value("to", -1);
                    s.dock_port = o.value("port", 0);
                    s.own_port = o.value("own_port", 0);
                }
                if (js.contains("systems"))
                {
                    const nlohmann::json &o = js["systems"];
                    s.leak_cm2 = o.value("leak_cm2", 0.0);
                    s.battery = o.value("battery", -1.0);
                    s.rtg_age_years = o.value("rtg_age_years", 0.0);
                    if (o.contains("tanks"))
                        for (auto it = o["tanks"].begin(); it != o["tanks"].end(); ++it)
                            s.tanks.emplace_back(it.key(), it.value().get<double>());
                }
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
        if (!sc.system.empty() && sc.system != "Sol")
            json["system"] = sc.system;
        if (!sc.target.empty())
            json["target"] = sc.target;
        if (!sc.life_support.empty())
            json["life_support"] = sc.life_support;
        if (sc.fullscreen)
            json["fullscreen"] = true;
        if (!sc.weather.empty())
            json["weather"] = sc.weather;
        if (!sc.nav.empty())
            json["nav"] = sc.nav;
        if (sc.throttle > 0.0)
            json["throttle"] = sc.throttle;
        if (sc.autopilot)
            json["autopilot"] = true;
        if (!sc.mfd_left.empty())
            json["mfd_left"] = sc.mfd_left;
        if (!sc.mfd_right.empty())
            json["mfd_right"] = sc.mfd_right;
        if (!sc.enter_building.empty())
            json["enter_building"] = sc.enter_building;
        for (const std::string &assignment : sc.sets)
        {
            const std::size_t eq = assignment.find('=');
            if (eq != std::string::npos)
                json["set"][assignment.substr(0, eq)] = assignment.substr(eq + 1);
        }
        if (sc.epoch_jd > 0.0)
        {
            if (!sc.date_text.empty())
                json["date"] = sc.date_text;
            else
                json["mjd"] = sc.epoch_jd - mjd_offset;
        }
        json["ships"] = nlohmann::json::array();
        for (const ScenarioShip &s : sc.ships)
        {
            nlohmann::json js;
            js["ship"] = s.ship;
            if (!s.label.empty())
                js["name"] = s.label;
            js["body"] = s.body;
            if (s.dock_to >= 0)
                js["dock"] = {{"to", s.dock_to}, {"port", s.dock_port}, {"own_port", s.own_port}};
            if (!s.landed)
            {
                js["orbit"] = {{"altitude_km", s.altitude_km}, {"inclination", s.inclination}, {"phase", s.phase}};
                if (s.apoapsis_km > 0.0)
                    js["orbit"]["apoapsis_km"] = s.apoapsis_km;
                if (!s.toward.empty())
                    js["orbit"]["toward"] = s.toward;
                if (s.portal_m > 0.0)
                    js["orbit"]["portal_m"] = s.portal_m;
                if (s.has_state)
                    js["state"] = {{"r", s.state_position}, {"v", s.state_velocity}, {"q", s.state_orientation}};
                if (!s.escape_to.empty())
                    {
                    js["orbit"]["escape_to"] = s.escape_to;
                    if (s.escape_days > 0.0)
                        js["orbit"]["escape_days"] = s.escape_days;
                }
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
            if (s.leak_cm2 > 0.0 || s.battery >= 0.0 || s.rtg_age_years > 0.0 || !s.tanks.empty())
            {
                nlohmann::json o;
                if (s.leak_cm2 > 0.0)
                    o["leak_cm2"] = s.leak_cm2;
                if (s.battery >= 0.0)
                    o["battery"] = s.battery;
                if (s.rtg_age_years > 0.0)
                    o["rtg_age_years"] = s.rtg_age_years;
                for (const auto &t : s.tanks)
                    o["tanks"][t.first] = t.second;
                js["systems"] = o;
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
        std::error_code ec;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file() && it->path().extension() == ".json")
                files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const fs::path &f : files)
        {
            Scenario s;
            std::string error;
            if (load_scenario(f.string(), s, error))
            {
                std::string group;
                for (const fs::path &part : fs::relative(f.parent_path(), folder))
                {
                    std::string name = part.string();
                    if (name == "." || name.empty())
                        continue;
                    std::size_t digits = 0;
                    while (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits])))
                        ++digits;
                    if (digits > 0 && digits + 1 < name.size() && name[digits] == '_')
                        name = name.substr(digits + 1);
                    for (char &c : name)
                        if (c == '_')
                            c = ' ';
                    group += (group.empty() ? "" : "/") + name;
                }
                s.group = group;
                result.push_back(std::move(s));
            }
            else
                errors.push_back(error);
        }
        return result;
    }
} // namespace kosmos
