#include "app_state.h"

#include <filesystem>

// KosmosLink: native addon programs talk to the running game here (see src/link/ and the SDK's manual).

namespace kosmos::app
{
    namespace
    {
        constexpr const char *listen_setting = "/sim/settings/link-listen";

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

            void log(const std::string &addon, const std::string &text) override { app_.sim.log("[" + addon + "] " + text); }

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
        link_server->poll();
        link_server->flush();
        const Ship *active = sim.active_ship();
        if (active != link_ship)
        {
            link_ship = active;
            link_server->broadcast_event("ship", active ? active->spec().name : std::string());
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
} // namespace kosmos::app
