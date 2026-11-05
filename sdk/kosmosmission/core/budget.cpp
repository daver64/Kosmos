#include "budget.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include "json.hpp"

namespace km
{
    double ShipSpec::mass() const
    {
        double m = 0;
        for (const StageSpec &s : stages)
            m += s.dry_mass + s.propellant;
        return m;
    }

    bool load_ship(const std::string &path, ShipSpec &out, std::string &err)
    {
        std::ifstream in(path);
        if (!in)
        {
            err = "cannot open " + path;
            return false;
        }
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.contains("stages") || !j["stages"].is_array() || j["stages"].empty())
        {
            err = path + " has no stages";
            return false;
        }
        out = ShipSpec{};
        out.file = path;
        out.name = j.value("name", std::filesystem::path(path).parent_path().filename().string());
        out.rcs_propellant = j.value("rcs_propellant", 0.0);
        out.rcs_isp = j.value("rcs_isp", 0.0);
        for (const nlohmann::json &s : j["stages"])
        {
            StageSpec st;
            st.name = s.value("name", "");
            st.dry_mass = s.value("dry_mass", 0.0);
            st.propellant = s.value("propellant", 0.0);
            st.length = s.value("length", 0.0);
            st.radius = s.value("radius", 0.0);
            double isp_weighted = 0, sl_weighted = 0;
            if (s.contains("engines"))
                for (const nlohmann::json &e : s["engines"])
                {
                    const double th = e.value("thrust", 0.0);
                    const double isp = e.value("isp", 0.0);
                    st.thrust += th;
                    isp_weighted += th * isp;
                    sl_weighted += th * e.value("isp_sl", isp);
                }
            if (st.thrust > 0)
            {
                st.isp = isp_weighted / st.thrust;
                st.isp_sl = sl_weighted / st.thrust;
            }
            out.stages.push_back(st);
        }
        for (const nlohmann::json &p : j.value("ports", nlohmann::json::array()))
            out.ports.push_back(p.value("name", "Port"));
        double stack_length = 0, max_radius = 0;
        for (const StageSpec &st : out.stages)
        {
            stack_length += st.length;
            max_radius = std::max(max_radius, st.radius);
        }
        if (j.contains("payload_size"))
        {
            const nlohmann::json &p = j["payload_size"];
            if (p.is_array() && p.size() == 3)
            {
                out.size = {true, false, 0, p[0].get<double>(), p[1].get<double>(), p[2].get<double>()};
            }
            else if (p.is_object())
                out.size = {true, true, p.value("diameter", 0.0), 0, 0, p.value("length", 0.0)};
        }
        if (!out.size.known && max_radius > 0 && stack_length > 0)
            out.size = {true, true, 2 * max_radius, 0, 0, stack_length};
        for (const nlohmann::json &b : j.value("payload_bays", nlohmann::json::array()))
        {
            PayloadBay bay;
            bay.name = b.value("name", "Payload bay");
            bay.round = b.value("shape", "cylinder") != "box";
            bay.length = b.value("length", 0.0);
            bay.max_mass = b.value("max_mass", 0.0);
            bay.port = b.value("port", "Payload");
            if (bay.round)
                bay.radius = b.value("radius", b.value("diameter", 0.0) / 2);
            else if (b.contains("size") && b["size"].is_array() && b["size"].size() == 3)
            {
                bay.width = b["size"][0].get<double>();
                bay.height = b["size"][1].get<double>();
                bay.length = b["size"][2].get<double>();
            }
            if (b.contains("position") && b["position"].is_array() && b["position"].size() == 3)
                for (int i = 0; i < 3; ++i)
                    bay.position[i] = b["position"][i].get<double>();
            const bool valid = bay.length > 0 && (bay.round ? bay.radius > 0 : bay.width > 0 && bay.height > 0);
            if (valid)
                out.bays.push_back(bay);
        }
        return true;
    }

    std::vector<ShipSpec> list_ships(const std::string &root)
    {
        std::vector<ShipSpec> ships;
        std::error_code ec;
        for (const auto &dir : std::filesystem::directory_iterator(std::filesystem::path(root) / "addons", ec))
        {
            // An addon lists its ships in addon.json ("ships": ["a.json", ...]); one without a list has a ship.json.
            std::vector<std::string> files;
            std::ifstream in(dir.path() / "addon.json");
            if (in)
            {
                const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
                if (!j.is_discarded() && j.contains("ships") && j["ships"].is_array())
                    for (const nlohmann::json &f : j["ships"])
                        if (f.is_string())
                            files.push_back(f.get<std::string>());
            }
            if (std::find(files.begin(), files.end(), "ship.json") == files.end())
                files.push_back("ship.json");
            for (const std::string &file : files)
            {
                const std::filesystem::path f = dir.path() / file;
                ShipSpec s;
                std::string err;
                if (std::filesystem::exists(f, ec) && load_ship(f.string(), s, err))
                    ships.push_back(s);
            }
        }
        std::sort(ships.begin(), ships.end(), [](const ShipSpec &a, const ShipSpec &b) { return a.name < b.name; });
        return ships;
    }

    Budget budget(const ShipSpec &ship, double payload, bool use_sea_level)
    {
        Budget b;
        b.payload = payload;
        const size_t n = ship.stages.size();
        std::vector<double> above(n + 1, payload); // mass of everything still attached above stage i, with the payload
        for (size_t i = n; i-- > 0;)
            above[i] = above[i + 1] + ship.stages[i].dry_mass + ship.stages[i].propellant;
        for (size_t i = 0; i < n; ++i)
        {
            const StageSpec &s = ship.stages[i];
            StageDv d;
            d.name = s.name;
            d.m0 = above[i];
            d.m1 = above[i] - s.propellant;
            const double isp = use_sea_level && s.isp_sl > 0 ? s.isp_sl : s.isp;
            if (isp > 0 && d.m1 > 0 && d.m0 > d.m1)
                d.dv = isp * G0 * std::log(d.m0 / d.m1);
            b.total += d.dv;
            b.stages.push_back(d);
        }
        return b;
    }

    Ascent ascent_estimate(const Body &body, double altitude, double latitude_deg)
    {
        Ascent a;
        const double r = body.radius + altitude;
        a.v_orbit = std::sqrt(body.gm / r);
        a.v_rotation = body.equator_speed() * std::cos(latitude_deg * PI / 180.0);
        a.loss = (body.atm_density > 0.005 ? 190.0 : 100.0) * body.surface_gravity();
        a.dv = std::max(0.0, a.v_orbit - a.v_rotation + a.loss);
        a.note = "Rule of thumb: orbital speed - ground speed + gravity/drag loss.";
        return a;
    }

    bool AscentTable::load(const std::string &path)
    {
        std::ifstream in(path);
        if (!in)
            return false;
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.contains("ascents"))
            return false;
        for (const nlohmann::json &e : j["ascents"])
            set(e.value("ship", ""), e.value("body", ""), e.value("dv", 0.0), e.value("note", ""));
        return true;
    }

    void AscentTable::set(const std::string &ship, const std::string &body, double dv, const std::string &note) { entries_[ship + "|" + body] = {dv, note}; }

    bool AscentTable::find(const std::string &ship, const std::string &body, double &dv, std::string &note) const
    {
        const auto it = entries_.find(ship + "|" + body);
        if (it == entries_.end())
            return false;
        dv = it->second.dv;
        note = it->second.note;
        return true;
    }

    Ascent ascent(const Body &body, double altitude, double latitude_deg, const std::string &ship, const AscentTable &table)
    {
        Ascent a = ascent_estimate(body, altitude, latitude_deg);
        double dv;
        std::string note;
        if (table.find(ship, body.name, dv, note))
        {
            a.dv = dv;
            a.measured = true;
            a.note = note.empty() ? "Measured value." : note;
        }
        return a;
    }
} // namespace km
