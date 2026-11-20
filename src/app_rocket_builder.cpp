#include "app_state.h"
#include "ship_internal.h"
#include "../sdk/kosmosmission/core/payload.h"

#include <set>

namespace kosmos::app
{
    namespace
    {
        const char *role_name(shipedit::RocketPartRole role)
        {
            switch (role)
            {
            case shipedit::RocketPartRole::first: return "First stage";
            case shipedit::RocketPartRole::second: return "Second stage";
            case shipedit::RocketPartRole::upper: return "Upper stage";
            case shipedit::RocketPartRole::booster: return "Boosters";
            case shipedit::RocketPartRole::fairing: return "Fairing";
            }
            return "Unknown";
        }

        bool usable_port(const PortSpec &port, int stages)
        {
            const double d = glm::length(port.direction), u = glm::length(port.up);
            return port.stage >= 0 && port.stage < stages &&
                   std::isfinite(d) && std::isfinite(u) && d > 1e-6 && u > 1e-6 &&
                   std::isfinite(port.position.x) && std::isfinite(port.position.y) && std::isfinite(port.position.z) &&
                   glm::length(glm::cross(port.direction, port.up)) > 1e-6;
        }

        bool port_used(const RocketBuilderState &builder, int carrier, int port)
        {
            if (carrier >= 0 && builder.payloads[static_cast<std::size_t>(carrier)].own_port == port)
                return true;
            return std::any_of(builder.payloads.begin(), builder.payloads.end(), [&](const RocketBuilderState::Payload &p) {
                return p.carrier == carrier && p.carrier_port == port;
            });
        }

        std::string propulsion_problem(const RocketBuilderState &builder)
        {
            if (!builder.assembly || !builder.preview_valid)
                return "Add a first stage before launching.";
            const auto stats = builder.assembly->document().stats();
            if (stats.size() != builder.preview.stages.size())
                return "The assembled stage data is inconsistent.";
            for (const auto &part : builder.assembly->parts())
            {
                if (part.role == shipedit::RocketPartRole::fairing)
                    continue;
                for (int index : part.stages)
                {
                    if (index < 0 || index >= static_cast<int>(stats.size()))
                        return "A rocket part has an invalid stage reference.";
                    const auto &stage = builder.preview.stages[static_cast<std::size_t>(index)];
                    const auto &numbers = stats[static_cast<std::size_t>(index)];
                    if (!std::isfinite(numbers.thrust) || numbers.thrust <= 0.0 ||
                        !std::isfinite(numbers.propellant) || numbers.propellant <= 0.0 ||
                        !std::isfinite(stage.fuel_start) || stage.fuel_start <= 0.0)
                        return "Each powered stage and booster needs fuel and a main engine.";
                }
            }
            return {};
        }

        bool offered_part(const shipedit::RocketAssembly &assembly, const shipedit::Document &source,
                          const shipedit::RocketPart &part, int host)
        {
            std::vector<shipedit::RocketPartRole> roles;
            for (const auto &existing : assembly.parts())
                roles.push_back(existing.role);
            roles.push_back(part.role);
            std::string error;
            if (!shipedit::valid_rocket_roles(roles, error))
                return false;
            using Role = shipedit::RocketPartRole;
            if (part.role != Role::booster && part.role != Role::fairing)
                return part.stages.size() == 1;
            if (host < 0 || host >= static_cast<int>(assembly.parts().size()))
                return false;
            const auto &selected = assembly.parts()[static_cast<std::size_t>(host)];
            if (selected.stages.size() != 1)
                return false;
            if (part.role == Role::booster)
                return part.stages.size() == 1 && selected.role == Role::first;
            if (part.stages.size() < 2 || selected.stages[0] <= 0 ||
                (selected.role != Role::second && selected.role != Role::upper) || part.stages.front() <= 0)
                return false;
            shipedit::json expanded;
            if (!source.expanded_root(expanded, error))
                return false;
            const auto &stages = expanded["stages"];
            for (std::size_t i = 0; i < part.stages.size(); ++i)
                if ((i && part.stages[i] != part.stages[i - 1] + 1) ||
                    stages[static_cast<std::size_t>(part.stages[i])].value("with_next", false) != (i + 1 < part.stages.size()))
                    return false;
            const bool source_coupled = stages[static_cast<std::size_t>(part.stages.front() - 1)].value("with_next", false);
            const bool target_coupled = assembly.document().root()["stages"][static_cast<std::size_t>(selected.stages[0] - 1)].value("with_next", false);
            return source_coupled || !target_coupled;
        }
    }

    bool App::update_rocket_preview(std::string &error)
    {
        if (!rocket_builder.assembly || rocket_builder.assembly->parts().empty())
        {
            vis.clear_parts();
            rocket_builder.preview_valid = false;
            return true;
        }
        shipedit::json data;
        ShipSpec preview;
        if (!rocket_builder.assembly->preview_json(data, error) ||
            !parse_ship_spec(data.dump(), (std::filesystem::path(sim.root()) / "preview.json").string(), preview, error))
            return false;
        vis.clear_parts();
        rocket_builder.preview = std::move(preview);
        rocket_builder.preview_valid = true;
        return true;
    }

    bool App::rocket_preview_pose(sl::dvec3 &position, sl::dquat &orientation) const
    {
        const auto it = std::find_if(sim.bases.begin(), sim.bases.end(), [&](const Base &b) {
            return b.id == rocket_builder.base_id && b.body == rocket_builder.body;
        });
        const int body = sim.universe.find(rocket_builder.body);
        if (it == sim.bases.end() || body < 0 || !rocket_builder.preview_valid)
            return false;
        const Body &planet = sim.universe.bodies[static_cast<std::size_t>(body)];
        const sl::FlightBody &pb = *sim.universe.world.body(body);
        sl::dvec3 dir;
        double heading;
        base_pad(*it, planet.radius, dir, heading);
        const sl::dvec3 up = glm::normalize(dir);
        const double lowest = ship_detail::rocket_lowest_point(rocket_builder.preview);
        position = pb.position + up * (planet.radius + it->height + base_pad_deck(*it) + lowest);
        orientation = ship_detail::rocket_pad_orientation(rocket_builder.preview, up, pb.spin_axis);
        return true;
    }

    void App::draw_rocket_preview()
    {
        sl::dvec3 position;
        sl::dquat orientation;
        if (!rocket_preview_pose(position, orientation))
            return;
        struct Pose { sl::dvec3 position; sl::dquat orientation; const ShipSpec *spec; };
        std::vector<Pose> poses{{position, orientation, &rocket_builder.preview}};
        const auto draw = [&](const Pose &pose) {
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(pose.position - eye_world)) *
                                    glm::mat4(glm::mat3_cast(glm::quat(pose.orientation)));
            draw_parts(vis, *pose.spec, 0, static_cast<int>(pose.spec->stages.size()) - 1, model);
        };
        draw(poses.front());
        for (const RocketBuilderState::Payload &payload : rocket_builder.payloads)
        {
            const ShipSpec *spec = sim.find_ship_spec(payload.ship);
            const std::size_t parent = static_cast<std::size_t>(payload.carrier + 1);
            if (!spec || parent >= poses.size() || payload.carrier_port < 0 || payload.own_port < 0 ||
                payload.carrier_port >= static_cast<int>(poses[parent].spec->ports.size()) ||
                payload.own_port >= static_cast<int>(spec->ports.size()))
                continue;
            const Pose &leader = poses[parent];
            const PortSpec &lp = leader.spec->ports[static_cast<std::size_t>(payload.carrier_port)];
            const PortSpec &fp = spec->ports[static_cast<std::size_t>(payload.own_port)];
            const sl::dvec3 dir = leader.orientation * lp.direction, up = leader.orientation * lp.up;
            const sl::dmat3 wanted(-glm::cross(dir, up), up, dir);
            const sl::dmat3 local(glm::cross(fp.direction, fp.up), fp.up, -fp.direction);
            const sl::dmat3 rotation = wanted * glm::transpose(local);
            const sl::dvec3 port = leader.position + leader.orientation * lp.position;
            Pose next{port - rotation * fp.position, glm::normalize(glm::quat_cast(rotation)), spec};
            poses.push_back(next);
            draw(poses.back());
        }
    }

    bool App::start_rocket_builder(const Base &selected, std::string &error)
    {
        RocketBuilderState &b = rocket_builder;
        error.clear();
        const int body = sim.universe.find(selected.body);
        if (body < 0 || !sim.universe.bodies[static_cast<std::size_t>(body)].solid_surface ||
            std::none_of(sim.bases.begin(), sim.bases.end(), [&](const Base &base) {
                return base.id == selected.id && base.body == selected.body;
            }))
            error = "That base is not available on a solid surface.";
        else if (!ship || !sim.find_ship_spec(ship->spec().name))
            error = "The current vessel is not available as a launchable ship.";
        else if (netplay.mode() != NetPlay::Mode::none || flight_replay.playing || part_tool.edit_paused)
            error = "Leave multiplayer, playback and external editing before starting the builder.";
        else if (std::string(b.name).find_first_not_of(" \t\r\n") == std::string::npos ||
                 sim.find_ship_spec(b.name) || std::string(b.id) != shipedit::slug(b.id) || !*b.id)
            error = "Choose a unique, nonempty rocket name and a valid lower-case addon ID.";
        if (!error.empty())
            return false;
        const Base base = selected;
        Scenario scenario;
        scenario.id = "rocket-builder";
        scenario.system = sim.galaxy.star(sim.system_index()).name;
        ScenarioShip entry;
        entry.ship = ship->spec().name;
        entry.body = base.body;
        entry.base = base.id;
        entry.landed = true;
        scenario.ships.push_back(entry);
        if (!menu_start_scenario(scenario))
        {
            error = "Could not move to that base; see the log.";
            return false;
        }
        b.base_id = base.id;
        b.body = base.body;
        b.assembly = std::make_unique<shipedit::RocketAssembly>(b.name);
        b.sources.clear();
        b.payloads.clear();
        b.fit_status.clear();
        b.saved = b.preview_valid = false;
        b.pan = sl::dvec3(0.0);
        for (const Addon &addon : sim.addons())
            for (const ShipSpec &spec : addon.ships)
            {
                shipedit::AddonInfo info{addon.id, addon.name, addon.directory, {}};
                RocketBuilderState::Source source;
                std::string problem;
                const std::filesystem::path file = std::filesystem::path(spec.source_file).lexically_relative(info.dir);
                if (!source.document.load(info, file.string(), problem) ||
                    !shipedit::rocket_parts(source.document, source.parts, problem))
                {
                    sim.log("Rocket catalog " + spec.name + ": " + problem);
                    continue;
                }
                if (!source.parts.empty())
                {
                    source.ship = spec.name;
                    b.sources.push_back(std::move(source));
                }
            }
        b.active = b.open = true;
        b.status = "Add a first stage. Right-drag to orbit, Shift-right-drag to pan, and use the zoom keys to zoom.";
        sim.props.set_bool("/sim/paused", true);
        eva.active = false;
        map_mode = galaxy_mode = cockpit_view = cockpit3d = false;
        rig.interior_cam = rig.base_view = rig.user_cam = -1;
        rig.cam_distance = 160.0;
        return true;
    }

    bool App::attach_rocket_payload(const std::string &name, int carrier, int carrier_port, int own_port, std::string &error)
    {
        RocketBuilderState &b = rocket_builder;
        error.clear();
        if (!b.active || !b.preview_valid || b.saved || carrier < -1 || carrier >= static_cast<int>(b.payloads.size()))
            error = "Select a valid carrier in an editable rocket.";
        else
        {
            const ShipSpec *leader = carrier < 0 ? &b.preview : sim.find_ship_spec(b.payloads[static_cast<std::size_t>(carrier)].ship);
            const ShipSpec *payload = sim.find_ship_spec(name);
            if (!leader || !payload || carrier_port < 0 || own_port < 0 ||
                carrier_port >= static_cast<int>(leader->ports.size()) || own_port >= static_cast<int>(payload->ports.size()))
                error = "Select existing carrier and payload ports.";
            else if (port_used(b, carrier, carrier_port))
                error = "That carrier port is already occupied.";
            else if (!usable_port(leader->ports[static_cast<std::size_t>(carrier_port)], static_cast<int>(leader->stages.size())) ||
                     !usable_port(payload->ports[static_cast<std::size_t>(own_port)], static_cast<int>(payload->stages.size())))
                error = "An attachment port has invalid geometry or no carrying stage.";
            else
            {
                b.payloads.push_back({name, carrier, carrier_port, own_port});
                update_rocket_payload_fit();
                return true;
            }
        }
        return false;
    }

    void App::update_rocket_payload_fit()
    {
        RocketBuilderState &b = rocket_builder;
        b.fit_status.clear();
        if (b.payloads.empty())
            return;
        shipedit::json data;
        std::string error;
        km::ShipSpec rocket;
        if (!b.assembly->ship_json(data, error) ||
            !km::parse_ship(data.dump(), "builder preview", rocket, error))
        {
            b.fit_status = "Payload fit unavailable: " + error;
            return;
        }
        for (std::size_t root = 0; root < b.payloads.size(); ++root)
        {
            if (b.payloads[root].carrier != -1)
                continue;
            std::vector<km::ShipSpec> stack;
            int current = static_cast<int>(root);
            bool valid = true;
            while (current >= 0)
            {
                const ShipSpec *spec = sim.find_ship_spec(b.payloads[static_cast<std::size_t>(current)].ship);
                km::ShipSpec member;
                if (!spec || !km::load_ship(spec->source_file, member, error))
                {
                    b.fit_status += "Fit unavailable for " + b.payloads[root].ship + ": " +
                                    (spec ? error : "ship not found") + "\n";
                    valid = false;
                    break;
                }
                const auto positive = [](double value) { return std::isfinite(value) && value > 0.0; };
                const km::PayloadSize &size = member.size;
                if (!size.known || !positive(size.length) ||
                    (size.round ? !positive(size.diameter) : !positive(size.width) || !positive(size.height)))
                {
                    b.fit_status += "Fit unavailable for " + b.payloads[root].ship + ": " +
                                    member.name + " has unknown or invalid cargo dimensions.\n";
                    valid = false;
                    break;
                }
                stack.push_back(std::move(member));
                int child = -1;
                for (std::size_t i = 0; i < b.payloads.size(); ++i)
                    if (b.payloads[i].carrier == current)
                    {
                        if (child >= 0)
                        {
                            valid = false;
                            b.fit_status += "Fit unavailable for branching payload " + b.payloads[root].ship + ".\n";
                            break;
                        }
                        child = static_cast<int>(i);
                    }
                if (!valid)
                    break;
                current = child;
            }
            if (!valid)
                continue;
            std::vector<km::PayloadBay> bays;
            const int port = b.payloads[root].carrier_port;
            if (port >= 0 && port < static_cast<int>(b.preview.ports.size()))
                for (const km::PayloadBay &bay : rocket.bays)
                    if (bay.port == b.preview.ports[static_cast<std::size_t>(port)].name &&
                        std::isfinite(bay.length) && bay.length > 0.0 &&
                        (bay.round ? std::isfinite(bay.radius) && bay.radius > 0.0 :
                         std::isfinite(bay.width) && bay.width > 0.0 && std::isfinite(bay.height) && bay.height > 0.0))
                        bays.push_back(bay);
            const km::FitReport fit = km::check_fit(bays, {km::make_payload(stack)});
            if (!fit.checked)
                b.fit_status += "Fit unavailable for " + b.payloads[root].ship + ": no usable bay or cargo size.\n";
            else if (!fit.ok)
                b.fit_status += "Estimated fit problem: " + fit.text() + "\n";
            else
                b.fit_status += "Estimated bay fit: " + fit.text() + "\n";
        }
        b.fit_status += "Fit is approximate and does not check the actual attachment orientation or collisions.";
    }

    bool App::launch_built_rocket(std::string &error)
    {
        RocketBuilderState &b = rocket_builder;
        error.clear();
        if (!b.active || !b.preview_valid || !b.assembly)
        {
            error = "Add a first stage before launching.";
            return false;
        }
        const int body = sim.universe.find(b.body);
        if (body < 0 || !sim.universe.bodies[static_cast<std::size_t>(body)].solid_surface ||
            std::none_of(sim.bases.begin(), sim.bases.end(), [&](const Base &base) {
                return base.id == b.base_id && base.body == b.body;
            }))
        {
            error = "The selected base is no longer available.";
            return false;
        }
        std::set<std::pair<int, int>> used_ports;
        for (std::size_t i = 0; i < b.payloads.size(); ++i)
        {
            const auto &p = b.payloads[i];
            const ShipSpec *payload = sim.find_ship_spec(p.ship);
            const ShipSpec *leader = p.carrier == -1 ? &b.preview :
                p.carrier >= 0 && p.carrier < static_cast<int>(i) ? sim.find_ship_spec(b.payloads[static_cast<std::size_t>(p.carrier)].ship) : nullptr;
            if (!leader || !payload || p.carrier_port < 0 || p.own_port < 0 ||
                p.carrier_port >= static_cast<int>(leader->ports.size()) || p.own_port >= static_cast<int>(payload->ports.size()) ||
                !usable_port(leader->ports[static_cast<std::size_t>(p.carrier_port)], static_cast<int>(leader->stages.size())) ||
                !usable_port(payload->ports[static_cast<std::size_t>(p.own_port)], static_cast<int>(payload->stages.size())) ||
                !used_ports.emplace(p.carrier, p.carrier_port).second ||
                !used_ports.emplace(static_cast<int>(i), p.own_port).second)
            {
                error = "A payload attachment is invalid or uses an occupied port.";
                return false;
            }
        }
        error = propulsion_problem(b);
        if (!error.empty())
        {
            return false;
        }
        if (!b.saved)
        {
            if (sim.find_ship_spec(b.name))
            {
                error = "A ship with this name already exists.";
                return false;
            }
            if (!sim.validate_built_rocket_id(b.id, error))
                return false;
            if (Sim::user_dir().empty())
            {
                error = "No user addon directory is available.";
                return false;
            }
            std::filesystem::path written;
            const auto validate = [](const std::filesystem::path &file, std::string &problem) {
                ShipSpec spec;
                if (!load_ship_spec(file.string(), spec, problem))
                    return false;
                if (spec.stages.empty())
                {
                    problem = "Generated rocket has no stages.";
                    return false;
                }
                return true;
            };
            if (!b.assembly->save_as(std::filesystem::path(Sim::user_dir()) / "addons", b.id, b.name,
                                     validate, written, error))
            {
                if (!written.empty())
                {
                    b.saved = true;
                    b.saved_directory = written.string();
                }
                return false;
            }
            b.saved = true;
            b.saved_directory = written.string();
        }
        const ShipSpec *registered = sim.find_ship_spec(b.name);
        if (registered)
        {
            std::error_code ec;
            if (!std::filesystem::equivalent(registered->directory, b.saved_directory, ec) || ec)
            {
                error = "A different ship now uses this rocket name.";
                return false;
            }
        }
        else if (!sim.register_built_rocket(b.saved_directory, error))
            return false;
        Scenario scenario;
        scenario.id = "built-rocket";
        scenario.name = b.name;
        scenario.system = sim.galaxy.star(sim.system_index()).name;
        ScenarioShip root;
        root.ship = b.name;
        root.body = b.body;
        root.base = b.base_id;
        root.landed = true;
        scenario.ships.push_back(root);
        for (const RocketBuilderState::Payload &payload : b.payloads)
        {
            ScenarioShip item = root;
            item.ship = payload.ship;
            item.dock_to = payload.carrier + 1;
            item.dock_port = payload.carrier_port;
            item.own_port = payload.own_port;
            scenario.ships.push_back(item);
        }
        if (!menu_start_scenario(scenario))
        {
            error = "The addon was saved but the scenario could not start; see the log.";
            return false;
        }
        b.active = b.open = b.preview_valid = false;
        b.assembly.reset();
        b.payloads.clear();
        sim.props.set_bool("/sim/paused", false);
        sim.log("Rocket addon saved and launched from " + b.base_id);
        return true;
    }

    bool App::setup_rocket_preview_test(std::string &error)
    {
        const auto base = std::find_if(sim.bases.begin(), sim.bases.end(), [](const Base &b) { return b.id == "kourou"; });
        if (base == sim.bases.end())
        {
            error = "Kourou is unavailable.";
            return false;
        }
        std::snprintf(rocket_builder.name, sizeof rocket_builder.name, "%s", "Builder preview test rocket");
        std::snprintf(rocket_builder.id, sizeof rocket_builder.id, "%s", "builder-preview-test");
        if (!start_rocket_builder(*base, error))
            return false;
        const auto source = std::find_if(rocket_builder.sources.begin(), rocket_builder.sources.end(), [](const RocketBuilderState::Source &s) {
            return s.ship == "CVELN1";
        });
        if (source == rocket_builder.sources.end())
        {
            error = "N1 catalog unavailable.";
            return false;
        }
        std::string warning;
        return rocket_builder.assembly->add(source->document, "block-a", -1, error, warning) &&
               rocket_builder.assembly->add(source->document, "block-b", -1, error, warning) &&
               rocket_builder.assembly->add(source->document, "block-v", -1, error, warning) &&
               update_rocket_preview(error) &&
               attach_rocket_payload("CVEL Fregat", -1, 0, 0, error) &&
               attach_rocket_payload("Luna E3", 0, 1, 0, error);
    }

    int App::test_rocket_builder()
    {
        int failures = 0;
        const auto check = [&](bool ok, const std::string &message) {
            std::printf("%s rocket builder: %s\n", ok ? "ok" : "FAIL", message.c_str());
            if (!ok)
                ++failures;
        };
        const auto base = std::find_if(sim.bases.begin(), sim.bases.end(), [](const Base &b) { return b.id == "kourou"; });
        if (base == sim.bases.end())
        {
            std::fprintf(stderr, "rocket builder selftest: Kourou is unavailable\n");
            return 1;
        }
        std::snprintf(rocket_builder.name, sizeof rocket_builder.name, "%s", "Builder integration test rocket");
        std::snprintf(rocket_builder.id, sizeof rocket_builder.id, "%s", "builder-integration-test");
        std::string problem, warning;
        if (!start_rocket_builder(*base, problem))
        {
            std::fprintf(stderr, "rocket builder selftest: %s\n", problem.c_str());
            return 1;
        }
        check(rocket_builder.active && sim.props.get_bool("/sim/paused"), "enter base and pause");
        const auto source = std::find_if(rocket_builder.sources.begin(), rocket_builder.sources.end(), [](const RocketBuilderState::Source &s) {
            return s.ship == "CVELN1";
        });
        if (source == rocket_builder.sources.end())
        {
            std::fprintf(stderr, "rocket builder selftest: N1 catalog unavailable\n");
            return 1;
        }
        const bool built = rocket_builder.assembly->add(source->document, "block-a", -1, problem, warning) &&
                           rocket_builder.assembly->add(source->document, "block-b", -1, problem, warning) &&
                           rocket_builder.assembly->add(source->document, "block-v", -1, problem, warning) &&
                           update_rocket_preview(problem);
        check(built, "assemble and parse N1 preview: " + problem);
        if (!built)
            return 1;
        sl::dvec3 position;
        sl::dquat orientation;
        check(rocket_preview_pose(position, orientation), "locate preview at selected pad");
        check(attach_rocket_payload("CVEL Fregat", -1, 0, 0, problem), "attach Fregat: " + problem);
        check(!attach_rocket_payload("Luna E3", -1, 0, 0, problem) && !problem.empty(), "reject occupied port");
        check(attach_rocket_payload("Luna E3", 0, 1, 0, problem), "attach nested probe: " + problem);
        check(!attach_rocket_payload("Luna E3", 0, 0, 0, problem) && !problem.empty(), "reject payload's occupied own port");
        check(rocket_builder.fit_status.find("Estimated") != std::string::npos, "estimate nested cargo fit");
        const std::string chosen_base = rocket_builder.base_id;
        rocket_builder.base_id = "missing-builder-test-base";
        check(!launch_built_rocket(problem) && !problem.empty() && rocket_builder.active && ship,
              "missing base leaves the editor and flight intact");
        rocket_builder.base_id = chosen_base;
        std::snprintf(rocket_builder.id, sizeof rocket_builder.id, "%s", "cvel-n1");
        check(!launch_built_rocket(problem) && !problem.empty() && !rocket_builder.saved && rocket_builder.active,
              "duplicate addon ID is rejected without saving");
        const std::string disabled_id = sim.props.get_string("/selftest/rocket-builder-disabled-id");
        if (!disabled_id.empty())
        {
            std::snprintf(rocket_builder.id, sizeof rocket_builder.id, "%s", disabled_id.c_str());
            check(!launch_built_rocket(problem) && problem.find("disabled") != std::string::npos &&
                      !rocket_builder.saved && rocket_builder.active,
                  "disabled addon ID is rejected before saving");
            check(!sim.validate_built_rocket_id("cvel-atlas", problem) &&
                      problem.find("already exists") != std::string::npos,
                  "disabled installed addon ID remains reserved");
        }
        std::snprintf(rocket_builder.id, sizeof rocket_builder.id, "%s", "../invalid");
        check(!launch_built_rocket(problem) && !problem.empty() && !rocket_builder.saved && rocket_builder.active,
              "invalid addon ID is rejected before saving");
        std::snprintf(rocket_builder.id, sizeof rocket_builder.id, "%s", "builder-integration-test");
        std::snprintf(rocket_builder.name, sizeof rocket_builder.name, "%s", "CVELN1");
        check(!launch_built_rocket(problem) && !problem.empty() && !rocket_builder.saved && rocket_builder.active,
              "duplicate ship name is rejected without saving");
        std::snprintf(rocket_builder.name, sizeof rocket_builder.name, "%s", "Builder integration test rocket");
        const double upper_fuel = rocket_builder.preview.stages.back().fuel_start;
        rocket_builder.preview.stages.back().fuel_start = 0.0;
        check(!propulsion_problem(rocket_builder).empty() && !launch_built_rocket(problem) &&
                  !problem.empty() && rocket_builder.active && !rocket_builder.saved,
              "empty powered upper stage blocks launch without discarding the editor");
        rocket_builder.preview.stages.back().fuel_start = upper_fuel;
        rocket_builder.payloads[0].carrier_port = -1;
        check(!launch_built_rocket(problem) && rocket_builder.active && sim.props.get_bool("/sim/paused") &&
                  !rocket_builder.saved && ship,
              "invalid attachment leaves the unsaved editor and flight intact");
        rocket_builder.payloads[0].carrier_port = 0;
        if (!launch_built_rocket(problem))
        {
            std::fprintf(stderr, "rocket builder selftest: launch: %s\n", problem.c_str());
            return 1;
        }
        check(ship && ship->spec().name == "Builder integration test rocket" && !rocket_builder.active,
              "save, register and start generated rocket");
        const ShipSpec *first_registered = sim.find_ship_spec("Builder integration test rocket");
        const std::string first_directory = rocket_builder.saved_directory;
        check(first_registered && !sim.register_built_rocket(first_directory, problem) && !problem.empty() &&
                  sim.find_ship_spec("Builder integration test rocket") == first_registered,
              "duplicate registration is rejected without disturbing the existing ship");
        check(!sim.register_built_rocket(sim.root(), problem) && !problem.empty() &&
                  sim.find_ship_spec("Builder integration test rocket") == first_registered,
              "registration rejects paths outside the user addon directory");
        check(sim.nav.dock_links().size() == 2, "launch retains both payload links");
        check(glm::length(ship->origin() - position) < 1.0, "launch uses preview pad position");
        check(std::fabs(glm::dot(ship->body().orientation, orientation)) > 0.99999, "launch uses preview orientation");
        sim.props.set_bool("/autopilot/enabled", true);
        sim.step(1.0 / 60.0);
        check(sim.props.get_bool("/autopilot/enabled") && sim.props.get_double("/controls/throttle") > 0.9,
              "shared ascent autopilot engages on the pad");
        sim.props.set_bool("/autopilot/enabled", false);
        sim.props.set_double("/controls/throttle", 1.0);
        for (int i = 0; i < 480; ++i)
            sim.step(1.0 / 60.0);
        check(glm::length(ship->origin() - position) > 5.0, "generated rocket lifts off under throttle");
        check(sim.nav.dock_links().size() == 2, "payloads remain docked during ascent");
        sim.props.set_double("/controls/throttle", 0.0);
        for (int i = 0; i < 2; ++i)
        {
            sim.props.set_bool("/controls/stage", true);
            sim.step(1.0 / 60.0);
        }
        check(ship->active_stage() == 2 && sim.nav.dock_links().size() == 2, "staging preserves payloads on the upper stage");
        sim.props.set_bool("/controls/stage", true);
        sim.step(1.0 / 60.0);
        check(sim.active_ship() && sim.active_ship()->spec().name == "CVEL Fregat" && sim.nav.dock_links().size() == 1,
              "final staging releases Fregat and keeps its probe docked");
        std::snprintf(rocket_builder.name, sizeof rocket_builder.name, "%s", "Builder piggyback test rocket");
        std::snprintf(rocket_builder.id, sizeof rocket_builder.id, "%s", "builder-piggyback-test");
        if (!start_rocket_builder(*base, problem))
        {
            std::fprintf(stderr, "rocket builder selftest: piggyback setup: %s\n", problem.c_str());
            return 1;
        }
        const auto energia = std::find_if(rocket_builder.sources.begin(), rocket_builder.sources.end(), [](const RocketBuilderState::Source &s) {
            return s.ship == "CVEL Energia";
        });
        const bool piggyback = energia != rocket_builder.sources.end() &&
                               rocket_builder.assembly->add(energia->document, "core", -1, problem, warning) &&
                               rocket_builder.assembly->add(energia->document, "boosters", 0, problem, warning) &&
                               update_rocket_preview(problem) &&
                               attach_rocket_payload("CVEL Buran", -1, 0, 1, problem);
        check(piggyback, "assemble Energia with independent Buran piggyback");
        if (!piggyback || !launch_built_rocket(problem))
        {
            std::fprintf(stderr, "rocket builder selftest: piggyback launch: %s\n", problem.c_str());
            return 1;
        }
        check(sim.find_ship_spec("Builder integration test rocket") == first_registered,
              "incremental registration preserves existing ship-spec pointers");
        check(sim.nav.dock_links().size() == 1, "piggyback launches attached");
        sim.props.set_bool("/controls/stage", true);
        sim.step(1.0 / 60.0);
        sim.props.set_bool("/controls/stage", true);
        sim.step(1.0 / 60.0);
        Ship *orbiter = sim.active_ship();
        check(orbiter && orbiter->spec().name == "CVEL Buran" && sim.nav.dock_links().empty(),
              "piggyback separates and receives control");
        if (orbiter && orbiter->spec().name == "CVEL Buran" && !orbiter->spec().animation_controls.empty())
        {
            sim.props.set_double("/controls/anim/bay-doors", 1.0);
            sim.step(1.0);
            check(orbiter->animation_position(0) > 0.0, "separated piggyback retains its animation controls");
        }
        else
            check(false, "separated piggyback retains its animation controls");
        std::printf("rocket builder selftest: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }

    void App::draw_rocket_builder_payloads()
    {
        RocketBuilderState &b = rocket_builder;
        ImGui::Separator();
        ImGui::TextUnformatted("Separate payload vessels");
        std::vector<const ShipSpec *> payload_ships;
        for (const Addon &addon : sim.addons())
            for (const ShipSpec &spec : addon.ships)
                if (!spec.ports.empty() && sim.find_ship_spec(spec.name) == &spec)
                    payload_ships.push_back(&spec);
        if (!b.preview_valid || b.preview.ports.empty())
            ImGui::TextWrapped("Add a stage with a catalogued payload port before attaching a vessel.");
        else if (payload_ships.empty())
            ImGui::TextWrapped("No payload ships with attachment ports are available.");
        else
        {
            b.payload_ship = std::clamp(b.payload_ship, 0, static_cast<int>(payload_ships.size()) - 1);
            b.payload_carrier = std::clamp(b.payload_carrier, 0, static_cast<int>(b.payloads.size()));
            const int carrier = b.payload_carrier - 1;
            const ShipSpec *carrier_spec = carrier < 0 ? &b.preview : sim.find_ship_spec(b.payloads[static_cast<std::size_t>(carrier)].ship);
            if (ImGui::BeginCombo("Payload ship", payload_ships[static_cast<std::size_t>(b.payload_ship)]->name.c_str()))
            {
                for (int i = 0; i < static_cast<int>(payload_ships.size()); ++i)
                    if (ImGui::Selectable(payload_ships[static_cast<std::size_t>(i)]->name.c_str(), i == b.payload_ship))
                        b.payload_ship = i, b.payload_own_port = 0;
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("Carrier", carrier < 0 ? b.name : b.payloads[static_cast<std::size_t>(carrier)].ship.c_str()))
            {
                if (ImGui::Selectable(b.name, b.payload_carrier == 0))
                    b.payload_carrier = 0, b.payload_carrier_port = 0;
                for (int i = 0; i < static_cast<int>(b.payloads.size()); ++i)
                    if (ImGui::Selectable((b.payloads[static_cast<std::size_t>(i)].ship + "##carrier" + std::to_string(i)).c_str(), b.payload_carrier == i + 1))
                        b.payload_carrier = i + 1, b.payload_carrier_port = 0;
                ImGui::EndCombo();
            }
            const ShipSpec *payload = payload_ships[static_cast<std::size_t>(b.payload_ship)];
            if (carrier_spec && !carrier_spec->ports.empty() && !payload->ports.empty())
            {
                b.payload_carrier_port = std::clamp(b.payload_carrier_port, 0, static_cast<int>(carrier_spec->ports.size()) - 1);
                b.payload_own_port = std::clamp(b.payload_own_port, 0, static_cast<int>(payload->ports.size()) - 1);
                if (ImGui::BeginCombo("Carrier port", carrier_spec->ports[static_cast<std::size_t>(b.payload_carrier_port)].name.c_str()))
                {
                    for (int i = 0; i < static_cast<int>(carrier_spec->ports.size()); ++i)
                        if (ImGui::Selectable((carrier_spec->ports[static_cast<std::size_t>(i)].name + "##carrierport" + std::to_string(i)).c_str(), i == b.payload_carrier_port))
                            b.payload_carrier_port = i;
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("Payload port", payload->ports[static_cast<std::size_t>(b.payload_own_port)].name.c_str()))
                {
                    for (int i = 0; i < static_cast<int>(payload->ports.size()); ++i)
                        if (ImGui::Selectable((payload->ports[static_cast<std::size_t>(i)].name + "##payloadport" + std::to_string(i)).c_str(), i == b.payload_own_port))
                            b.payload_own_port = i;
                    ImGui::EndCombo();
                }
                ImGui::BeginDisabled(b.saved);
                if (ImGui::Button("Attach payload"))
                {
                    std::string problem;
                    b.status = attach_rocket_payload(payload->name, carrier, b.payload_carrier_port, b.payload_own_port, problem)
                                   ? "Payload attached. Its fit and collision clearance have not been verified." : problem;
                }
                ImGui::EndDisabled();
            }
            else
                ImGui::TextWrapped("The selected carrier or payload has no attachment ports.");
        }
        for (std::size_t i = 0; i < b.payloads.size(); ++i)
            ImGui::BulletText("%s (carrier %d)", b.payloads[i].ship.c_str(), b.payloads[i].carrier + 1);
        ImGui::BeginDisabled(b.saved || b.payloads.empty());
        if (ImGui::Button("Remove last payload"))
        {
            b.payloads.pop_back();
            update_rocket_payload_fit();
            b.status = "Payload removed.";
        }
        ImGui::EndDisabled();
        if (!b.payloads.empty())
        {
            ImGui::TextWrapped("Payloads are separate vessels; their engines do not power the rocket.");
            ImGui::TextWrapped("%s", b.fit_status.c_str());
        }
    }

    void App::draw_rocket_builder()
    {
        RocketBuilderState &b = rocket_builder;
        if (!b.open)
            return;
        ImGui::SetNextWindowSize(ImVec2(480, 650), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Rocket builder", &b.open))
        {
            ImGui::End();
            if (!b.open && b.active)
            {
                vis.clear_parts();
                b.active = b.preview_valid = false;
                b.assembly.reset();
                b.payloads.clear();
                sim.props.set_bool("/sim/paused", false);
            }
            return;
        }
        if (!b.active)
        {
            ImGui::TextWrapped("Starting the builder leaves the current flight. Save it first if you want to return to it.");
            std::vector<const Base *> bases;
            for (const Base &base : sim.bases)
            {
                const int body = sim.universe.find(base.body);
                if (body >= 0 && sim.universe.bodies[static_cast<std::size_t>(body)].solid_surface)
                    bases.push_back(&base);
            }
            if (bases.empty())
                ImGui::TextWrapped("There are no bases on solid surfaces in this system.");
            else
            {
                b.base = std::clamp(b.base, 0, static_cast<int>(bases.size()) - 1);
                if (ImGui::BeginCombo("Base", (bases[b.base]->name + " (" + bases[b.base]->body + ")").c_str()))
                {
                    for (int i = 0; i < static_cast<int>(bases.size()); ++i)
                        if (ImGui::Selectable((bases[i]->name + "##" + bases[i]->id).c_str(), i == b.base))
                            b.base = i;
                    ImGui::EndCombo();
                }
                ImGui::InputText("Rocket name", b.name, sizeof b.name);
                ImGui::InputText("Addon ID", b.id, sizeof b.id);
                if (ImGui::Button("Leave flight and build"))
                {
                    std::string problem;
                    if (!start_rocket_builder(*bases[static_cast<std::size_t>(b.base)], problem))
                        b.status = problem;
                }
            }
        }
        else
        {
            ImGui::Text("%s at %s", b.name, b.base_id.c_str());
            ImGui::BeginDisabled(b.saved);
            ImGui::InputText("Addon ID", b.id, sizeof b.id);
            ImGui::EndDisabled();
            ImGui::TextWrapped("Simulation paused. Stages are ordered bottom to top; boosters and fairings attach to a selected stage.");
            if (b.sources.empty())
                ImGui::TextWrapped("No classified rocket parts are available.");
            else
            {
                b.source = std::clamp(b.source, 0, static_cast<int>(b.sources.size()) - 1);
                if (ImGui::BeginCombo("Source", b.sources[static_cast<std::size_t>(b.source)].ship.c_str()))
                {
                    for (int i = 0; i < static_cast<int>(b.sources.size()); ++i)
                        if (ImGui::Selectable(b.sources[static_cast<std::size_t>(i)].ship.c_str(), i == b.source))
                            b.source = i, b.part = 0;
                    ImGui::EndCombo();
                }
                RocketBuilderState::Source &source = b.sources[static_cast<std::size_t>(b.source)];
                const auto &parts = b.assembly->parts();
                if (!parts.empty())
                {
                    b.host = std::clamp(b.host, 0, static_cast<int>(parts.size()) - 1);
                    if (ImGui::BeginCombo("Attachment host", parts[static_cast<std::size_t>(b.host)].name.c_str()))
                    {
                        for (int i = 0; i < static_cast<int>(parts.size()); ++i)
                            if (ImGui::Selectable((parts[static_cast<std::size_t>(i)].name + "##host" + std::to_string(i)).c_str(), i == b.host))
                                b.host = i;
                        ImGui::EndCombo();
                    }
                }
                std::vector<int> offered;
                for (int i = 0; i < static_cast<int>(source.parts.size()); ++i)
                    if (offered_part(*b.assembly, source.document, source.parts[static_cast<std::size_t>(i)], b.host))
                        offered.push_back(i);
                if (!offered.empty())
                {
                    if (std::find(offered.begin(), offered.end(), b.part) == offered.end())
                        b.part = offered.front();
                    if (ImGui::BeginCombo("Part", source.parts[static_cast<std::size_t>(b.part)].name.c_str()))
                    {
                        for (int i : offered)
                        {
                            const auto &part = source.parts[static_cast<std::size_t>(i)];
                            if (ImGui::Selectable((part.name + " (" + role_name(part.role) + ")").c_str(), i == b.part))
                                b.part = i;
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                    ImGui::TextWrapped("No parts from this source are suitable at this point or on the selected host.");
                ImGui::BeginDisabled(b.saved || !b.payloads.empty() || offered.empty());
                if (ImGui::Button("Add part"))
                {
                    std::string problem, warning;
                    if (!b.assembly->add(source.document, source.parts[static_cast<std::size_t>(b.part)].id, b.host, problem, warning))
                        b.status = problem;
                    else if (!update_rocket_preview(problem))
                    {
                        b.assembly->undo();
                        b.status = problem;
                    }
                    else
                        b.status = warning.empty() ? "Part added." : warning;
                }
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Assembly");
            for (int i = 0; i < static_cast<int>(b.assembly->parts().size()); ++i)
            {
                const auto &part = b.assembly->parts()[static_cast<std::size_t>(i)];
                if (ImGui::Selectable((part.name + "##part" + std::to_string(i)).c_str(), b.selection == i))
                    b.selection = i;
            }
            ImGui::BeginDisabled(b.saved || !b.payloads.empty());
            ImGui::InputFloat("Move Z (m)", &b.move_distance);
            if (ImGui::Button("Move selected"))
            {
                std::string problem;
                if (!b.assembly->move(b.selection, b.move_distance, problem))
                    b.status = problem;
                else if (!update_rocket_preview(problem))
                    b.assembly->undo(), b.status = problem;
                else
                    b.status = "Part and its attached groups moved.";
            }
            if (ImGui::Button("Remove selected"))
            {
                std::string problem, warning;
                if (!b.assembly->remove(b.selection, problem, warning))
                    b.status = problem;
                else if (!update_rocket_preview(problem))
                    b.assembly->undo(), b.status = problem;
                else
                    b.status = warning.empty() ? "Part removed." : warning;
            }
            ImGui::SameLine();
            if (ImGui::Button("Undo") && b.assembly->undo())
            {
                std::string problem;
                if (!update_rocket_preview(problem))
                    b.assembly->redo(), b.status = problem;
            }
            ImGui::SameLine();
            if (ImGui::Button("Redo") && b.assembly->redo())
            {
                std::string problem;
                if (!update_rocket_preview(problem))
                    b.assembly->undo(), b.status = problem;
            }
            ImGui::EndDisabled();
            if (b.preview_valid)
            {
                for (const std::string &warning : b.assembly->document().validate())
                    ImGui::TextWrapped("Warning: %s", warning.c_str());
                const int body = sim.universe.find(b.body);
                if (body >= 0)
                {
                    const Body &planet = sim.universe.bodies[static_cast<std::size_t>(body)];
                    double mass = b.preview.rcs_propellant, thrust = 0.0;
                    bool initially_ignited = true;
                    const auto stats = b.assembly->document().stats();
                    for (std::size_t i = 0; i < b.preview.stages.size(); ++i)
                    {
                        const StageSpec &stage = b.preview.stages[i];
                        mass += stage.dry_mass + stage.propellant * stage.fuel_start;
                        if (i > 0)
                            initially_ignited = initially_ignited && stage.ignite_with_previous;
                        if (initially_ignited && stage.propellant > 0.0 && stage.fuel_start > 0.0)
                            thrust += stats[i].thrust;
                    }
                    for (const auto &payload : b.payloads)
                        if (const ShipSpec *spec = sim.find_ship_spec(payload.ship))
                        {
                            mass += spec->rcs_propellant;
                            for (const StageSpec &stage : spec->stages)
                                mass += stage.dry_mass + stage.propellant * stage.fuel_start;
                        }
                    const double weight = mass * planet.gm / (planet.radius * planet.radius);
                    if (std::isfinite(weight) && weight > 0.0 && std::isfinite(thrust))
                    {
                        ImGui::Text("Approximate initial thrust/weight: %.2f", thrust / weight);
                        if (thrust <= weight)
                            ImGui::TextWrapped("Warning: the rocket may not lift off with this payload.");
                        ImGui::TextWrapped("Estimate uses nominal thrust and stage masses; it excludes atmospheric losses and other equipment.");
                    }
                }
            }
            if (!b.payloads.empty())
                ImGui::TextWrapped("Remove payloads before changing the rocket.");
            draw_rocket_builder_payloads();
            ImGui::Separator();
            const std::string readiness = propulsion_problem(b);
            if (!readiness.empty())
                ImGui::TextWrapped("%s", readiness.c_str());
            ImGui::BeginDisabled(!readiness.empty());
            if (ImGui::Button(b.saved ? "Retry launch" : "Save addon and launch"))
            {
                std::string problem;
                if (!launch_built_rocket(problem))
                    b.status = problem;
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("Different stage combinations may not lift off or reach orbit. Set the ascent profile in Autopilot > Spacecraft after launch.");
            if (ImGui::Button("Cancel builder"))
                b.open = false;
        }
        if (!b.status.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", b.status.c_str());
        }
        ImGui::End();
        if (!b.open && b.active)
        {
            vis.clear_parts();
            b.active = b.preview_valid = false;
            b.assembly.reset();
            b.payloads.clear();
            sim.props.set_bool("/sim/paused", false);
        }
    }
}
