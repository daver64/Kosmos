#include <cmath>
#include <cctype>
#include "sim.h"
#include "transfer_plan.h"

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
        universe.epoch_override = start_epoch_jd;
        if (system_ == 0 ? !universe.load(root + "/data/sol.json", error) : !universe.load_text(galaxy.system_json(system_), galaxy.star(system_).name, error))
            return false;
        universe.stellar_luminosity = galaxy.star(system_).luminosity;
        universe.world.integrator = sl::FlightIntegrator::rk4;
        universe.world.max_substep = 0.1;
        universe.publish(props);
        publish_galaxy();

        for (const char *name : {"throttle", "pitch", "yaw", "roll", "rcs-x", "rcs-y", "rcs-z", "brake", "flaps"})
            props.set_double(std::string("/controls/") + name, 0.0);
        props.set_double("/controls/gear", 1.0);
        props.set_string("/controls/af-mode", "on");
        props.set_bool("/controls/airlock/pressurize", true);
        props.set_double("/controls/trim", 0.0);
        props.set_double("/controls/gimbal-pitch", 0.0);
        props.set_double("/controls/gimbal-yaw", 0.0);
        props.set_bool("/controls/lighting/lamps", true);
        props.set_bool("/controls/lighting/nav", false);
        props.set_double("/controls/brake-latch", 0.0);
        props.set_string("/controls/sas", "stability");
        props.set_double("/controls/target-pitch", 90.0);
        props.set_double("/controls/target-heading", 90.0);
        props.set_bool("/controls/stage", false);
        props.set_bool("/autopilot/enabled", false);
        props.set_string("/autopilot/status", "");
        // Aircraft autopilot: which modes are armed and their targets (metres, m/s, degrees); targets are captured when it engages.
        for (const char *mode : {"hdg-hold", "alt-hold"})
            props.set_bool(std::string("/autopilot/ac/") + mode, true);
        for (const char *mode : {"vs-hold", "spd-hold", "nav", "appr"})
            props.set_bool(std::string("/autopilot/ac/") + mode, false);
        for (const char *target : {"heading", "altitude", "vs", "speed"})
            props.set_double(std::string("/autopilot/ac/") + target, 0.0);
        props.set_int("/autopilot/ac/receiver", 0);
        // Spacecraft ascent autopilot: target orbit altitude in km and launch heading.
        props.set_double("/autopilot/sc/target-altitude", 200.0);
        props.set_double("/autopilot/sc/heading", 90.0);
        props.set_int("/sim/warp-index", 0);
        props.set_bool("/sim/paused", false);
        props.set_int("/sim/active-ship", 0);
        props.set_int("/ships/count", 0);
        props.set_string("/sim/target", "");
        props.set_bool("/target/valid", false);
        props.node("/sim/warp")->tie_double([this] { return current_warp_; });
        props.node("/sim/time")->tie_double([this] { return universe.time(); });
        props.node("/sim/frame")->tie_int([this] { return static_cast<int>(frame_count_ & 0x3fffffff); });
        props.set_bool("/autopilot/burn/armed", false);
        props.set_string("/autopilot/burn/state", "");
        props.set_bool("/autopilot/land/armed", false);
        props.set_string("/autopilot/land/state", "");
        props.set_bool("/autopilot/timed/armed", false);
        props.set_string("/autopilot/timed/state", "");
        props.set_string("/autopilot/vessel-ascent/request", "");
        props.set_string("/autopilot/vessel-ascent/state", "Off");
        props.set_bool("/autopilot/vessel-ascent/active", false);
        props.set_double("/autopilot/vessel-ascent/target-km", 200.0);
        props.set_double("/autopilot/vessel-ascent/heading", 90.0);

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
        fresh.epoch_override = start_epoch_jd;
        if (index == 0 ? !fresh.load(root_ + "/data/sol.json", error) : !fresh.load_text(galaxy.system_json(index), galaxy.star(index).name, error))
            return false;
        fresh.stellar_luminosity = galaxy.star(index).luminosity;
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
        props.set_bool("/autopilot/land/armed", false);
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

    // The launcher writes <user dir>/addons.json as {"disabled": ["addon-id", ...]}.
    std::vector<std::string> Sim::disabled_addon_ids()
    {
        namespace fs = std::filesystem;
        std::vector<std::string> disabled;
        const std::string user = user_dir();
        std::ifstream list(user.empty() ? fs::path() : fs::path(user) / "addons.json");
        if (list)
        {
            try
            {
                nlohmann::json j;
                list >> j;
                disabled = j.value("disabled", std::vector<std::string>());
            }
            catch (const std::exception &e)
            {
                log(std::string("addons.json: ") + e.what());
            }
        }
        return disabled;
    }

    void Sim::load_addon(const std::string &folder_path, const std::vector<std::string> &disabled_addons, std::vector<Transmitter> &beacons)
    {
        namespace fs = std::filesystem;
        const fs::path folder(folder_path);
        std::ifstream in(folder / "addon.json");
        nlohmann::json json;
        try
        {
            in >> json;
        }
        catch (const std::exception &e)
        {
            log("addon " + folder.filename().string() + ": " + e.what());
            return;
        }
        Addon addon;
        addon.directory = folder.string();
        addon.id = json.value("id", folder.filename().string());
        if (std::find(disabled_addons.begin(), disabled_addons.end(), addon.id) != disabled_addons.end())
        {
            log("addon " + addon.id + " is disabled");
            return;
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
        for (const nlohmann::json &file : json.value("buildings", nlohmann::json::array()))
        {
            ShipSpec spec;
            std::string error;
            if (load_ship_spec((folder / file.get<std::string>()).string(), spec, error))
            {
                spec.directory = folder.string();
                spec.kind = "building";
                addon.buildings.push_back(std::move(spec));
            }
            else
                log("addon " + addon.id + ": " + error);
        }
        for (const nlohmann::json &entry : json.value("mfds", nlohmann::json::array()))
        {
            std::string error;
            const std::string id = entry.value("id", "");
            if (!mfd.register_mode(addon.id, id, (folder / entry.value("script", "")).string(), error, entry.value("domain", "any"), entry.value("ships", std::vector<std::string>{}), entry.value("short", ""), entry.value("name", "")))
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
            scenarios_ = load_scenarios_in((fs::path(root_) / "scenarios").string(), errors);
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
        const std::vector<std::string> disabled_addons = disabled_addon_ids();
        for (const fs::path &folder : folders)
            load_addon(folder.string(), disabled_addons, beacons);
        load_user_content();
        nav.set_beacons(std::move(beacons));
        {
            std::vector<std::string> directories, problems;
            for (const Addon &addon : addons_)
                directories.push_back(addon.directory);
            equipment.load(directories, problems);
            weapon_specs.load(directories, problems);
            for (const std::string &p : problems)
                log("equipment: " + p);
        }
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
        scenarios_ = load_scenarios_in((fs::path(root_) / "scenarios").string(), errors);
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
        props.set_int("/sim/bases/count", static_cast<int>(bases.size()));
        for (std::size_t i = 0; i < bases.size(); ++i)
        {
            const std::string at = "/sim/bases/base[" + std::to_string(i) + "]/";
            props.set_string(at + "id", bases[i].id);
            props.set_string(at + "name", bases[i].name);
            props.set_string(at + "body", bases[i].body);
            props.set_double(at + "latitude", bases[i].latitude);
            props.set_double(at + "longitude", bases[i].longitude);
        }
    }

    void Sim::add_scenario(Scenario scenario)
    {
        const auto same = std::find_if(scenarios_.begin(), scenarios_.end(), [&](const Scenario &o) { return o.id == scenario.id; });
        if (same != scenarios_.end())
            *same = std::move(scenario);
        else
            scenarios_.push_back(std::move(scenario));
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

    const ShipSpec *Sim::find_building_spec(const std::string &name) const
    {
        for (const Addon &addon : addons_)
            for (const ShipSpec &spec : addon.buildings)
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
        cancel_autoland("Autoland cancelled: flight changed");
        cancel_timed_burn("Timed burn cancelled: flight changed");
        cancel_vessel_ascent("Ascent cancelled: flight changed");
        nav.clear_links();
        ships_.clear();
        active_ = 0;
        props.set_int("/ships/count", 0);
        script_loaded_ = false;
    }

    bool Sim::remove_ship(int index)
    {
        if (index < 0 || index >= ship_count() || index == active_)
            return false;
        nav.clear_links();
        std::map<int, LinkClaim> shifted;
        for (auto &entry : link_claims)
            if (entry.first != index)
                shifted[entry.first > index ? entry.first - 1 : entry.first] = entry.second;
        link_claims = std::move(shifted);
        ships_.erase(ships_.begin() + index);
        if (active_ > index)
            --active_;
        for (int i = index; i < ship_count(); ++i)
            ships_[static_cast<std::size_t>(i)]->publish(props, "/ships/ship[" + std::to_string(i) + "]");
        props.set_int("/sim/active-ship", active_);
        props.set_int("/ships/count", ship_count());
        return true;
    }

    // On the last stage the stage key lets go of the payload, the rearmost docked one first, and hands you its controls.
    void Sim::release_payload_on_stage()
    {
        Ship *ship = active_ship();
        if (!ship || !props.get_bool("/controls/stage") || ship->active_stage() < ship->stage_count() - 1)
            return;
        int best = -1, other = -1, port = -1;
        double rear = -1e30;
        for (const std::array<int, 4> &l : nav.dock_links())
        {
            const bool lead = l[0] == active_;
            if (!lead && l[2] != active_)
                continue;
            const int mine = lead ? l[1] : l[3];
            const double z = ship->spec().ports[static_cast<std::size_t>(mine)].position.z;
            if (z > rear)
            {
                rear = z;
                port = mine;
                other = lead ? l[2] : l[0];
                best = 1;
            }
        }
        if (best < 0)
            return;
        props.set_bool("/controls/stage", false);
        if (nav.undock(ships_, active_, port))
            activate(other);
    }

    void Sim::activate(int index)
    {
        if (index < 0 || index >= ship_count())
            return;
        cancel_autoland("Autoland cancelled: vessel changed");
        cancel_timed_burn("Timed burn cancelled: vessel changed");
        cancel_vessel_ascent("Ascent cancelled: vessel changed");
        active_ = index;
        props.set_int("/sim/active-ship", index);
        props.set_bool("/autopilot/burn/armed", false);
        props.set_bool("/autopilot/land/armed", false);
        const Ship &ship = *ships_[static_cast<std::size_t>(index)];
        flying_as_aircraft_ = ship.flies_as_aircraft();
        load_active_script();
    }

    void Sim::load_active_script()
    {
        script_loaded_ = false;
        const ShipSpec &spec = active_ship()->spec();
        // A script name starting with "@" is one of the shared programs in data/autopilot.
        const std::filesystem::path script = spec.script.rfind("@", 0) == 0 ? std::filesystem::path(root()) / "data/autopilot" / spec.script.substr(1)
                                             : !spec.script.empty() ? std::filesystem::path(spec.directory) / spec.script
                                             : flying_as_aircraft_ ? std::filesystem::path(root()) / "data/autopilot/aircraft.lua"
                                                                   : std::filesystem::path();
        if (script.empty())
            return;
        std::ifstream in(script);
        if (!in)
        {
            log("missing script " + script.string());
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

    Controls Sim::read_controls(const std::string &prefix, const Ship *ship)
    {
        Controls c;
        auto path_of = [&](const char *name) { return prefix + "/" + name; };
        c.throttle = props.get_double(path_of("throttle"));
        c.hover = props.get_double(path_of("hover"));
        c.pitch = props.get_double(path_of("pitch"));
        c.yaw = props.get_double(path_of("yaw"));
        c.roll = props.get_double(path_of("roll"));
        c.brake = props.get_double(path_of("brake"));
        c.flaps = props.get_double(path_of("flaps"));
        c.gear = props.get_double(path_of("gear"));
        c.rcs_x = props.get_double(path_of("rcs-x"));
        c.rcs_y = props.get_double(path_of("rcs-y"));
        c.rcs_z = props.get_double(path_of("rcs-z"));
        c.pod_left = props.get_double(path_of("pod-left"));
        c.pod_right = props.get_double(path_of("pod-right"));
        c.pod_tilt_left = props.get_double(path_of("pod-tilt-left"));
        c.pod_tilt_right = props.get_double(path_of("pod-tilt-right"));
        c.rcs_mode = props.get_string(path_of("rcs-mode"), "rot");
        c.af_mode = props.get_string(path_of("af-mode"), "on");
        c.trim = props.get_double(path_of("trim"));
        c.gimbal_pitch = props.get_double(path_of("gimbal-pitch"));
        c.gimbal_yaw = props.get_double(path_of("gimbal-yaw"));
        if (!ship)
            ship = active_ship();
        if (ship)
            for (std::size_t i = 0; i < ship->spec().animation_controls.size(); ++i)
            {
                const std::string path = prefix + "/anim/" + ship->spec().animation_controls[i].property;
                c.anim.push_back(props.exists(path) ? props.get_double(path) : ship->animation_position(i));
            }
        c.sas = props.get_string(path_of("sas"), "stability");
        c.target_pitch = props.get_double(path_of("target-pitch"));
        c.target_heading = props.get_double(path_of("target-heading"));
        c.ascent_attitude = prefix == "/controls" && ascent_ship_ == active_;
        c.parachute = props.get_int(path_of("parachute"));
        if (c.parachute != 0)
            props.set_int(path_of("parachute"), 0);
        c.stage = props.get_bool(path_of("stage"));
        if (c.stage)
            props.set_bool(path_of("stage"), false);
        return c;
    }

    int Sim::advance(double frame)
    {
        ++frame_count_;
        follow_burn_plan();
        if (current_warp_ > 4.0 || props.get_bool("/sim/paused"))
        {
            step_accumulator_ = 0.0;
            step(frame);
            return 1;
        }
        step_accumulator_ = std::min(step_accumulator_ + frame, fixed_step * max_steps_per_frame);
        int steps = 0;
        while (step_accumulator_ >= fixed_step)
        {
            step(fixed_step);
            step_accumulator_ -= fixed_step;
            ++steps;
        }
        return steps;
    }

    // The airlock chamber follows its pressurize switch; a door opens only against matching pressure, and never both at once.
    void Sim::update_airlock(double frame)
    {
        double pressure = props.get_double("/airlock/pressure", 1.0);
        bool outer = props.get_bool("/controls/airlock/outer-door"), inner = props.get_bool("/controls/airlock/inner-door");
        if (outer && inner)
            inner = false;
        if (outer && pressure > 0.05)
            outer = false;
        if (inner && pressure < 0.95)
            inner = false;
        const bool pressurize = props.get_bool("/controls/airlock/pressurize", true);
        if (!outer && !inner)
            pressure = std::clamp(pressure + (pressurize ? 1.0 : -1.0) * frame / 20.0, 0.0, 1.0);
        props.set_bool("/controls/airlock/outer-door", outer);
        props.set_bool("/controls/airlock/inner-door", inner);
        props.set_bool("/airlock/outer-open", outer);
        props.set_bool("/airlock/inner-open", inner);
        props.set_double("/airlock/pressure", pressure);
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
        if (ascent_ship_ >= 0 && (props.get_bool("/autopilot/enabled") || props.get_bool("/autopilot/land/armed") ||
                                 props.get_bool("/autopilot/burn/armed") || props.get_bool("/autopilot/timed/armed")))
            cancel_vessel_ascent("Ascent cancelled: another autopilot engaged");
        if (Ship *flown = active_ship(); flown && flown->spec().hybrid && flown->flies_as_aircraft() != flying_as_aircraft_)
        {
            // Crossing between air and space changes which autopilot applies, so the old one is disengaged.
            props.set_bool("/autopilot/enabled", false);
            if (ascent_ship_ != active_)
                activate(active_);
            else
            {
                flying_as_aircraft_ = flown->flies_as_aircraft();
                load_active_script();
            }
            log(flying_as_aircraft_ ? "Aircraft mode: wings and aircraft autopilot" : "Spacecraft mode: RCS and SAS, autopilot off");
        }
        update_airlock(frame);
        if (script_loaded_)
        {
            // Scripts see simulated seconds, so a ramp or a timer keeps its pace under time warp.
            const sl::LuaScriptResult result = lua.emit("update", static_cast<float>(frame * std::max(current_warp_, 1.0)));
            if (!result.success)
            {
                log("script error: " + result.error);
                script_loaded_ = false;
            }
        }
        run_vessel_ascent(frame);
        run_timed_burn();
        run_autoland();
        run_burn();
        release_payload_on_stage();
        const Controls controls = read_controls();
        Controls idle;
        idle.sas = "off";

        int index = std::clamp(props.get_int("/sim/warp-index"), 0, warp_levels - 1);
        // Everything sitting still on the ground can be warped in physics up to ground_warp.
        bool ground_hold = !ships_.empty();
        for (const std::unique_ptr<Ship> &ship : ships_)
        {
            const Telemetry &t = ship->telemetry();
            if (!ship->spec().building() && !ship->crashed() && (ship->on_rails() || !t.landed || t.surface_speed > 0.5 || t.thrust > 0.0))
                ground_hold = false;
        }
        // Burning in vacuum may be warped up to thrust_warp in physics; in or near the air, or on the ground, it may not.
        bool vacuum_burn = false;
        if (!ground_hold)
        {
            bool all_vacuum = !ships_.empty();
            for (std::size_t i = 0; i < ships_.size() && all_vacuum; ++i)
            {
                const Ship &ship = *ships_[i];
                if (ship.spec().building() || ship.crashed())
                    continue;
                const std::string b = ship.rails_blocker(static_cast<int>(i) == active_ ? controls : idle);
                if (b == "engines are firing" && ship.rails_margin() > 0.0)
                    vacuum_burn = true;
                else if (!b.empty())
                    all_vacuum = false;
            }
            vacuum_burn = vacuum_burn && all_vacuum;
        }
        const double phys_limit = ground_hold ? ground_warp : vacuum_burn ? thrust_warp : max_physics_warp;
        if (warp_factor(index) > phys_limit)
        {
            bool all_ok = !ships_.empty() && !ground_hold;
            std::string why;
            for (std::size_t i = 0; i < ships_.size(); ++i)
                if (!ships_[i]->on_rails() && !ships_[i]->spec().building() && !ships_[i]->crashed())
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
                    log("Time warp held at " + std::to_string(static_cast<int>(phys_limit)) + "x: " + why);
                while (index > 0 && warp_factor(index) > phys_limit)
                    --index;
                props.set_int("/sim/warp-index", index);
            }
        }
        // Coasting towards the air at high warp: keep each frame's travel well inside the gap, so the clock is already at 4x at the top of the atmosphere.
        if (warp_factor(index) > phys_limit && !ground_hold)
        {
            double allowed = 1.0e18;
            for (const std::unique_ptr<Ship> &ship : ships_)
                if (!ship->spec().building() && !ship->crashed())
                {
                    // Leaving on an escape path, the air is behind the ship however fast it goes.
                    const Telemetry &tm = ship->telemetry();
                    if (!tm.bound && tm.vertical_speed > 0.0)
                        continue;
                    // An orbit whose lowest point clears the air never reaches it, so there is nothing to slow down for.
                    if (tm.bound && tm.periapsis > tm.altitude - ship->rails_margin())
                        continue;
                    const double speed = std::max(1.0, glm::length(ship->relative_velocity()));
                    allowed = std::min(allowed, std::max(0.0, ship->rails_margin()) / (2.0 * speed * std::max(frame, 1.0 / 60.0)));
                }
            while (index > 0 && warp_factor(index) > phys_limit && warp_factor(index) > allowed)
                --index;
            props.set_int("/sim/warp-index", index);
        }
        const double warp = warp_factor(index);
        current_warp_ = warp;

        for (std::size_t i = 0; i < ships_.size(); ++i)
        {
            Ship &ship = *ships_[i];
            if (ship.spec().building())
                ship.go_dynamic();
            else if (warp > phys_limit)
                ship.go_rails();
            else
                ship.go_dynamic();
            Controls held = idle;
            held.gear = ship.gear_position() > 0.5 ? 1.0 : 0.0;
            const auto claim = link_claims.find(static_cast<int>(i));
            if (claim != link_claims.end() && !(claim->second.passthrough && static_cast<int>(i) == active_))
            {
                const std::string prefix = "/ships/ship[" + std::to_string(i) + "]/module/" + claim->second.addon + "/controls";
                Controls module = read_controls(prefix, &ship);
                if (!props.exists(prefix + "/gear"))
                    module.gear = held.gear;
                ship.apply_controls(module, frame * warp);
            }
            else
                ship.apply_controls(static_cast<int>(i) == active_ ? controls : held, frame * warp);
            if (claim != link_claims.end() && warp <= phys_limit)
            {
                const double limit_force = ship.spec().module_max_force > 0.0 ? ship.spec().module_max_force : ship.telemetry().mass * 20.0;
                const double limit_torque = ship.spec().module_max_torque > 0.0 ? ship.spec().module_max_torque : ship.telemetry().mass * 20.0;
                auto clamped = [](const double v[3], double limit) {
                    sl::dvec3 out(v[0], v[1], v[2]);
                    const double len = glm::length(out);
                    return len > limit && len > 0.0 ? out * (limit / len) : out;
                };
                universe.world.apply_force(ship.body_id(), ship.body().direction_to_world(clamped(claim->second.force, limit_force)));
                universe.world.apply_torque(ship.body_id(), ship.body().direction_to_world(clamped(claim->second.torque, limit_torque)));
            }
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
        // On rails everything is analytic, so big substeps lose nothing and keep a 100000x frame cheap.
        universe.world.max_substep = warp <= 4.0 ? fixed_step : (warp > phys_limit ? 1.0e12 : (ground_hold ? (warp > 100.0 ? 1.0e12 : fixed_step) : 0.05));
        // Past 100x a motionless ship is held to the ground like a building, so one huge step cannot throw its gear springs.
        for (const std::unique_ptr<Ship> &ship : ships_)
            if (!ship->spec().building())
                ship->hold_for_warp(ground_hold && warp > 100.0);
        universe.world.step(frame * warp);
        nav.couple(universe, ships_, active_, frame * warp);

        for (const std::unique_ptr<Ship> &ship : ships_)
        {
            ship->after_step(frame * warp);
            if (ship->on_rails() && ship->body().rails_parent != ship->telemetry().soi)
            {
                ship->go_dynamic();
                ship->after_step(0.0);
                if (warp > phys_limit && ship->can_rails(idle))
                    ship->go_rails();
                else if (warp > phys_limit)
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

    double Sim::burn_lead_seconds() const
    {
        // Never closer than five minutes: the planner drifts, and landing past the ignition jumps the plan to the next window.
        return std::max(300.0, std::clamp(props.get_int("/sim/settings/burn-lead-min", 5), 0, 120) * 60.0);
    }

    std::vector<Sim::TransferWindow> Sim::transfer_windows(int count, std::string &why)
    {
        std::vector<TransferWindow> out;
        const Ship *me = active_ship();
        if (!me || !props.get_bool("/target/valid"))
        {
            why = "No target selected";
            return out;
        }
        if (!props.get_bool("/target/in-plane-data"))
        {
            why = "Target is not orbiting the same body";
            return out;
        }
        const Telemetry &t = me->telemetry();
        if (!t.bound || t.soi < 0 || t.soi >= static_cast<int>(universe.bodies.size()))
        {
            why = "Not in a bound orbit";
            return out;
        }
        const double mu = universe.bodies[static_cast<std::size_t>(t.soi)].gm;
        const double r1 = props.get_double("/target/ship-radius");
        const double r2 = props.get_double("/target/radius");
        const double phase = props.get_double("/target/phase-angle") * 3.14159265358979323846 / 180.0;
        const HohmannPlan plan = plan_hohmann(mu, r1, r2, phase);
        if (!plan.has_window)
        {
            why = "Orbits have the same period, so there is no window to wait for";
            return out;
        }
        const double burn = t.max_thrust > 0.0 ? std::fabs(plan.dv1) * t.mass / t.max_thrust : 0.0;
        for (int k = 0; k < count + 64 && static_cast<int>(out.size()) < count; ++k)
        {
            const double ignition = window_wait(plan, k) - 0.5 * burn;
            if (ignition <= 0.0)
                continue;
            out.push_back({ignition, burn, plan.dv1, plan.tof});
        }
        if (out.empty())
            why = "No window ahead";
        return out;
    }

    Sim::PlannedBurn Sim::planned_burn()
    {
        PlannedBurn result;
        const double now = universe.time();
        // A mode publishes its plan while it is drawing; a stale stamp means it is closed or has no plan.
        struct Candidate { const char *prefix, *name; };
        long best_stamp = -1;
        for (const Candidate &c : {Candidate{"/plan/transfer", "Transfer MFD"}, Candidate{"/plan/orbitcomp", "OrbitComp MFD"}})
        {
            const long stamp = props.get_int(std::string(c.prefix) + "/stamp", -1);
            if (stamp < 0 || frame_count_ - stamp > 5 || stamp < best_stamp)
                continue;
            const double at = props.get_double(std::string(c.prefix) + "/at");
            best_stamp = stamp;
            result.valid = true;
            result.source = c.name;
            result.ignition_in = at - now;
        }
        if (result.valid)
            return result;
        std::string why;
        const std::vector<TransferWindow> windows = transfer_windows(1, why);
        if (windows.empty())
        {
            result.why = why;
            return result;
        }
        result.valid = true;
        result.source = "Hohmann estimate";
        result.ignition_in = windows.front().ignition_in;
        return result;
    }

    // The planner's idea of the burn drifts by up to about a fifth of any skip (it assumes circular orbits), so a long gap is
    // approached in fractions that cannot overshoot; the last couple of minutes are taken at once.
    static double seek_step(bool follow, double gap)
    {
        return follow && gap > 120.0 ? gap * 0.7 : gap;
    }

    bool Sim::skip_to_burn(double ignition_in, bool follow, std::string &why)
    {
        seek_ = BurnSeek{};
        const double gap = ignition_in - burn_lead_seconds();
        const double step = seek_step(follow, gap);
        if (!skip_time(step, why))
            return false;
        if (follow)
        {
            const PlannedBurn plan = planned_burn();
            seek_ = BurnSeek{true, 30, 12, gap - step, plan.valid ? plan.source : std::string()};
        }
        return true;
    }

    void Sim::follow_burn_plan()
    {
        if (!seek_.active)
            return;
        if (seek_.wait > 0)
        {
            --seek_.wait;
            return;
        }
        const PlannedBurn plan = planned_burn();
        const double gap = plan.ignition_in - burn_lead_seconds();
        // A different source, or a gap that grew by more than drift explains (a jump to the next window), means the plan changed under us: leave the date where it is.
        if (!plan.valid || plan.source != seek_.source || gap > seek_.previous_gap * 1.25 + 3600.0 || --seek_.passes < 0)
        {
            seek_.active = false;
            return;
        }
        if (gap < 20.0)
        {
            seek_.active = false;
            return;
        }
        std::string why;
        const double step = seek_step(true, gap);
        if (!skip_time(step, why))
        {
            seek_.active = false;
            return;
        }
        seek_.previous_gap = gap - step;
        seek_.wait = 3;
    }

    bool Sim::skip_time(double seconds, std::string &why)
    {
        if (props.get_bool("/autopilot/timed/armed") || ascent_ship_ >= 0)
        {
            why = "Cancel the active autopilot before skipping time";
            return false;
        }
        if (seconds <= 0.0)
        {
            why = "Already within the lead time of the burn";
            return false;
        }
        if (ships_.empty())
        {
            why = "No vessels";
            return false;
        }
        const Controls controls = read_controls();
        Controls idle;
        idle.sas = "off";
        for (std::size_t i = 0; i < ships_.size(); ++i)
        {
            const Ship &ship = *ships_[i];
            if (ship.spec().building() || ship.crashed() || ship.on_rails())
                continue;
            const std::string b = ship.rails_blocker(static_cast<int>(i) == active_ ? controls : idle);
            if (!b.empty())
            {
                why = (static_cast<int>(i) == active_ ? std::string() : ship.spec().name + ": ") + b;
                return false;
            }
        }
        std::vector<bool> was_dynamic(ships_.size(), false);
        for (std::size_t i = 0; i < ships_.size(); ++i)
        {
            Ship &ship = *ships_[i];
            was_dynamic[i] = !ship.on_rails();
            if (!ship.spec().building() && !ship.crashed())
                ship.go_rails();
        }
        const double saved_substep = universe.world.max_substep;
        universe.world.max_substep = 1.0e12;
        universe.world.step(seconds);
        universe.world.max_substep = saved_substep;
        // Only the position and the date move: a zero step keeps consumables, systems, mission time and nav timers exactly as they were.
        nav.couple(universe, ships_, active_, 0.0);
        for (std::size_t i = 0; i < ships_.size(); ++i)
        {
            Ship &ship = *ships_[i];
            ship.after_step(0.0);
            if (ship.on_rails() && ship.body().rails_parent != ship.telemetry().soi)
            {
                ship.go_dynamic();
                ship.after_step(0.0);
            }
            if (was_dynamic[i] && ship.on_rails())
                ship.go_dynamic();
        }
        update_target();
        nav.update(universe, ships_, active_);
        log("Time advanced " + std::to_string(static_cast<long long>(seconds)) + " s");
        return true;
    }

    void Sim::run_burn()
    {
        if (!props.get_bool("/autopilot/burn/armed"))
            return;
        Ship *me = active_ship();
        auto finish = [&](const char *state, const std::string &text) {
            props.set_bool("/autopilot/burn/armed", false);
            props.set_string("/autopilot/burn/state", state);
            props.set_double("/controls/throttle", 0.0);
            props.set_string("/controls/sas", "stability");
            log(text);
        };
        if (!me || me->crashed())
        {
            finish("abort", "Burn autopilot: no vessel");
            return;
        }
        const Telemetry &t = me->telemetry();
        // "escape" burns to a hyperbolic excess speed and carries on through escape; the default raises the apoapsis.
        const bool escape = props.get_string("/autopilot/burn/mode") == "escape";
        if ((!t.bound && !escape) || me->main_max_thrust() <= 0.0 || t.mass <= 0.0 || t.soi < 0)
        {
            finish("abort", "Burn autopilot stopped: " + std::string(me->main_max_thrust() > 0.0 && t.soi >= 0 ? "not in a bound orbit" : "no thrust available"));
            return;
        }
        const double mu = universe.bodies[static_cast<std::size_t>(t.soi)].gm;
        const double r = glm::length(me->body().position - universe.position(t.soi));
        const double target = props.get_double("/autopilot/burn/target-radius");
        const double apo = escape ? 0.0 : t.apoapsis + t.body_radius;
        double needed = std::sqrt(mu * (2.0 / r - 2.0 / (r + target)));
        if (escape)
        {
            const double vinf = props.get_double("/autopilot/burn/target-vinf");
            needed = std::sqrt(vinf * vinf + 2.0 * mu / r);
        }
        const double remaining = std::max(needed - t.orbital_speed, 0.0);
        const double accel = me->main_max_thrust() / t.mass;
        const double duration = remaining / accel;
        props.set_double("/autopilot/burn/remaining", remaining);
        props.set_double("/autopilot/burn/duration", duration);

        const std::string state = props.get_string("/autopilot/burn/state");
        const double ignition = props.get_double("/autopilot/burn/start") - 0.5 * duration - universe.time();
        props.set_double("/autopilot/burn/ignition-in", ignition);
        props.set_bool("/autopilot/enabled", false);
        props.set_string("/controls/sas", "prograde");

        if (state != "burn" && ignition > 0.0)
        {
            props.set_string("/autopilot/burn/state", "wait");
            props.set_double("/controls/throttle", 0.0);
            // Slow time warp down so the ignition is not overshot.
            int index = props.get_int("/sim/warp-index");
            while (index > 0 && (ignition < 30.0 || warp_factor(index) > ignition * 5.0))
                --index;
            props.set_int("/sim/warp-index", index);
            return;
        }
        if ((!escape && apo >= target) || remaining < 0.05)
        {
            finish("done", escape ? std::string("Burn complete: escape speed reached")
                                  : "Burn complete: apoapsis " + std::to_string(static_cast<int>(apo / 1000.0)) + " km");
            return;
        }
        props.set_string("/autopilot/burn/state", "burn");
        if (props.get_int("/sim/warp-index") > 0 && warp_factor(props.get_int("/sim/warp-index")) > thrust_warp)
            props.set_int("/sim/warp-index", 0);
        Controls attitude;
        attitude.sas = "prograde";
        bool valid = false;
        const sl::dvec3 prograde = me->sas_direction(attitude, valid);
        const sl::dvec3 nose = me->body().direction_to_world({0.0, 0.0, -1.0});
        const double alignment = valid ? glm::dot(nose, prograde) : -1.0;
        const double level = std::clamp(remaining / (2.0 * accel), 0.03, 1.0);
        props.set_double("/controls/throttle", alignment > 0.985 ? level : 0.0);
    }

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
