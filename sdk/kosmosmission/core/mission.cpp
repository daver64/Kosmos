#include "mission.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include "json.hpp"
#include <sstream>

namespace km
{
    namespace
    {
        const char *const kNames[] = {"ascent", "transfer", "arrival", "plane", "custom"};
    }

    const char *leg_type_name(LegType t) { return kNames[static_cast<int>(t)]; }

    LegType leg_type_from_name(const std::string &s)
    {
        for (int i = 0; i < 5; ++i)
            if (s == kNames[i])
                return static_cast<LegType>(i);
        return LegType::Custom;
    }

    double Mission::total_dv() const
    {
        double t = 0;
        for (const Leg &l : legs)
            t += l.dv;
        return t;
    }

    std::vector<double> Mission::remaining_after(double available) const
    {
        std::vector<double> r;
        double left = available * (1.0 - margin);
        for (const Leg &l : legs)
        {
            left -= l.dv;
            r.push_back(left);
        }
        return r;
    }

    std::vector<LedgerRow> ledger(const Mission &m, double available)
    {
        std::vector<LedgerRow> rows;
        const std::vector<double> rem = m.remaining_after(available);
        double cum = 0;
        for (size_t i = 0; i < m.legs.size(); ++i)
        {
            cum += m.legs[i].dv;
            rows.push_back({m.legs[i].name, m.legs[i].dv, cum, rem[i], rem[i] < 0});
        }
        return rows;
    }

    std::string to_scenario_json(const Mission &m)
    {
        nlohmann::json j;
        j["name"] = m.name;
        j["description"] = m.description.empty() ? "Mission planned in KosmosMission." : m.description;
        j["active"] = 0;
        nlohmann::json ship;
        ship["ship"] = m.ship;
        ship["body"] = m.start.body;
        if (m.start.in_orbit())
        {
            ship["orbit"] = {{"altitude_km", m.start.altitude / 1000.0}, {"inclination", m.start.inclination}, {"phase", 0}};
        }
        else
            ship["surface"] = {{"base", m.start.base}};
        j["ships"] = nlohmann::json::array({ship});

        nlohmann::json mi;
        mi["version"] = 1;
        mi["ship_file"] = m.ship_file;
        mi["payload"] = m.payload;
        nlohmann::json items = nlohmann::json::array();
        for (const PayloadItem &p : m.payloads)
        {
            nlohmann::json o = {{"label", p.label}, {"ship_file", p.ship_file}, {"count", p.count}, {"mass", p.mass}, {"bay", p.bay}};
            if (p.size.known)
                o["size"] = p.size.round ? nlohmann::json{{"diameter", p.size.diameter}, {"length", p.size.length}}
                                         : nlohmann::json{{"width", p.size.width}, {"height", p.size.height}, {"length", p.size.length}};
            items.push_back(o);
        }
        mi["payloads"] = items;
        mi["margin"] = m.margin;
        mi["start"] = {{"body", m.start.body}, {"base", m.start.base}, {"altitude", m.start.altitude}, {"inclination", m.start.inclination},
                       {"latitude", m.start.latitude}};
        nlohmann::json legs = nlohmann::json::array();
        for (const Leg &l : m.legs)
            legs.push_back({{"type", leg_type_name(l.type)}, {"name", l.name}, {"body", l.body}, {"to_body", l.to_body}, {"dv", l.dv},
                            {"duration", l.duration}, {"depart_days", l.depart_days}, {"altitude", l.altitude}, {"note", l.note}});
        mi["legs"] = legs;
        j["mission"] = mi;
        return j.dump(1) + "\n";
    }

    bool from_scenario_json(const std::string &text, Mission &out, std::string &err)
    {
        nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded())
        {
            err = "not valid JSON";
            return false;
        }
        if (!j.contains("mission"))
        {
            err = "this scenario has no mission block";
            return false;
        }
        const nlohmann::json &mi = j["mission"];
        out = Mission{};
        out.name = j.value("name", "Mission");
        out.description = j.value("description", "");
        if (j.contains("ships") && j["ships"].is_array() && !j["ships"].empty())
            out.ship = j["ships"][0].value("ship", "");
        out.ship_file = mi.value("ship_file", "");
        out.payload = mi.value("payload", 0.0);
        for (const nlohmann::json &o : mi.value("payloads", nlohmann::json::array()))
        {
            PayloadItem p;
            p.label = o.value("label", "Payload");
            p.ship_file = o.value("ship_file", "");
            p.count = std::max(1, o.value("count", 1));
            p.mass = o.value("mass", 0.0);
            p.bay = o.value("bay", "");
            if (o.contains("size") && o["size"].is_object())
            {
                const nlohmann::json &z = o["size"];
                p.size.known = true;
                p.size.round = !z.contains("width");
                p.size.diameter = z.value("diameter", 0.0);
                p.size.width = z.value("width", 0.0);
                p.size.height = z.value("height", 0.0);
                p.size.length = z.value("length", 0.0);
            }
            out.payloads.push_back(p);
        }
        out.margin = mi.value("margin", 0.05);
        if (mi.contains("start"))
        {
            const nlohmann::json &s = mi["start"];
            out.start.body = s.value("body", "Earth");
            out.start.base = s.value("base", "");
            out.start.altitude = s.value("altitude", 400000.0);
            out.start.inclination = s.value("inclination", 0.0);
            out.start.latitude = s.value("latitude", 0.0);
        }
        for (const nlohmann::json &l : mi.value("legs", nlohmann::json::array()))
        {
            Leg leg;
            leg.type = leg_type_from_name(l.value("type", "custom"));
            leg.name = l.value("name", "");
            leg.body = l.value("body", "");
            leg.to_body = l.value("to_body", "");
            leg.dv = l.value("dv", 0.0);
            leg.duration = l.value("duration", 0.0);
            leg.depart_days = l.value("depart_days", 0.0);
            leg.altitude = l.value("altitude", 0.0);
            leg.note = l.value("note", "");
            out.legs.push_back(leg);
        }
        return true;
    }

    bool save_mission(const std::string &path, const Mission &m, std::string &err)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f)
        {
            err = "cannot write " + path;
            return false;
        }
        f << to_scenario_json(m);
        return bool(f);
    }

    bool load_mission(const std::string &path, Mission &out, std::string &err)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            err = "cannot open " + path;
            return false;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        return from_scenario_json(ss.str(), out, err);
    }

    std::string summary_text(const Mission &m, double available)
    {
        std::ostringstream o;
        char buf[256];
        o << m.name << "\n";
        o << "Ship: " << m.ship << ", payload " << m.payload_mass() << " kg\n";
        for (const PayloadItem &p : m.payloads)
            o << "  " << p.count << " x " << p.label << " (" << p.mass << " kg each)\n";
        o << "Start: " << m.start.body << (m.start.in_orbit() ? " orbit" : " base " + m.start.base) << "\n\n";
        for (const LedgerRow &r : ledger(m, available))
        {
            std::snprintf(buf, sizeof buf, "%-32s %8.0f m/s  total %8.0f  left %8.0f%s\n", r.name.c_str(), r.dv, r.cumulative, r.remaining,
                          r.short_of ? "  SHORT" : "");
            o << buf;
        }
        std::snprintf(buf, sizeof buf, "\nTotal %.0f m/s of %.0f m/s available (%.0f%% margin held)\n", m.total_dv(), available, m.margin * 100);
        o << buf;
        return o.str();
    }

    std::string summary_csv(const Mission &m, double available)
    {
        std::ostringstream o;
        o << "leg,type,body,to_body,dv_ms,cumulative_ms,remaining_ms,depart_days,duration_days\n";
        const std::vector<LedgerRow> rows = ledger(m, available);
        char buf[96];
        for (size_t i = 0; i < m.legs.size(); ++i)
        {
            const Leg &l = m.legs[i];
            o << '"' << l.name << "\"," << leg_type_name(l.type) << ',' << l.body << ',' << l.to_body << ',';
            std::snprintf(buf, sizeof buf, "%.1f,%.1f,%.1f,%.3f,%.3f\n", l.dv, rows[i].cumulative, rows[i].remaining, l.depart_days, l.duration / DAY);
            o << buf;
        }
        return o.str();
    }
} // namespace km
