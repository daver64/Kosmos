#include <cmath>
#include <cctype>
#include "sim.h"

#include "json.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace kosmos
{
    namespace
    {
        const double warp_table[Sim::warp_levels] = {1, 2, 3, 4, 5, 10, 50, 100, 1000, 10000, 100000};
    }

    double Sim::warp_factor(int index)
    {
        return warp_table[std::clamp(index, 0, warp_levels - 1)];
    }

    Sim::Sim() = default;

    void Sim::log(const std::string &text)
    {
        messages_.push_back(text);
        message_times_.push_back(std::chrono::steady_clock::now());
        if (messages_.size() > 6)
        {
            messages_.erase(messages_.begin());
            message_times_.erase(message_times_.begin());
        }
        std::fprintf(stderr, "[kosmos] %s\n", text.c_str());
        if (log_sink)
            log_sink(text);
    }

    bool Sim::init(const std::string &root, std::string &error, int system)
    {
        root_ = root;
        system_ = std::clamp(system, 0, galaxy.size() - 1);
        galaxy.set_systems_dir(root + "/data/systems");
        universe.epoch_override = system_ == 0 ? start_epoch_jd : 0.0;
        if (system_ == 0 ? !universe.load(root + "/data/sol.json", error) : !universe.load_text(galaxy.system_json(system_), galaxy.star(system_).name, error))
            return false;
        universe.world.integrator = sl::FlightIntegrator::rk4;
        universe.world.max_substep = 0.1;
        universe.publish(props);
        publish_galaxy();

        for (const char *name : {"throttle", "pitch", "yaw", "roll", "rcs-x", "rcs-y", "rcs-z", "brake", "flaps"})
            props.set_double(std::string("/controls/") + name, 0.0);
        props.set_double("/controls/gear", 1.0);
        props.set_string("/controls/sas", "stability");
        props.set_double("/controls/target-pitch", 90.0);
        props.set_double("/controls/target-heading", 90.0);
        props.set_bool("/controls/stage", false);
        props.set_bool("/autopilot/enabled", false);
        props.set_string("/autopilot/status", "");
        props.set_int("/sim/warp-index", 0);
        props.set_bool("/sim/paused", false);
        props.set_int("/sim/active-ship", 0);
        props.set_int("/ships/count", 0);
        props.set_string("/sim/target", "");
        props.set_bool("/target/valid", false);
        props.node("/sim/warp")->tie_double([this] { return current_warp_; });

        lua.initialise([this](const std::string &text) { log(text); });
        sl::bind_property_tree(lua, props);
        mfd.init(lua, props);
        nav.init(props);
        weather.init(root_, user_dir(), props);
        scan_addons();
        return true;
    }

    bool Sim::change_system(int system, std::string &error)
    {
        clear_ships();
        const std::size_t old_count = universe.bodies.size();
        const int index = std::clamp(system, 0, galaxy.size() - 1);
        Universe fresh;
        fresh.epoch_override = index == 0 ? start_epoch_jd : 0.0;
        if (index == 0 ? !fresh.load(root_ + "/data/sol.json", error) : !fresh.load_text(galaxy.system_json(index), galaxy.star(index).name, error))
            return false;
        universe = std::move(fresh);
        system_ = index;
        universe.world.integrator = sl::FlightIntegrator::rk4;
        universe.world.max_substep = 0.1;
        if (sl::PropertyNode *bodies = props.node("/universe/bodies"))
            for (std::size_t i = old_count; i-- > 0;)
                bodies->remove_child("body", static_cast<int>(i));
        universe.publish(props);
        publish_galaxy();
        props.set_int("/sim/warp-index", 0);
        props.set_string("/sim/target", "");
        props.set_bool("/target/valid", false);
        props.set_bool("/autopilot/enabled", false);
        return true;
    }

    // /galaxy/current and the ten nearest stars, so scripts can plan jumps without a C++ binding of their own.
    void Sim::publish_galaxy()
    {
        props.set_int("/galaxy/star-count", galaxy.size());
        props.set_int("/galaxy/current", system_);
        const Star &here = galaxy.star(system_);
        props.set_string("/galaxy/current-name", here.name);
        props.set_string("/galaxy/current-class", std::string(1, here.spectral_class));
        const std::vector<int> near = galaxy.nearest(system_, 10);
        for (std::size_t i = 0; i < near.size(); ++i)
        {
            const std::string base = "/galaxy/nearest/star[" + std::to_string(i) + "]/";
            const Star &s = galaxy.star(near[i]);
            props.set_int(base + "index", near[i]);
            props.set_string(base + "name", s.name);
            props.set_string(base + "class", std::string(1, s.spectral_class));
            props.set_double(base + "distance-ly", galaxy.distance_ly(system_, near[i]));
            props.set_int(base + "planets", galaxy.planet_count(near[i]));
        }
        props.set_int("/galaxy/nearest/count", static_cast<int>(near.size()));
    }

    void Sim::scan_addons()
    {
        namespace fs = std::filesystem;
        addons_.clear();
        scenarios_.clear();
        bases.clear();
        std::vector<Transmitter> beacons;
        {
            std::string error;
            if (fs::exists(fs::path(root_) / "data" / "navaids.json") && !load_navaids((fs::path(root_) / "data" / "navaids.json").string(), beacons, error))
                log(error);
        }
        {
            std::vector<std::string> errors;
            scenarios_ = load_scenarios_in((fs::path(root_) / "data" / "scenarios").string(), errors);
            for (const std::string &e : errors)
                log("scenario " + e);
        }
        {
            std::vector<std::string> errors;
            bases = load_bases_in((fs::path(root_) / "data" / "bases").string(), errors);
            for (const std::string &e : errors)
                log("base " + e);
        }
        const fs::path dir = fs::path(root_) / "addons";
        if (!fs::is_directory(dir))
        {
            load_user_content();
            nav.set_beacons(std::move(beacons));
            return;
        }
        std::vector<fs::path> folders;
        for (const fs::directory_entry &entry : fs::directory_iterator(dir))
            if (entry.is_directory() && fs::exists(entry.path() / "addon.json"))
                folders.push_back(entry.path());
        std::sort(folders.begin(), folders.end());
        // The launcher writes <user dir>/addons.json as {"disabled": ["addon-id", ...]}.
        std::vector<std::string> disabled_addons;
        {
            const std::string user = user_dir();
            std::ifstream list(user.empty() ? fs::path() : fs::path(user) / "addons.json");
            if (list)
            {
                try
                {
                    nlohmann::json j;
                    list >> j;
                    disabled_addons = j.value("disabled", std::vector<std::string>());
                }
                catch (const std::exception &e)
                {
                    log(std::string("addons.json: ") + e.what());
                }
            }
        }
        for (const fs::path &folder : folders)
        {
            std::ifstream in(folder / "addon.json");
            nlohmann::json json;
            try
            {
                in >> json;
            }
            catch (const std::exception &e)
            {
                log("addon " + folder.filename().string() + ": " + e.what());
                continue;
            }
            Addon addon;
            addon.directory = folder.string();
            addon.id = json.value("id", folder.filename().string());
            if (std::find(disabled_addons.begin(), disabled_addons.end(), addon.id) != disabled_addons.end())
            {
                log("addon " + addon.id + " is disabled");
                continue;
            }
            addon.name = json.value("name", addon.id);
            addon.version = json.value("version", "0");
            for (const nlohmann::json &file : json.value("ships", nlohmann::json::array()))
            {
                ShipSpec spec;
                std::string error;
                if (load_ship_spec((folder / file.get<std::string>()).string(), spec, error))
                {
                    spec.directory = folder.string();
                    addon.ships.push_back(std::move(spec));
                }
                else
                    log("addon " + addon.id + ": " + error);
            }
            for (const nlohmann::json &entry : json.value("mfds", nlohmann::json::array()))
            {
                std::string error;
                const std::string id = entry.value("id", "");
                if (!mfd.register_mode(addon.id, id, (folder / entry.value("script", "")).string(), error, entry.value("domain", "any")))
                    log("addon " + addon.id + ": " + error);
            }
            for (const nlohmann::json &file : json.value("navaids", nlohmann::json::array()))
            {
                std::string error;
                if (!load_navaids((folder / file.get<std::string>()).string(), beacons, error))
                    log("addon " + addon.id + ": " + error);
            }
            {
                std::vector<std::string> errors;
                for (Scenario &sc : load_scenarios_in((folder / "scenarios").string(), errors))
                    scenarios_.push_back(std::move(sc));
                for (const std::string &e : errors)
                    log("addon " + addon.id + ": scenario " + e);
            }
            {
                std::vector<std::string> errors;
                for (Base &b : load_bases_in((folder / "bases").string(), errors))
                    bases.push_back(std::move(b));
                for (const std::string &e : errors)
                    log("addon " + addon.id + ": base " + e);
            }
            addons_.push_back(std::move(addon));
        }
        load_user_content();
        nav.set_beacons(std::move(beacons));
    }

    std::string Sim::user_dir()
    {
        const char *xdg = std::getenv("XDG_CONFIG_HOME");
        const char *home = std::getenv("HOME");
        const char *appdata = std::getenv("APPDATA");
        std::filesystem::path dir = xdg && *xdg ? std::filesystem::path(xdg) : home && *home ? std::filesystem::path(home) / ".config" : appdata ? std::filesystem::path(appdata) : std::filesystem::path();
        return dir.empty() ? std::string() : (dir / "kosmos").string();
    }

    void Sim::refresh_scenarios()
    {
        namespace fs = std::filesystem;
        std::vector<std::string> errors;
        scenarios_ = load_scenarios_in((fs::path(root_) / "data" / "scenarios").string(), errors);
        for (const Addon &addon : addons_)
            for (Scenario &sc : load_scenarios_in((fs::path(addon.directory) / "scenarios").string(), errors))
                scenarios_.push_back(std::move(sc));
        for (const std::string &e : errors)
            log("scenario " + e);
        load_user_content();
    }

    void Sim::load_user_content()
    {
        namespace fs = std::filesystem;
        const std::string user = user_dir();
        if (user.empty())
            return;
        std::vector<std::string> errors;
        for (Scenario &sc : load_scenarios_in((fs::path(user) / "scenarios").string(), errors))
        {
            const auto same = std::find_if(scenarios_.begin(), scenarios_.end(), [&](const Scenario &o) { return o.id == sc.id; });
            if (same != scenarios_.end())
                *same = std::move(sc);
            else
                scenarios_.push_back(std::move(sc));
        }
        for (Base &b : load_bases_in((fs::path(user) / "bases").string(), errors))
        {
            const auto same = std::find_if(bases.begin(), bases.end(), [&](const Base &o) { return o.id == b.id; });
            if (same != bases.end())
                *same = std::move(b);
            else
                bases.push_back(std::move(b));
        }
        for (const std::string &e : errors)
            log("user " + e);
    }

    const Scenario *Sim::find_scenario(const std::string &name) const
    {
        const auto lower = [](std::string t) {
            for (char &c : t)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return t;
        };
        const std::string key = lower(name);
        for (const Scenario &s : scenarios_)
            if (lower(s.id) == key || lower(s.name) == key)
                return &s;
        return nullptr;
    }

    const ShipSpec *Sim::find_ship_spec(const std::string &name) const
    {
        for (const Addon &addon : addons_)
            for (const ShipSpec &spec : addon.ships)
                if (spec.name == name)
                    return &spec;
        return nullptr;
    }

    Ship *Sim::spawn(const ShipSpec &spec)
    {
        // Unset transponder and port frequencies get a distinct value per ship so receivers can tell them apart.
        ShipSpec tuned = spec;
        const double slot = static_cast<double>(ships_.size());
        if (tuned.transponder <= 0.0)
            tuned.transponder = 108.0 + 0.1 * slot;
        for (std::size_t i = 0; i < tuned.ports.size(); ++i)
            if (tuned.ports[i].frequency <= 0.0)
                tuned.ports[i].frequency = 131.0 + 0.1 * (slot * 4.0 + static_cast<double>(i));
        ships_.push_back(std::make_unique<Ship>(universe, tuned));
        Ship *ship = ships_.back().get();
        ship->set_life_support(life_support);
        ship->publish(props, "/ships/ship[" + std::to_string(ships_.size() - 1) + "]");
        props.set_int("/ships/count", static_cast<int>(ships_.size()));
        return ship;
    }

    void Sim::clear_ships()
    {
        nav.clear_links();
        ships_.clear();
        active_ = 0;
        props.set_int("/ships/count", 0);
        script_loaded_ = false;
    }

    void Sim::activate(int index)
    {
        if (index < 0 || index >= ship_count())
            return;
        active_ = index;
        props.set_int("/sim/active-ship", index);
        script_loaded_ = false;
        const ShipSpec &spec = ships_[static_cast<std::size_t>(index)]->spec();
        if (spec.script.empty())
            return;
        std::ifstream in(std::filesystem::path(spec.directory) / spec.script);
        if (!in)
        {
            log("missing script " + spec.script);
            return;
        }
        const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const sl::LuaScriptResult result = lua.execute(source);
        if (!result.success)
            log("script error: " + result.error);
        script_loaded_ = result.success;
    }

    Ship *Sim::active_ship()
    {
        return active_ < ship_count() ? ships_[static_cast<std::size_t>(active_)].get() : nullptr;
    }

    Controls Sim::read_controls()
    {
        Controls c;
        c.throttle = props.get_double("/controls/throttle");
        c.hover = props.get_double("/controls/hover");
        c.pitch = props.get_double("/controls/pitch");
        c.yaw = props.get_double("/controls/yaw");
        c.roll = props.get_double("/controls/roll");
        c.brake = props.get_double("/controls/brake");
        c.flaps = props.get_double("/controls/flaps");
        c.gear = props.get_double("/controls/gear");
        c.rcs_x = props.get_double("/controls/rcs-x");
        c.rcs_y = props.get_double("/controls/rcs-y");
        c.rcs_z = props.get_double("/controls/rcs-z");
        c.sas = props.get_string("/controls/sas", "stability");
        c.target_pitch = props.get_double("/controls/target-pitch");
        c.target_heading = props.get_double("/controls/target-heading");
        c.stage = props.get_bool("/controls/stage");
        if (c.stage)
            props.set_bool("/controls/stage", false);
        return c;
    }

    void Sim::step(double frame)
    {
        // HUD messages are notices: they go after a few seconds.
        const auto now = std::chrono::steady_clock::now();
        while (!messages_.empty() && now - message_times_.front() > std::chrono::milliseconds(3500))
        {
            messages_.erase(messages_.begin());
            message_times_.erase(message_times_.begin());
        }
        if (props.get_bool("/sim/paused"))
            return;
        if (script_loaded_)
        {
            const sl::LuaScriptResult result = lua.emit("update", static_cast<float>(frame));
            if (!result.success)
            {
                log("script error: " + result.error);
                script_loaded_ = false;
            }
        }
        const Controls controls = read_controls();
        Controls idle;
        idle.sas = "off";

        int index = std::clamp(props.get_int("/sim/warp-index"), 0, warp_levels - 1);
        if (warp_factor(index) > max_physics_warp)
        {
            bool all_ok = !ships_.empty();
            std::string why;
            for (std::size_t i = 0; i < ships_.size(); ++i)
                if (!ships_[i]->on_rails())
                {
                    const std::string b = ships_[i]->rails_blocker(static_cast<int>(i) == active_ ? controls : idle);
                    if (!b.empty())
                    {
                        all_ok = false;
                        if (why.empty())
                            why = (static_cast<int>(i) == active_ ? std::string() : ships_[i]->spec().name + ": ") + b;
                    }
                }
            if (!all_ok)
            {
                if (!why.empty())
                    log("Time warp held at " + std::to_string(static_cast<int>(max_physics_warp)) + "x: " + why);
                while (index > 0 && warp_factor(index) > max_physics_warp)
                    --index;
                props.set_int("/sim/warp-index", index);
            }
        }
        const double warp = warp_factor(index);
        current_warp_ = warp;

        for (std::size_t i = 0; i < ships_.size(); ++i)
        {
            Ship &ship = *ships_[i];
            if (warp > max_physics_warp)
                ship.go_rails();
            else
                ship.go_dynamic();
            Controls held = idle;
            held.gear = ship.gear_position() > 0.5 ? 1.0 : 0.0;
            ship.apply_controls(static_cast<int>(i) == active_ ? controls : held, frame * warp);
        }

        if (active_ >= 0 && active_ < static_cast<int>(ships_.size()))
        {
            const Telemetry &tm = ships_[static_cast<std::size_t>(active_)]->telemetry();
            weather.step(universe, props, tm.soi, tm.latitude, tm.longitude, tm.altitude, frame * warp);
        }
        const double grip = weather.grip();
        for (const std::unique_ptr<Ship> &ship : ships_)
            if (ship->telemetry().soi == (active_ >= 0 && active_ < static_cast<int>(ships_.size()) ? ships_[static_cast<std::size_t>(active_)]->telemetry().soi : -1))
                ship->set_surface_grip(grip);
        universe.world.step(frame * warp);
        nav.couple(universe, ships_, active_, frame * warp);

        for (const std::unique_ptr<Ship> &ship : ships_)
        {
            ship->after_step(frame * warp);
            if (ship->on_rails() && ship->body().rails_parent != ship->telemetry().soi)
            {
                ship->go_dynamic();
                ship->after_step(0.0);
                if (warp > max_physics_warp && ship->can_rails(idle))
                    ship->go_rails();
                else if (warp > max_physics_warp)
                    props.set_int("/sim/warp-index", 0);
            }
        }
        update_target();
        nav.update(universe, ships_, active_);
    }

    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double two_pi = 2.0 * pi;

        double wrap_two_pi(double a)
        {
            a = std::fmod(a, two_pi);
            return a < 0.0 ? a + two_pi : a;
        }

        // Seconds until a bound orbit reaches true anomaly `to` starting from `from` (radians).
        double time_between_anomalies(double from, double to, double e, double n)
        {
            const auto mean = [&](double nu) {
                const double ecc = 2.0 * std::atan(std::sqrt((1.0 - e) / (1.0 + e)) * std::tan(nu / 2.0));
                return wrap_two_pi(ecc - e * std::sin(ecc));
            };
            return wrap_two_pi(mean(wrap_two_pi(to)) - mean(wrap_two_pi(from))) / n;
        }
    } // namespace

    void Sim::update_target()
    {
        Ship *me = active_ship();
        const std::string name = props.get_string("/sim/target");
        int ship_index = -1, body_id = -1;
        for (std::size_t i = 0; i < ships_.size(); ++i)
            if (ships_[i].get() != me && ships_[i]->name == name)
                ship_index = static_cast<int>(i);
        if (ship_index < 0)
            body_id = universe.find(name);
        if (!me || (ship_index < 0 && body_id < 0))
        {
            props.set_bool("/target/valid", false);
            return;
        }
        const sl::FlightBody &mb = me->body();
        sl::dvec3 tp, tv;
        int parent = -1;
        if (ship_index >= 0)
        {
            const Ship &t = *ships_[static_cast<std::size_t>(ship_index)];
            tp = t.body().position;
            tv = t.body().velocity;
            parent = t.telemetry().soi;
        }
        else
        {
            tp = universe.position(body_id);
            tv = universe.world.body(body_id)->velocity;
            parent = universe.bodies[static_cast<std::size_t>(body_id)].parent;
        }
        const int soi = me->telemetry().soi;
        const sl::dvec3 rel = tp - mb.position, relv = tv - mb.velocity;
        const double dist = glm::length(rel);
        props.set_bool("/target/valid", true);
        props.set_string("/target/kind", ship_index >= 0 ? "ship" : "body");
        props.set_double("/target/distance", dist);
        props.set_double("/target/relative-speed", glm::length(relv));
        props.set_double("/target/closing-speed", dist > 0.0 ? -glm::dot(rel, relv) / dist : 0.0);
        props.set_bool("/target/same-soi", parent == soi);

        sl::OrbitElements te;
        const bool has_orbit = parent >= 0 && universe.world.orbit_of(ship_index >= 0 ? ships_[static_cast<std::size_t>(ship_index)]->body_id() : body_id, parent, te);
        props.set_bool("/target/has-orbit", has_orbit);
        if (has_orbit)
        {
            const double mu = universe.bodies[static_cast<std::size_t>(parent)].gm;
            props.set_double("/target/orbit/inclination", te.inclination * 180.0 / pi);
            props.set_double("/target/orbit/raan", te.raan * 180.0 / pi);
            props.set_double("/target/orbit/eccentricity", te.eccentricity);
            props.set_double("/target/orbit/semi-major-axis", te.semi_major_axis);
            props.set_double("/target/orbit/period", te.period(mu));
        }

        // Plane alignment and phasing only make sense for two bound orbits about the same body.
        const Telemetry &tm = me->telemetry();
        bool aligned = false;
        if (parent == soi && tm.bound)
        {
            const sl::dvec3 centre = universe.position(soi), cv = universe.world.body(soi)->velocity;
            const sl::dvec3 rs = mb.position - centre, vs = mb.velocity - cv;
            const sl::dvec3 rt = tp - centre, vt = tv - cv;
            const sl::dvec3 hs = glm::cross(rs, vs), ht = glm::cross(rt, vt);
            if (glm::length(hs) > 1e-6 && glm::length(ht) > 1e-6)
            {
                const sl::dvec3 hsn = glm::normalize(hs), htn = glm::normalize(ht);
                const double rel_inc = std::acos(std::clamp(glm::dot(hsn, htn), -1.0, 1.0));
                props.set_double("/target/relative-inclination", rel_inc * 180.0 / pi);
                const sl::dvec3 node = glm::cross(htn, hsn);
                if (glm::length(node) > 1e-9)
                {
                    const sl::dvec3 an = glm::normalize(node);
                    const double to_an = wrap_two_pi(std::atan2(glm::dot(glm::cross(rs, an), hsn), glm::dot(rs, an)));
                    const double nu = tm.true_anomaly * pi / 180.0;
                    const double n = std::sqrt(universe.bodies[static_cast<std::size_t>(soi)].gm / std::pow(std::fabs(tm.semi_major_axis), 3.0));
                    props.set_double("/target/an-time", time_between_anomalies(nu, nu + to_an, tm.eccentricity, n));
                    props.set_double("/target/dn-time", time_between_anomalies(nu, nu + to_an + pi, tm.eccentricity, n));
                    props.set_double("/target/an-true-anomaly", wrap_two_pi(nu + to_an) * 180.0 / pi);
                    props.set_double("/target/dn-true-anomaly", wrap_two_pi(nu + to_an + pi) * 180.0 / pi);
                }
                // Phase angle: how far the target leads the ship, measured in the ship's orbital plane.
                const sl::dvec3 rtp = rt - hsn * glm::dot(rt, hsn);
                props.set_double("/target/phase-angle", std::atan2(glm::dot(glm::cross(rs, rtp), hsn), glm::dot(rs, rtp)) * 180.0 / pi);
                props.set_double("/target/radius", glm::length(rt));
                props.set_double("/target/ship-radius", glm::length(rs));
                aligned = true;
            }
        }
        props.set_bool("/target/in-plane-data", aligned);
    }
} // namespace kosmos
