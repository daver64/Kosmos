#include "app_state.h"

#include "scenario_start.h"

#include <functional>
#include <algorithm>
#include <cmath>
#include <filesystem>

// KosmosLink: native addon programs talk to the running game here (see src/link/ and the SDK's manual).

namespace kosmos::app
{
    namespace
    {
        constexpr const char *listen_setting = "/sim/settings/link-listen";
        constexpr const char *timeout_setting = "/sim/settings/link-timeout";
        constexpr const char *claim_flown_setting = "/sim/settings/link-claim-flown";

        // What the game does for a connected program's requests that go beyond reading and writing properties.
        class AppLinkHost : public link::LinkHost
        {
        public:
            explicit AppLinkHost(App &app) : app_(app) {}

            std::string sim_version() override { return KOSMOS_VERSION; }

            std::string define_keys(const std::string &json) override
            {
                const std::string error = app_.keys.add_from_json(json, "native addon");
                if (error.empty())
                    app_.keys.load(); // applies a binding the player saved earlier
                return error;
            }

            std::string spawn_ship(const std::string &name, double x, double y, double z, std::uint32_t &handle) override
            {
                Sim &sim = app_.sim;
                const kosmos::ShipSpec *spec = sim.find_ship_spec(name);
                if (!spec)
                    return "no ship called '" + name + "'";
                const Ship *flown = sim.active_ship();
                if (!flown)
                    return "no ship is being flown";
                const sl::dvec3 offset = flown->body().direction_to_world(sl::dvec3(x, y, z));
                const sl::dvec3 position = flown->body().position + offset, velocity = flown->body().velocity;
                const sl::dquat orientation = flown->body().orientation;
                const int soi = sim.universe.soi_body(position);
                const sl::FlightBody &parent = *sim.universe.world.body(soi);
                Ship *ship = sim.spawn(*spec);
                if (!ship)
                    return "the ship could not be created";
                ship->place_relative(soi, position - parent.position, velocity - parent.velocity, orientation);
                ship->from_link = true;
                handle = static_cast<std::uint32_t>(sim.ship_count() - 1);
                return "";
            }

            std::string release_ordnance(int hardpoint) override
            {
                Ship *ship = app_.sim.active_ship();
                if (!ship)
                    return "no ship is being flown";
                return app_.arsenal.release(app_.arsenal_ctx, *ship, hardpoint) ? "" : "nothing to release";
            }

            std::string give_equipment(const std::string &id, int count) override
            {
                return app_.arsenal.give(app_.sim, id, count) ? "" : "no equipment called '" + id + "'";
            }

            std::string define_mfd(const std::string &addon, const std::string &id, const std::string &title) override
            {
                std::string directory;
                for (const kosmos::Addon &a : app_.sim.addons())
                    if (a.id == addon)
                        directory = a.directory;
                const std::string mode = addon + "." + id;
                if (app_.sim.mfd.mode_by_id(mode) >= 0)
                    return "";
                App &app = app_;
                app_.sim.mfd.register_native(addon, mode, title, directory, [&app, addon, id](MfdCanvas &g) {
                    link::Page *page = app.link_server ? app.link_server->page(addon, id) : nullptr;
                    if (!page || !app.link_server->page_live(addon))
                    {
                        g.set_colour(255, 120, 80);
                        g.text(mfd_size / 2.0f, mfd_size / 2.0f, "NO SIGNAL", 1);
                        return;
                    }
                    page->wanted = std::chrono::steady_clock::now();
                    for (const link::DrawCommand &d : page->commands)
                    {
                        if (d.kind > static_cast<std::uint8_t>(MfdCommand::Kind::image))
                            continue;
                        MfdCommand c;
                        c.kind = static_cast<MfdCommand::Kind>(d.kind);
                        c.a = d.a;
                        c.b = d.b;
                        c.c = d.c;
                        c.d = d.d;
                        c.e = d.e;
                        c.f = d.f;
                        c.colour = {d.r, d.g, d.bl, 255};
                        c.text = d.text;
                        c.align = d.align;
                        g.commands.push_back(std::move(c));
                    }
                });
                return "";
            }

            int body_named(const std::string &name) const
            {
                if (!name.empty())
                    return app_.sim.universe.find(name);
                const Ship *flown = app_.sim.active_ship();
                return flown ? app_.sim.universe.find(app_.sim.props.get_string("/ships/ship[" + std::to_string(app_.sim.active_index()) + "]/soi", "")) : -1;
            }

            std::string weather_at(const std::string &body_name, double lat, double lon, double alt, link::WeatherInfo &out, std::string &station, std::string &metar) override
            {
                const int body = body_named(body_name);
                if (body < 0)
                    return "unknown body";
                kosmos::Conditions c;
                if (!app_.sim.weather.at(app_.sim.universe, body, lat, lon, alt, c))
                    return "that body has no weather";
                out.wind_east = c.wind_east;
                out.wind_north = c.wind_north;
                out.wind_speed = std::hypot(c.wind_east, c.wind_north);
                out.wind_from = out.wind_speed > 0.01 ? std::fmod(std::atan2(-c.wind_east, -c.wind_north) * 180.0 / 3.14159265358979 + 360.0, 360.0) : 0.0;
                out.gust = c.gust;
                out.rain = c.rain;
                out.thunder = c.thunder;
                out.cloud_cover = c.cloud_cover;
                out.cloud_base = c.cloud_base;
                out.cloud_top = c.cloud_top;
                out.visibility = c.visibility;
                out.temperature = c.temperature;
                out.pressure = c.pressure;
                const Ship *flown = app_.sim.active_ship();
                if (flown && body == app_.sim.universe.find(app_.sim.props.get_string("/ships/ship[" + std::to_string(app_.sim.active_index()) + "]/soi", "")))
                {
                    station = app_.sim.props.get_string("/environment/station", "");
                    metar = app_.sim.props.get_string("/environment/metar", "");
                }
                return "";
            }

            std::string set_weather(const std::string &mode, const std::string &arg) override
            {
                sl::PropertyTree &props = app_.sim.props;
                if (mode == "metar")
                {
                    kosmos::Metar m;
                    if (!kosmos::decode_metar(arg, m))
                        return "that is not a METAR the game can read";
                    props.set_string("/sim/settings/weather-metar", arg);
                    props.set_string("/sim/settings/weather-preset", "custom");
                }
                else if (mode == "preset")
                {
                    const auto &presets = kosmos::Weather::presets();
                    if (std::none_of(presets.begin(), presets.end(), [&](const kosmos::WeatherPreset &p) { return arg == p.id; }))
                        return "no weather preset called '" + arg + "'";
                    props.set_string("/sim/settings/weather-preset", arg);
                }
                else
                    props.set_string("/sim/settings/weather-preset", "auto");
                props.set_string("/sim/weather-force", "");
                props.set_bool("/sim/settings/weather", true);
                return "";
            }

            std::string list_ships(double radius, bool buildings, std::vector<link::ShipInfo> &out) override
            {
                Sim &sim = app_.sim;
                const Ship *flown = sim.active_ship();
                for (int i = 0; i < sim.ship_count(); ++i)
                {
                    const Ship &s = *sim.ship(i);
                    if (!buildings && s.spec().building())
                        continue;
                    const double distance = flown ? glm::length(s.body().position - flown->body().position) : 0.0;
                    if (radius > 0.0 && distance > radius)
                        continue;
                    const std::string base = "/ships/ship[" + std::to_string(i) + "]/";
                    link::ShipInfo info;
                    info.index = static_cast<std::uint32_t>(i);
                    info.name = s.name;
                    info.body = sim.props.get_string(base + "soi", "");
                    info.latitude = sim.props.get_double(base + "latitude", 0.0);
                    info.longitude = sim.props.get_double(base + "longitude", 0.0);
                    info.altitude = sim.props.get_double(base + "altitude", 0.0);
                    info.heading = sim.props.get_double(base + "heading", 0.0);
                    info.speed = sim.props.get_double(base + "speed", 0.0);
                    info.vertical_speed = sim.props.get_double(base + "vertical-speed", 0.0);
                    info.distance = distance;
                    info.flags = static_cast<std::uint8_t>((i == sim.active_index() ? 1 : 0) | (sim.props.get_bool(base + "landed") ? 2 : 0) | (s.crashed() ? 4 : 0) | (s.from_link ? 8 : 0));
                    out.push_back(std::move(info));
                }
                return "";
            }

            std::string remove_ship(std::uint32_t index) override
            {
                Sim &sim = app_.sim;
                if (index >= static_cast<std::uint32_t>(sim.ship_count()))
                    return "no such ship";
                if (static_cast<int>(index) == sim.active_index())
                    return "the flown ship cannot be removed";
                if (!sim.ship(static_cast<int>(index))->from_link)
                    return "only ships a link addon created can be removed";
                return sim.remove_ship(static_cast<int>(index)) ? "" : "the ship could not be removed";
            }

            std::string place_ship(std::uint32_t index, const std::string &body_name, double lat, double lon, double alt, double heading, double speed) override
            {
                Sim &sim = app_.sim;
                if (index >= static_cast<std::uint32_t>(sim.ship_count()))
                    return "no such ship";
                const int body = body_named(body_name);
                if (body < 0)
                    return "unknown body";
                Ship &ship = *sim.ship(static_cast<int>(index));
                const sl::dvec3 dir = direction_from_lat_lon(sim.universe, body, lat, lon);
                const sl::Terrain *terrain = app_.terrain_of ? app_.terrain_of(body) : nullptr;
                const double ground = terrain ? terrain->height_at_direction(dir) : 0.0;
                if (alt > 0.0)
                    ship.place_airborne(body, dir, heading, ground, alt, speed);
                else
                    ship.place_on_surface(body, dir, heading, ground);
                return "";
            }

            std::string list_facilities(const std::string &kind, const std::string &body_name, double lat, double lon, double radius_km, std::size_t max, std::vector<link::Facility> &out) override
            {
                const Sim &sim = app_.sim;
                const int body = body_named(body_name);
                if (body < 0)
                    return "unknown body";
                const double radius = sim.universe.bodies[static_cast<std::size_t>(body)].radius;
                const double pi = 3.14159265358979, rad = pi / 180.0;
                const auto offset = [&](const kosmos::Base &b, double x, double z, double &la, double &lo) {
                    const double h = b.heading * rad;
                    const double east = z * std::sin(h) + x * std::cos(h), north = z * std::cos(h) - x * std::sin(h);
                    la = b.latitude + north / radius / rad;
                    lo = b.longitude + east / (radius * std::max(0.01, std::cos(b.latitude * rad))) / rad;
                };
                for (const kosmos::Base &b : sim.bases)
                {
                    if (sim.universe.find(b.body) != body)
                        continue;
                    if ((kind == "airport" && b.runways.empty()) || ((kind == "base" || kind == "pad") && !b.runways.empty()))
                        continue;
                    // Haversine distance over the body's surface.
                    const double dla = (b.latitude - lat) * rad, dlo = (b.longitude - lon) * rad;
                    const double a = std::sin(dla / 2) * std::sin(dla / 2) + std::cos(lat * rad) * std::cos(b.latitude * rad) * std::sin(dlo / 2) * std::sin(dlo / 2);
                    const double distance = 2.0 * radius * std::asin(std::min(1.0, std::sqrt(a)));
                    if (radius_km > 0.0 && distance > radius_km * 1000.0)
                        continue;
                    link::Facility f;
                    f.id = b.id;
                    f.name = b.name;
                    f.icao = b.icao;
                    f.body = b.body;
                    f.latitude = b.latitude;
                    f.longitude = b.longitude;
                    f.elevation = b.height;
                    f.heading = b.heading;
                    f.distance = distance;
                    for (const kosmos::Runway &r : b.runways)
                    {
                        link::FacilityRunway fr;
                        fr.name = r.name;
                        offset(b, r.x, r.z, fr.latitude, fr.longitude);
                        fr.heading = std::fmod(b.heading + r.heading + 720.0, 360.0);
                        fr.length = r.length;
                        fr.width = r.width;
                        for (const kosmos::RunwayIls &ils : r.ils)
                            fr.ils.push_back(ils.frequency);
                        f.runways.push_back(std::move(fr));
                    }
                    for (const kosmos::BaseNavaid &n : b.navaids)
                    {
                        link::FacilityNavaid fn;
                        fn.type = n.type;
                        fn.id = n.id;
                        fn.name = n.name;
                        offset(b, n.x, n.z, fn.latitude, fn.longitude);
                        fn.frequency = n.frequency;
                        fn.range = n.range;
                        f.navaids.push_back(std::move(fn));
                    }
                    out.push_back(std::move(f));
                }
                std::sort(out.begin(), out.end(), [](const link::Facility &a, const link::Facility &b) { return a.distance < b.distance; });
                if (out.size() > max)
                    out.resize(max);
                return "";
            }

            void log(const std::string &addon, const std::string &text) override { app_.sim.log("[" + addon + "] " + text); }

            void notify(const std::string &text, double seconds) override
            {
                (void)seconds; // notices share the HUD message list and its timing
                app_.sim.log(text);
            }

            std::string claim_ship(int owner, const std::string &addon, std::uint32_t index, bool passthrough) override
            {
                Sim &sim = app_.sim;
                if (index >= static_cast<std::uint32_t>(sim.ship_count()))
                    return "no such ship";
                if (static_cast<int>(index) == sim.active_index() && !sim.props.get_bool(claim_flown_setting))
                    return "the player has not allowed programs to take over the flown ship (View > Native addons)";
                const auto held = sim.link_claims.find(static_cast<int>(index));
                if (held != sim.link_claims.end() && held->second.owner != owner)
                    return "another program already owns that ship";
                Sim::LinkClaim &claim = sim.link_claims[static_cast<int>(index)];
                claim.owner = owner;
                claim.addon = addon;
                claim.passthrough = passthrough;
                return "";
            }

            std::string release_ship(int owner, std::uint32_t index) override
            {
                auto &claims = app_.sim.link_claims;
                const auto held = claims.find(static_cast<int>(index));
                if (held == claims.end() || held->second.owner != owner)
                    return "that ship is not owned by this program";
                claims.erase(held);
                return "";
            }

            void release_all(int owner) override
            {
                for (auto it = app_.sim.link_claims.begin(); it != app_.sim.link_claims.end();)
                    it = it->second.owner == owner ? app_.sim.link_claims.erase(it) : std::next(it);
            }

            Sim::LinkClaim *owned(int owner, std::uint32_t index)
            {
                const auto held = app_.sim.link_claims.find(static_cast<int>(index));
                return held != app_.sim.link_claims.end() && held->second.owner == owner ? &held->second : nullptr;
            }

            std::string ship_value(int owner, std::uint32_t index, const std::string &name, double value) override
            {
                const Sim::LinkClaim *claim = owned(owner, index);
                if (!claim)
                    return "that ship is not owned by this program";
                app_.sim.props.set_double("/ships/ship[" + std::to_string(index) + "]/module/" + claim->addon + "/" + name, value);
                return "";
            }

            std::string ship_force(int owner, std::uint32_t index, const double force[3], const double torque[3]) override
            {
                Sim::LinkClaim *claim = owned(owner, index);
                if (!claim)
                    return "that ship is not owned by this program";
                std::copy(force, force + 3, claim->force);
                std::copy(torque, torque + 3, claim->torque);
                return "";
            }

        private:
            App &app_;
        };
    } // namespace

    void App::setup_link()
    {
        link_host = std::make_unique<AppLinkHost>(*this);
        link_server = std::make_unique<link::LinkServer>(sim.props, *link_host, &native_addons);
        std::vector<link::AddonRef> refs;
        for (const kosmos::Addon &addon : sim.addons())
            refs.push_back({addon.id, addon.name, addon.directory});
        native_addons.scan(refs);
        const std::string keys_file = kosmos::keybindings_path();
        if (!keys_file.empty())
            native_addons.set_state_file((std::filesystem::path(keys_file).parent_path() / "native_addons.json").string());
        native_addons.load();
        link_window = options.link_window;
        if (options.link_listen)
            sim.props.set_bool(listen_setting, true);
    }

    void App::update_link()
    {
        if (!link_server)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now - link_tick > std::chrono::seconds(1))
        {
            link_tick = now;
            bool want = sim.props.get_bool(listen_setting);
            for (const link::NativeEntry &e : native_addons.entries())
                want = want || (e.allowed && !e.program.empty());
            if (want && !link_server->running() && !link_failed)
            {
                if (link_server->start(static_cast<std::uint16_t>(options.link_port)))
                {
                    native_addons.set_endpoint(link_server->port());
                    sim.log("KosmosLink listening on 127.0.0.1:" + std::to_string(link_server->port()));
                }
                else
                {
                    link_failed = true;
                    sim.log("KosmosLink: cannot listen on port " + std::to_string(options.link_port));
                }
            }
            else if (!want)
            {
                link_failed = false;
                if (link_server->running())
                    link_server->stop();
            }
            native_addons.update();
        }
        if (!link_server->running())
            return;
        link_server->set_timeout(sim.props.exists(timeout_setting) ? sim.props.get_double(timeout_setting) : 15.0);
        link_server->poll();
        link_server->flush();
        const Ship *active = sim.active_ship();
        if (active != link_ship)
        {
            link_ship = active;
            link_server->broadcast_event("ship", active ? active->spec().name : std::string());
        }

        // Opt-in events: each is worked out only while some client has asked for it.
        struct Watch
        {
            bool primed = false, paused = false, landed = false, crashed = false;
            int warp = 0, ships = 0;
            std::string soi, weather;
            const Ship *tracked = nullptr;
            std::chrono::steady_clock::time_point tick{};
        };
        static Watch w;
        link::LinkServer &srv = *link_server;
        auto &props = sim.props;
        if (!link_scenario.empty())
        {
            srv.broadcast_event("scenario", link_scenario);
            link_scenario.clear();
        }
        if (srv.wants_event("pause") || srv.wants_event("warp") || srv.wants_event("ships") || srv.wants_event("soi") || srv.wants_event("crash") || srv.wants_event("touchdown") || srv.wants_event("weather") || srv.wants_event("tick") || !w.primed)
        {
            const bool paused = props.get_bool("/sim/paused");
            const int warp = props.get_int("/sim/warp-index");
            const int count = sim.ship_count();
            const std::string soi = active ? props.get_string("/ships/ship[" + std::to_string(sim.active_index()) + "]/soi", "") : std::string();
            const bool landed = active && props.get_bool("/ships/ship[" + std::to_string(sim.active_index()) + "]/landed"), crashed = active && active->crashed();
            if (w.primed)
            {
                if (paused != w.paused)
                    srv.broadcast_event("pause", paused ? "1" : "0");
                if (warp != w.warp)
                    srv.broadcast_event("warp", std::to_string(props.get_double("/sim/warp")));
                if (count != w.ships)
                    srv.broadcast_event("ships", std::to_string(count));
                if (active == w.tracked)
                {
                    if (soi != w.soi)
                        srv.broadcast_event("soi", soi);
                    if (landed && !w.landed)
                        srv.broadcast_event("touchdown", active->name);
                    if (crashed && !w.crashed)
                        srv.broadcast_event("crash", active->name);
                }
            }
            w.primed = true;
            w.paused = paused;
            w.warp = warp;
            w.ships = count;
            w.soi = soi;
            w.landed = landed;
            w.crashed = crashed;
            w.tracked = active;
            if (srv.wants_event("weather"))
            {
                char key[160];
                std::snprintf(key, sizeof key, "%.0f %.0f %.1f %.1f %.0f %.0f", props.get_double("/environment/wind-speed"), props.get_double("/environment/wind-from"), std::round(props.get_double("/environment/rain") * 5.0) / 5.0, std::round(props.get_double("/environment/cloud-cover") * 5.0) / 5.0, std::round(props.get_double("/environment/visibility") / 1000.0), props.get_double("/environment/temperature"));
                if (key != w.weather)
                {
                    if (!w.weather.empty())
                        srv.broadcast_event("weather", props.get_string("/environment/metar", ""));
                    w.weather = key;
                }
            }
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - w.tick).count() >= 1.0)
            {
                w.tick = now;
                srv.broadcast_event("tick", std::to_string(sim.universe.julian_date()));
            }
        }
    }

    void App::shutdown_link()
    {
        if (link_server && link_server->running())
        {
            link_server->broadcast_event("shutdown", "");
            link_server->flush();
            link_server->stop();
        }
        native_addons.shutdown();
    }

    void App::draw_link_window()
    {
        if (!link_window)
            return;
        ImGui::SetNextWindowPos(ImVec2(380.0f, 50.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(620.0f, 420.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.94f);
        if (ImGui::Begin("Native addons", &link_window))
        {
            ImGui::TextWrapped("Native addons are separate programs that read and control the simulation through KosmosLink. They run only if you allow them here, and can do only what their addon declares.");
            bool listen = sim.props.get_bool(listen_setting);
            if (ImGui::Checkbox("Listen for programs started outside Kosmos", &listen))
            {
                sim.props.set_bool(listen_setting, listen);
                link_failed = false;
                kosmos::settings_save(sim.props);
            }
            if (link_server && link_server->running())
                ImGui::TextDisabled("Listening on 127.0.0.1:%d (this computer only)", static_cast<int>(link_server->port()));
            else
                ImGui::TextDisabled(link_failed ? "Cannot listen on the link port (is another program using it?)" : "Not listening");
            float timeout = sim.props.exists(timeout_setting) ? static_cast<float>(sim.props.get_double(timeout_setting)) : 15.0f;
            if (ImGui::SliderFloat("Drop silent programs after (s, 0 = never)", &timeout, 0.0f, 60.0f, "%.0f"))
            {
                sim.props.set_double(timeout_setting, timeout);
                kosmos::settings_save(sim.props);
            }
            bool claim_flown = sim.props.get_bool(claim_flown_setting);
            if (ImGui::Checkbox("Let programs with the vessel permission take over the ship I fly", &claim_flown))
            {
                sim.props.set_bool(claim_flown_setting, claim_flown);
                kosmos::settings_save(sim.props);
            }
            ImGui::Separator();
            std::vector<link::NativeEntry> &entries = native_addons.entries();
            if (entries.empty())
                ImGui::TextWrapped("No addon declares a native program, and none has connected. See the Kosmos SDK manual to write one.");
            const std::vector<link::ClientInfo> clients = link_server ? link_server->clients() : std::vector<link::ClientInfo>();
            for (link::NativeEntry &e : entries)
            {
                ImGui::PushID(e.id.c_str());
                bool allowed = e.allowed;
                if (ImGui::Checkbox(e.name.c_str(), &allowed))
                    native_addons.set_allowed(e.id, allowed);
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", e.id.c_str());
                const link::ClientInfo *connected = nullptr;
                for (const link::ClientInfo &c : clients)
                    if (c.addon == e.id)
                        connected = &c;
                std::string state = e.program.empty() ? "started outside Kosmos" : "program " + e.program;
                if (connected)
                    state += ", connected";
                else if (!e.status.empty())
                    state += ", " + e.status;
                if (connected)
                    state += ", last heard " + std::to_string(static_cast<int>(connected->idle)) + " s ago";
                ImGui::TextDisabled("    %s", state.c_str());
                ImGui::TextDisabled("    %s: %s", e.allowed ? "allowed" : "wants", link::capability_names(connected ? connected->requested : e.caps).c_str());
                if (!e.program.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Restart"))
                        native_addons.restart(e.id);
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

    // Windows the programs show: the server keeps their widgets, the game draws them and reports what the player did.
    void App::draw_link_ui()
    {
        if (!link_server || !link_server->running())
            return;
        for (const link::UiWindow &w : link_server->ui_windows())
        {
            bool open = true;
            ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.94f);
            const std::string title = w.title + "###link." + w.addon + "." + w.id;
            if (ImGui::Begin(title.c_str(), &open))
            {
                int n = 0;
                for (const link::UiWidget &wd : w.widgets)
                {
                    ImGui::PushID(n++);
                    switch (wd.kind)
                    {
                    case 0:
                        ImGui::TextWrapped("%s", wd.label.c_str());
                        break;
                    case 1:
                        if (ImGui::Button(wd.label.c_str()))
                            link_server->ui_event(w.addon, w.id, wd.id, "1");
                        break;
                    case 2:
                    {
                        bool v = wd.value != 0.0;
                        if (ImGui::Checkbox(wd.label.c_str(), &v))
                            link_server->ui_event(w.addon, w.id, wd.id, v ? "1" : "0");
                        break;
                    }
                    case 3:
                    {
                        float v = static_cast<float>(wd.value);
                        if (ImGui::SliderFloat(wd.label.c_str(), &v, static_cast<float>(wd.low), static_cast<float>(wd.high)))
                            link_server->ui_event(w.addon, w.id, wd.id, std::to_string(v));
                        break;
                    }
                    case 4:
                    {
                        char buffer[201] = {};
                        std::copy_n(wd.text.begin(), std::min<std::size_t>(wd.text.size(), 200), buffer);
                        if (ImGui::InputText(wd.label.c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                            link_server->ui_event(w.addon, w.id, wd.id, buffer);
                        break;
                    }
                    default:
                        ImGui::Separator();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::End();
            if (!open)
                link_server->ui_event(w.addon, w.id, "", "closed");
        }
    }

    // Lines, markers and labels the programs place in the world, projected with the same view as the orbit line.
    void App::draw_link_3d()
    {
        if (!link_server || !link_server->running() || map_mode || galaxy_mode)
            return;
        const std::vector<const link::Layer3 *> layers = link_server->layers();
        if (layers.empty() || !universe_ref)
            return;
        kosmos::Universe &universe = sim.universe;
        const sl::FlightBody *body_fb = universe.world.body(soi);
        if (!body_fb)
            return;
        const double radius = universe.bodies[static_cast<std::size_t>(soi)].radius;
        const sl::dquat orient = universe.orientation(soi);
        const sl::dvec3 body_rel = body_fb->position - eye_world;

        struct Point
        {
            sl::dvec3 rel, up;
            bool ok = true;
        };
        auto locate = [&](const link::Primitive3 &p, const double v[3]) {
            Point pt;
            if (p.frame == 0)
            {
                const sl::dvec3 dir = direction_from_lat_lon(universe, soi, v[0], v[1]);
                pt.rel = body_rel + dir * (radius + v[2]);
                pt.up = dir;
            }
            else if (p.frame == 1)
            {
                const sl::dvec3 local = orient * sl::dvec3(v[0], v[1], v[2]);
                pt.rel = body_rel + local;
                pt.up = glm::length(local) > 0.0 ? glm::normalize(local) : sl::dvec3(0, 1, 0);
            }
            else if (p.ship < static_cast<std::uint32_t>(sim.ship_count()))
            {
                const sl::FlightBody &fb = sim.ship(static_cast<int>(p.ship))->body();
                pt.rel = fb.position - eye_world + fb.direction_to_world(sl::dvec3(v[0], v[1], v[2]));
                pt.up = fb.direction_to_world(sl::dvec3(0, 1, 0));
            }
            else
                pt.ok = false;
            return pt;
        };
        // Ships hide things behind them as a bounding sphere sized from their stages: approximate, but cheap.
        struct Occluder
        {
            sl::dvec3 rel;
            double radius;
        };
        std::vector<Occluder> occluders;
        for (int i = 0; i < sim.ship_count(); ++i)
        {
            const kosmos::Ship *ship = sim.ship(i);
            if (!ship)
                continue;
            double r = 2.0;
            for (const auto &stage : ship->spec().stages)
                r = std::max({r, std::fabs(stage.z) + stage.length * 0.5, stage.radius});
            const sl::dvec3 rel = ship->body().position - eye_world;
            if (glm::length(rel) < 20.0e3)
                occluders.push_back({rel, std::min(r, 200.0)});
        }
        auto hidden = [&](const sl::dvec3 &rel) {
            const double length = glm::length(rel);
            if (length < 1.0)
                return false;
            const sl::dvec3 dir = rel / length;
            const double along = glm::dot(body_rel, dir);
            if (along > 0.0 && along < length && glm::length(body_rel - dir * along) < radius * 0.999)
                return true;
            for (const Occluder &o : occluders)
            {
                const double t = glm::dot(o.rel, dir);
                if (t > 0.0 && t < length - o.radius && glm::length(o.rel - dir * t) < o.radius && glm::length(rel - o.rel) > o.radius)
                    return true;
            }
            return false;
        };
        std::function<void(const sl::dvec3 &, const sl::dvec3 &, const link::Primitive3 &)> segment;
        auto draw_segment = [&](const sl::dvec3 &a, const sl::dvec3 &b, const link::Primitive3 &p) {
            if (p.depth && (hidden(a) || hidden(b)))
                return;
            glm::vec4 ca = view_proj * glm::vec4(glm::vec3(a), 1.0f), cb = view_proj * glm::vec4(glm::vec3(b), 1.0f);
            const float near_w = 0.05f;
            if (ca.w < near_w && cb.w < near_w)
                return;
            if (ca.w < near_w)
                ca += (cb - ca) * ((near_w - ca.w) / (cb.w - ca.w));
            else if (cb.w < near_w)
                cb += (ca - cb) * ((near_w - cb.w) / (ca.w - cb.w));
            auto sx = [&](const glm::vec4 &c) { return (c.x / c.w * 0.5f + 0.5f) * static_cast<float>(screen_w); };
            auto sy = [&](const glm::vec4 &c) { return (1.0f - (c.y / c.w * 0.5f + 0.5f)) * static_cast<float>(screen_h); };
            const float x1 = sx(ca), y1 = sy(ca), x2 = sx(cb), y2 = sy(cb);
            if (std::fabs(x1) > 1.0e5f || std::fabs(y1) > 1.0e5f || std::fabs(x2) > 1.0e5f || std::fabs(y2) > 1.0e5f)
                return;
            sl::line(sl::screen, x1, y1, x2, y2, {p.r, p.g, p.bl, p.alpha});
        };
        segment = [&](const sl::dvec3 &a, const sl::dvec3 &b, const link::Primitive3 &p) {
            const int pieces = p.depth && !occluders.empty() ? 16 : 1;
            for (int i = 0; i < pieces; ++i)
                draw_segment(a + (b - a) * (static_cast<double>(i) / pieces), a + (b - a) * (static_cast<double>(i + 1) / pieces), p);
        };
        auto ring = [&](const sl::dvec3 &centre, const sl::dvec3 &normal, double r, const link::Primitive3 &p) {
            const sl::dvec3 helper = std::fabs(normal.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
            const sl::dvec3 e1 = glm::normalize(glm::cross(normal, helper)), e2 = glm::cross(normal, e1);
            sl::dvec3 last = centre + e1 * r;
            for (int i = 1; i <= 40; ++i)
            {
                const double t = i * 6.283185307179586 / 40.0;
                const sl::dvec3 next = centre + (e1 * std::cos(t) + e2 * std::sin(t)) * r;
                segment(last, next, p);
                last = next;
            }
        };
        for (const link::Layer3 *layer : layers)
            for (const link::Primitive3 &p : layer->items)
            {
                const Point a = locate(p, p.a);
                if (!a.ok)
                    continue;
                switch (p.kind)
                {
                case 0:
                {
                    const Point b = locate(p, p.b);
                    if (b.ok)
                        segment(a.rel, b.rel, p);
                    break;
                }
                case 1:
                case 2:
                {
                    if (hidden(a.rel) && p.depth)
                        break;
                    float x = 0.0f, y = 0.0f;
                    if (glm::length(a.rel) < 1.0 || !project(view_proj, glm::vec3(a.rel), x, y) || x < 0 || x > screen_w || y < 0 || y > screen_h)
                        break;
                    const sl::Colour colour{p.r, p.g, p.bl, p.alpha};
                    if (p.kind == 1)
                        sl::circle(sl::screen, x, y, static_cast<float>(std::clamp(p.size, 2.0, 40.0)), colour, 1.0f);
                    else
                        sl::gprintf(static_cast<int>(x) + 8, static_cast<int>(y) - 6, colour, "%s", p.text.c_str());
                    break;
                }
                case 3:
                    ring(a.rel, a.up, p.size, p);
                    break;
                default:
                {
                    const sl::dvec3 helper = std::fabs(a.up.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
                    const sl::dvec3 e1 = glm::normalize(glm::cross(a.up, helper)), e2 = glm::cross(a.up, e1);
                    ring(a.rel, a.up, p.size, p);
                    ring(a.rel, e1, p.size, p);
                    ring(a.rel, e2, p.size, p);
                }
                }
            }
    }
} // namespace kosmos::app
