#include "menu.h"

#include <imgui.h>
#include "ui_slider.h"
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace kosmos
{
    namespace
    {
        namespace fs = std::filesystem;
        constexpr double pi = 3.14159265358979323846;

        bool dbl(const char *label, double &v, float speed = 1.0f, double lo = 0.0, double hi = 0.0, const char *format = "%.3f")
        {
            const bool bounded = lo != hi;
            return ImGui::DragScalar(label, ImGuiDataType_Double, &v, speed, bounded ? &lo : nullptr, bounded ? &hi : nullptr, format);
        }

        bool text(const char *label, std::string &s)
        {
            char buffer[256];
            std::snprintf(buffer, sizeof buffer, "%s", s.c_str());
            if (!ImGui::InputText(label, buffer, sizeof buffer))
                return false;
            s = buffer;
            return true;
        }

        bool combo(const char *label, int &index, const std::vector<std::string> &items)
        {
            const char *current = index >= 0 && index < static_cast<int>(items.size()) ? items[static_cast<std::size_t>(index)].c_str() : "(none)";
            bool changed = false;
            if (ImGui::BeginCombo(label, current))
            {
                for (int i = 0; i < static_cast<int>(items.size()); ++i)
                    if (ImGui::Selectable(items[static_cast<std::size_t>(i)].c_str(), i == index))
                    {
                        index = i;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            return changed;
        }

        int index_of(const std::vector<std::string> &items, const std::string &value)
        {
            const auto it = std::find(items.begin(), items.end(), value);
            return it == items.end() ? -1 : static_cast<int>(it - items.begin());
        }

        std::string slug(const std::string &name)
        {
            std::string out;
            for (char c : name)
                out += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
            return out.empty() ? "untitled" : out;
        }

        std::string user_folder(const char *sub)
        {
            const std::string user = Sim::user_dir();
            return user.empty() ? std::string(".") : (fs::path(user) / sub).string();
        }

        std::vector<std::string> body_names(const Sim &sim)
        {
            std::vector<std::string> names;
            for (const Body &b : sim.universe.bodies)
                names.push_back(b.name);
            return names;
        }

        std::vector<std::string> ship_names(const Sim &sim)
        {
            std::vector<std::string> names;
            for (const Addon &a : sim.addons())
                for (const ShipSpec &s : a.ships)
                    names.push_back(s.name);
            return names;
        }

        // Lists with add / duplicate / delete / reorder buttons. Returns true when the list changed.
        template <typename T, typename Name>
        bool list_box(const char *id, std::vector<T> &items, int &selected, Name name, const T &fresh)
        {
            bool changed = false;
            selected = std::clamp(selected, 0, std::max(0, static_cast<int>(items.size()) - 1));
            ImGui::PushID(id);
            if (ImGui::BeginListBox("##list", ImVec2(-FLT_MIN, 6.0f * ImGui::GetTextLineHeightWithSpacing())))
            {
                for (int i = 0; i < static_cast<int>(items.size()); ++i)
                {
                    ImGui::PushID(i);
                    if (ImGui::Selectable(name(items[static_cast<std::size_t>(i)]).c_str(), i == selected))
                        selected = i;
                    ImGui::PopID();
                }
                ImGui::EndListBox();
            }
            const bool any = !items.empty();
            if (ImGui::Button("Add"))
            {
                items.insert(items.begin() + (any ? selected + 1 : 0), fresh);
                selected = any ? selected + 1 : 0;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate") && any)
            {
                T copy = items[static_cast<std::size_t>(selected)];
                items.insert(items.begin() + selected + 1, copy);
                ++selected;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && any)
            {
                items.erase(items.begin() + selected);
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("up", ImGuiDir_Up) && any && selected > 0)
            {
                std::swap(items[static_cast<std::size_t>(selected)], items[static_cast<std::size_t>(selected - 1)]);
                --selected;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("down", ImGuiDir_Down) && any && selected + 1 < static_cast<int>(items.size()))
            {
                std::swap(items[static_cast<std::size_t>(selected)], items[static_cast<std::size_t>(selected + 1)]);
                ++selected;
                changed = true;
            }
            ImGui::PopID();
            return changed;
        }

        // Records an undo point once no widget is being dragged or typed into, so one drag is one step.
        void commit(const std::string &now, std::string &committed, std::vector<std::string> &undo, std::vector<std::string> &redo)
        {
            if (now == committed || ImGui::IsAnyItemActive())
                return;
            if (!committed.empty())
            {
                undo.push_back(committed);
                if (undo.size() > 100)
                    undo.erase(undo.begin());
            }
            redo.clear();
            committed = now;
        }
    } // namespace

    // ---------------------------------------------------------------- capture

    Scenario capture_scenario(Sim &sim, const std::string &name)
    {
        Scenario sc;
        sc.name = name;
        sc.id = slug(name);
        sc.epoch_jd = sim.universe.julian_date();
        sc.life_support = life_support_name(sim.life_support);
        if (sim.system_index() != 0)
            sc.system = sim.galaxy.star(sim.system_index()).name;
        // Base buildings are respawned from the base data, so they are not saved. `index` maps a sim ship to its scenario entry.
        std::vector<int> index(static_cast<std::size_t>(sim.ship_count()), -1);
        int kept = 0;
        for (int si = 0; si < sim.ship_count(); ++si)
            if (!sim.ship(si)->spec().building())
                index[static_cast<std::size_t>(si)] = kept++;
        const int flown = sim.props.get_int("/sim/active-ship");
        sc.active = flown >= 0 && flown < sim.ship_count() && index[static_cast<std::size_t>(flown)] >= 0 ? index[static_cast<std::size_t>(flown)] : 0;
        sc.sets.push_back("/controls/gear=" + std::to_string(sim.props.get_double("/controls/gear")));
        if (sim.props.get_string("/controls/sas") == "earth-communications")
            sc.sets.push_back("/controls/sas=earth-communications");
        if (flown >= 0 && flown < sim.ship_count())
            for (const auto &animation : sim.ship(flown)->spec().animation_controls)
            {
                const std::string path = "/controls/anim/" + animation.property;
                if (sim.props.exists(path))
                    sc.sets.push_back(path + "=" + std::to_string(sim.props.get_double(path)));
            }
        sc.target = sim.props.get_string("/sim/target");
        for (int i = 0; i < NavSystem::receivers; ++i)
            sc.nav.push_back(sim.props.get_double("/nav/nav[" + std::to_string(i) + "]/frequency"));
        for (int si = 0; si < sim.ship_count(); ++si)
        {
            if (index[static_cast<std::size_t>(si)] < 0)
                continue;
            Ship &ship = *sim.ship(si);
            const Telemetry &t = ship.telemetry();
            ScenarioShip s;
            s.ship = ship.spec().name;
            if (ship.name != ship.spec().name)
                s.label = ship.name;
            s.skin = ship.skin;
            const int soi = std::clamp(t.soi, 0, static_cast<int>(sim.universe.bodies.size()) - 1);
            s.body = sim.universe.bodies[static_cast<std::size_t>(soi)].name;
            s.carry_state = std::make_shared<ShipCarryState>(ship.carry_state());
            if (t.landed)
            {
                s.landed = true;
                s.latitude = t.latitude;
                s.longitude = t.longitude;
                s.heading = t.heading;
            }
            else
            {
                const sl::FlightBody &parent = *sim.universe.world.body(soi);
                const sl::dvec3 r = ship.body().position - parent.position;
                const sl::dvec3 v = ship.body().velocity - parent.velocity;
                const sl::dvec3 h = glm::cross(r, v);
                s.altitude_km = (glm::length(r) - sim.universe.bodies[static_cast<std::size_t>(soi)].radius) / 1000.0;
                s.inclination = std::atan2(h.z, h.y) * 180.0 / pi;
                const double inc = s.inclination * pi / 180.0;
                const sl::dvec3 prograde0(0.0, std::sin(inc), -std::cos(inc));
                s.phase = std::atan2(glm::dot(r, prograde0), r.x) * 180.0 / pi;
            }
            const sl::FlightBody &parent = *sim.universe.world.body(soi);
            const sl::dvec3 r = ship.body().position - parent.position;
            const sl::dvec3 v = ship.body().velocity - parent.velocity;
            const sl::dquat &q = ship.body().orientation;
            const sl::dvec3 &omega = ship.body().angular_velocity;
            s.has_state = true;
            s.state_position = {r.x, r.y, r.z};
            s.state_velocity = {v.x, v.y, v.z};
            s.state_orientation = {q.w, q.x, q.y, q.z};
            s.state_angular_velocity = {omega.x, omega.y, omega.z};
            sc.ships.push_back(std::move(s));
        }
        // Docks are stored on the later ship of each pair, pointing at the earlier one.
        for (const std::array<int, 4> &l : sim.nav.dock_links())
        {
            if (l[0] >= sim.ship_count() || l[2] >= sim.ship_count() || index[static_cast<std::size_t>(l[0])] < 0 || index[static_cast<std::size_t>(l[2])] < 0)
                continue;
            const int ia = index[static_cast<std::size_t>(l[0])], ib = index[static_cast<std::size_t>(l[2])];
            const bool swap = ia > ib;
            const int first = swap ? ib : ia, second = swap ? ia : ib;
            ScenarioShip &follower = sc.ships[static_cast<std::size_t>(second)];
            if (follower.dock_to >= 0 || first == second)
                continue;
            follower.dock_to = first;
            follower.dock_port = swap ? l[3] : l[1];
            follower.own_port = swap ? l[1] : l[3];
        }
        return sc;
    }

    // ------------------------------------------------------------ file dialog

    void FileDialog::show(Mode mode, const std::string &title, const std::string &folder, const std::string &name, std::function<void(const std::string &)> accept)
    {
        active_ = true;
        mode_ = mode;
        title_ = title;
        folder_ = folder;
        name_ = name;
        error_.clear();
        accept_ = std::move(accept);
        std::error_code ec;
        if (mode == Mode::save)
            fs::create_directories(folder_, ec);
        if (!fs::is_directory(folder_, ec))
            folder_ = fs::current_path().string();
    }

    void FileDialog::draw()
    {
        if (!active_)
            return;
        ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        bool open = true;
        if (ImGui::Begin((title_ + "###filedialog").c_str(), &open))
        {
            text("Folder", folder_);
            std::vector<fs::path> dirs, files;
            std::error_code ec;
            for (const fs::directory_entry &e : fs::directory_iterator(folder_, fs::directory_options::skip_permission_denied, ec))
            {
                if (e.is_directory(ec))
                    dirs.push_back(e.path());
                else if (e.path().extension() == ".json")
                    files.push_back(e.path());
            }
            std::sort(dirs.begin(), dirs.end());
            std::sort(files.begin(), files.end());
            if (ImGui::BeginListBox("##files", ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y - 70.0f)))
            {
                if (ImGui::Selectable("[..]"))
                    folder_ = fs::path(folder_).parent_path().string();
                for (const fs::path &d : dirs)
                    if (ImGui::Selectable(("[" + d.filename().string() + "]").c_str()))
                        folder_ = d.string();
                for (const fs::path &f : files)
                {
                    const bool picked = f.filename().string() == name_;
                    if (ImGui::Selectable(f.filename().string().c_str(), picked, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        name_ = f.filename().string();
                        if (ImGui::IsMouseDoubleClicked(0) && mode_ == Mode::open)
                        {
                            active_ = false;
                            accept_((fs::path(folder_) / name_).string());
                            break;
                        }
                    }
                }
                ImGui::EndListBox();
            }
            if (active_)
            {
                text("File", name_);
                const bool ok = !name_.empty();
                if (!ok)
                    ImGui::BeginDisabled();
                if (ImGui::Button(mode_ == Mode::open ? "Open" : "Save"))
                {
                    std::string file = name_;
                    if (fs::path(file).extension() != ".json")
                        file += ".json";
                    const fs::path path = fs::path(folder_) / file;
                    if (mode_ == Mode::open && !fs::exists(path))
                        error_ = "No such file";
                    else
                    {
                        active_ = false;
                        accept_(path.string());
                    }
                }
                if (!ok)
                    ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    active_ = false;
                if (!error_.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error_.c_str());
                }
            }
        }
        ImGui::End();
        if (!open)
            active_ = false;
    }

    // -------------------------------------------------------- scenario editor

    void ScenarioEditor::set(const Scenario &scenario)
    {
        scenario_ = scenario;
        has_ = true;
        selected_ = 0;
        undo_.clear();
        redo_.clear();
        committed_ = scenario_to_json(scenario_);
    }

    bool ScenarioEditor::undo()
    {
        if (undo_.empty())
            return false;
        Scenario previous;
        std::string error;
        if (!parse_scenario(undo_.back(), scenario_.id, previous, error))
            return false;
        redo_.push_back(committed_);
        committed_ = undo_.back();
        undo_.pop_back();
        previous.id = scenario_.id;
        previous.path = scenario_.path;
        scenario_ = previous;
        return true;
    }

    bool ScenarioEditor::redo()
    {
        if (redo_.empty())
            return false;
        Scenario next;
        std::string error;
        if (!parse_scenario(redo_.back(), scenario_.id, next, error))
            return false;
        undo_.push_back(committed_);
        committed_ = redo_.back();
        redo_.pop_back();
        next.id = scenario_.id;
        next.path = scenario_.path;
        scenario_ = next;
        return true;
    }

    void ScenarioEditor::draw_toolbar(Sim &sim, FileDialog &dialog, const MenuActions &actions)
    {
        if (ImGui::Button("From flight"))
        {
            set(capture_scenario(sim, scenario_.name.empty() ? "My scenario" : scenario_.name));
            status_ = "Captured the ships as they are now.";
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Replace this scenario with the current flight state of every ship.");
        ImGui::SameLine();
        if (ImGui::Button("New"))
        {
            Scenario fresh;
            fresh.name = "New scenario";
            fresh.id = "new_scenario";
            ScenarioShip s;
            const std::vector<std::string> names = ship_names(sim);
            s.ship = names.empty() ? "" : names.front();
            fresh.ships.push_back(s);
            set(fresh);
            status_.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load..."))
            dialog.show(FileDialog::Mode::open, "Load scenario", user_folder("scenarios"), "", [this](const std::string &path) {
                Scenario loaded;
                std::string error;
                if (load_scenario(path, loaded, error))
                {
                    set(loaded);
                    status_ = "Loaded " + path;
                }
                else
                    status_ = error;
            });
        ImGui::SameLine();
        if (ImGui::Button("Save..."))
            dialog.show(FileDialog::Mode::save, "Save scenario", user_folder("scenarios"), scenario_.id + ".json", [this, &sim](const std::string &path) {
                std::string error;
                if (save_scenario(path, scenario_, error))
                {
                    scenario_.path = path;
                    status_ = "Saved " + path;
                    sim.load_user_content();
                }
                else
                    status_ = error;
            });
        ImGui::SameLine();
        if (ImGui::Button("Launch") && actions.start_scenario)
        {
            actions.start_scenario(scenario_);
            status_ = "Launched.";
        }
        ImGui::BeginDisabled(undo_.empty());
        if (ImGui::Button("Undo") )
            undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(redo_.empty());
        if (ImGui::Button("Redo"))
            redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status_.c_str());
        ImGui::Separator();
    }

    void ScenarioEditor::draw_scenario_fields()
    {
        text("Name", scenario_.name);
        text("File id", scenario_.id);
        char desc[1024];
        std::snprintf(desc, sizeof desc, "%s", scenario_.description.c_str());
        if (ImGui::InputTextMultiline("Description", desc, sizeof desc, ImVec2(-FLT_MIN, 52.0f)))
            scenario_.description = desc;
        ui::slider_int("Flown ship", &scenario_.active, 0, std::max(0, static_cast<int>(scenario_.ships.size()) - 1));
        text("Target", scenario_.target);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A ship or body name for the Target MFD; empty for none");
        {
            const std::vector<std::string> modes = {"(program default)", "off", "warn", "fatal"};
            int index = scenario_.life_support == "off" ? 1 : scenario_.life_support == "warn" ? 2 : scenario_.life_support == "fatal" ? 3 : 0;
            if (combo("Life support", index, modes))
                scenario_.life_support = index == 0 ? "" : modes[static_cast<std::size_t>(index)];
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("What failing life support does to a crew: nothing, alarms only, or the crew dies");
        }
        for (std::size_t i = 0; i < 2; ++i)
        {
            if (scenario_.nav.size() <= i)
                scenario_.nav.resize(2, 0.0);
            dbl(i == 0 ? "NAV1 MHz" : "NAV2 MHz", scenario_.nav[i], 0.05f, 0.0, 0.0, "%.2f");
        }
        while (!scenario_.nav.empty() && scenario_.nav.back() == 0.0)
            scenario_.nav.pop_back();

    }

    void ScenarioEditor::draw_ship_fields(Sim &sim, ScenarioShip &s, const std::vector<std::string> &ships, const std::vector<std::string> &bodies)
    {
        const ScenarioShip previous = s;
        int ship_index = index_of(ships, s.ship);
        if (combo("Ship", ship_index, ships) && ship_index >= 0)
            s.ship = ships[static_cast<std::size_t>(ship_index)];
        text("Label", s.label);
        if (const ShipSpec *spec = sim.find_ship_spec(s.ship))
        {
            std::vector<std::string> skins = ship_skins(*spec);
            if (!skins.empty())
            {
                skins.insert(skins.begin(), "(default)");
                int skin_index = std::max(0, index_of(skins, s.skin));
                if (combo("Skin", skin_index, skins))
                    s.skin = skin_index == 0 ? "" : skins[static_cast<std::size_t>(skin_index)];
            }
        }
        dbl("Cabin leak cm2", s.leak_cm2, 0.1f, 0.0, 1000.0, "%.1f");
        if (s.battery < 0.0)
            s.battery = -1.0;
        {
            bool custom = s.battery >= 0.0;
            if (ImGui::Checkbox("Set battery charge", &custom))
                s.battery = custom ? 1.0 : -1.0;
            if (custom)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                dbl("##battery", s.battery, 0.01f, 0.0, 1.0, "%.2f");
            }
        }
        int body_index = index_of(bodies, s.body);
        if (combo("Body", body_index, bodies) && body_index >= 0)
            s.body = bodies[static_cast<std::size_t>(body_index)];
        int mode = s.landed ? 1 : 0;
        ImGui::RadioButton("Orbit", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Surface", &mode, 1);
        s.landed = mode == 1;
        if (!s.landed)
        {
            dbl("Altitude km", s.altitude_km, 1.0f, 0.0, 400000.0, "%.1f");
            dbl("Inclination", s.inclination, 0.5f, -180.0, 180.0, "%.1f deg");
            dbl("Phase", s.phase, 0.5f, -360.0, 360.0, "%.1f deg");
            dbl("Apoapsis km (0 = circular)", s.apoapsis_km, 100.0f, 0.0, 100000000.0, "%.0f");
            text("Aim at body", s.toward);
            if (s.dock_to < 0 && s.portal_m <= 0.0)
                text("Point nose at body", s.point_at);
        }
        else
        {
            if (!previous.landed)
                s.point_at.clear();
            int where = !s.base.empty() ? 2 : s.pad ? 1 : 0;
            ImGui::RadioButton("Lat/long", &where, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Launch pad", &where, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Base", &where, 2);
            if (where == 2 && s.base.empty() && !sim.bases.empty())
                s.base = sim.bases.front().id;
            if (where != 2)
                s.base.clear();
            s.pad = where == 1;
            if (where == 0)
            {
                dbl("Latitude", s.latitude, 0.1f, -90.0, 90.0, "%.4f");
                dbl("Longitude", s.longitude, 0.1f, -180.0, 180.0, "%.4f");
                dbl("Heading", s.heading, 1.0f, 0.0, 360.0, "%.0f deg");
            }
            else if (where == 2)
            {
                std::vector<std::string> ids;
                for (const Base &b : sim.bases)
                    ids.push_back(b.id);
                int bi = index_of(ids, s.base);
                if (combo("Base id", bi, ids) && bi >= 0)
                    s.base = ids[static_cast<std::size_t>(bi)];
                const Base *base = bi >= 0 ? &sim.bases[static_cast<std::size_t>(bi)] : nullptr;
                std::vector<std::string> places = {"Pad"};
                if (base)
                    for (const Runway &r : base->runways)
                        places.push_back(r.name);
                int place = s.runway + 1;
                if (combo("Start on", place, places))
                    s.runway = place - 1;
                if (s.runway >= 0)
                {
                    ImGui::Checkbox("Reverse direction", &s.reverse);
                    dbl("Past threshold m", s.along, 1.0f, -2000.0, 5000.0, "%.0f");
                }
            }
            dbl("Altitude m", s.altitude, 1.0f, 0.0, 100000.0, "%.0f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Above zero starts the ship airborne at this height");
            if (s.altitude > 0.0)
                dbl("Airspeed m/s", s.speed, 1.0f, 0.0, 3000.0, "%.0f");
        }
        if (s.ship != previous.ship)
            s.carry_state.reset();
        if (s.ship != previous.ship || s.body != previous.body || s.landed != previous.landed ||
            s.altitude_km != previous.altitude_km || s.inclination != previous.inclination ||
            s.phase != previous.phase || s.apoapsis_km != previous.apoapsis_km || s.toward != previous.toward || s.point_at != previous.point_at ||
            s.pad != previous.pad || s.base != previous.base || s.runway != previous.runway ||
            s.reverse != previous.reverse || s.along != previous.along || s.altitude != previous.altitude ||
            s.speed != previous.speed || s.latitude != previous.latitude || s.longitude != previous.longitude ||
            s.heading != previous.heading)
            s.has_state = false;
    }

    void ScenarioEditor::draw(bool &open, Sim &sim, FileDialog &dialog, const MenuActions &actions)
    {
        if (!open)
            return;
        if (!has_)
            set(capture_scenario(sim, "My scenario"));
        ImGui::SetNextWindowPos(ImVec2(60.0f, 80.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(470.0f, 640.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.92f);
        if (ImGui::Begin("Scenario editor", &open))
        {
        draw_toolbar(sim, dialog, actions);

        draw_scenario_fields();
            ImGui::SeparatorText("Ships");
            const std::vector<std::string> ships = ship_names(sim), bodies = body_names(sim);
            ScenarioShip fresh;
            fresh.ship = ships.empty() ? "" : ships.front();
            list_box("ships", scenario_.ships, selected_, [](const ScenarioShip &s) { return s.ship + (s.label.empty() ? "" : " \"" + s.label + "\""); }, fresh);
            if (scenario_.ships.empty())
                scenario_.ships.push_back(fresh);
            selected_ = std::clamp(selected_, 0, static_cast<int>(scenario_.ships.size()) - 1);
            scenario_.active = std::clamp(scenario_.active, 0, static_cast<int>(scenario_.ships.size()) - 1);
            ScenarioShip &s = scenario_.ships[static_cast<std::size_t>(selected_)];
            ImGui::Separator();
        draw_ship_fields(sim, s, ships, bodies);
        }
        ImGui::End();
        commit(scenario_to_json(scenario_), committed_, undo_, redo_);
    }

    // ------------------------------------------------------------ base editor

    void BaseEditor::restore(Sim &sim, const std::string &text_json)
    {
        std::vector<Base> parsed;
        std::string error;
        if (base_ < 0 || base_ >= static_cast<int>(sim.bases.size()) || !parse_bases(text_json, "undo", parsed, error) || parsed.empty())
            return;
        Base &current = sim.bases[static_cast<std::size_t>(base_)];
        parsed.front().direction = current.direction;
        parsed.front().height = current.height;
        current = std::move(parsed.front());
    }

    bool BaseEditor::undo(Sim &sim)
    {
        if (undo_.empty() || base_ != committed_base_)
            return false;
        redo_.push_back(committed_);
        committed_ = undo_.back();
        undo_.pop_back();
        restore(sim, committed_);
        return true;
    }

    bool BaseEditor::redo(Sim &sim)
    {
        if (redo_.empty() || base_ != committed_base_)
            return false;
        undo_.push_back(committed_);
        committed_ = redo_.back();
        redo_.pop_back();
        restore(sim, committed_);
        return true;
    }

    void BaseEditor::tick_blender(Sim &sim)
    {
        if (blender_paused_ && !blender_.active())
            end_blender(sim);
    }

    void BaseEditor::end_blender(Sim &sim)
    {
        blender_paused_ = false;
        sim.props.set_bool("/sim/paused", blender_was_paused_);
        sim.props.set_bool("/sim/blender-edit", false);
        blender_status_ = blender_.name() + " closed; simulation resumed";
    }

    // Bakes the chosen objects into one GLB (one scene object each, so Blender can join them) and swaps them for a model object.
    bool BaseEditor::begin_blender(Sim &sim, Base &base, const std::vector<int> &indices)
    {
        if (sim.props.get_bool("/sim/blender-edit"))
        {
            blender_status_ = "Another mesh edit is already running";
            return false;
        }
        std::string error;
        fs::path file;
        std::vector<BaseObject> saved = base.objects;
        if (indices.size() == 1 && base.objects[static_cast<std::size_t>(indices[0])].type == "model")
        {
            const BaseObject &o = base.objects[static_cast<std::size_t>(indices[0])];
            const std::string found = resolve_base_model(base, o.model);
            if (found.empty())
            {
                blender_status_ = "Model file not found: " + o.model;
                return false;
            }
            file = found;
            if (is_stock_model(found))
            {
                // Shipped models are never edited in place: work on a copy in the user's models folder.
                const fs::path copy = fs::path(user_folder("bases")) / "models" / fs::path(found).filename();
                std::error_code copy_ec;
                fs::create_directories(copy.parent_path(), copy_ec);
                if (!fs::exists(copy, copy_ec))
                    fs::copy_file(found, copy, copy_ec);
                if (copy_ec)
                {
                    blender_status_ = "Cannot copy the stock model: " + copy_ec.message();
                    return false;
                }
                file = copy;
                base.objects[static_cast<std::size_t>(indices[0])].model = (fs::path("models") / copy.filename()).generic_string();
            }
        }
        else
        {
            std::vector<GlbNode> nodes;
            std::string slug = base.id;
            for (int i : indices)
            {
                const BaseObject &o = base.objects[static_cast<std::size_t>(i)];
                GlbNode node;
                node.name = std::to_string(i) + "_" + o.type;
                if (!bake_base_object(o, node.mesh))
                {
                    blender_status_ = "Cannot convert a '" + o.type + "' object (only built-in props can be baked)";
                    return false;
                }
                nodes.push_back(std::move(node));
            }
            slug += indices.size() == 1 ? "_obj" + std::to_string(indices[0]) + "_" + nodes[0].name.substr(nodes[0].name.find('_') + 1) : "_group" + std::to_string(indices[0]);
            for (char &c : slug)
                if (!std::isalnum(static_cast<unsigned char>(c)))
                    c = '_';
            file = fs::path(user_folder("bases")) / "models" / (slug + ".glb");
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            if (!fs::exists(file, ec) && !write_glb(nodes, file, error))
            {
                blender_status_ = "Not exported: " + error;
                return false;
            }
        }
        if (!blender_.start(mesh_editor_choice(sim.props.get_string("/sim/settings/mesh-editor", "blender"), sim.props.get_string("/sim/settings/blender", "blender"), sim.props.get_string("/sim/settings/kosmoscad", ""), fs::path(sim.root())), file, error))
        {
            blender_status_ = "Cannot start the editor: " + error;
            return false;
        }
        blender_saved_ = saved;
        blender_base_ = base_;
        if (!(indices.size() == 1 && saved[static_cast<std::size_t>(indices[0])].type == "model"))
        {
            BaseObject model;
            model.type = "model";
            model.model = (fs::path("models") / file.filename()).generic_string();
            std::vector<BaseObject> rest;
            for (std::size_t i = 0; i < saved.size(); ++i)
                if (std::find(indices.begin(), indices.end(), static_cast<int>(i)) == indices.end())
                    rest.push_back(saved[i]);
            rest.push_back(model);
            base.objects = rest;
            object_ = static_cast<int>(rest.size()) - 1;
        }
        blender_was_paused_ = sim.props.get_bool("/sim/paused");
        sim.props.set_bool("/sim/paused", true);
        sim.props.set_bool("/sim/blender-edit", true);
        blender_paused_ = true;
        blender_status_ = "Editing " + file.filename().string() + " in " + blender_.name();
        return true;
    }

    void BaseEditor::tab_site(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz)
    {
        text("Id", b.id);
        text("Name", b.name);
        text("ICAO code", b.icao);
        std::vector<std::string> bodies = body_names(sim);
        int bi = index_of(bodies, b.body);
        bool moved = false;
        if (combo("Body", bi, bodies) && bi >= 0)
        {
            b.body = bodies[static_cast<std::size_t>(bi)];
            moved = true;
        }
        moved |= dbl("Latitude", b.latitude, 0.001f, -90.0, 90.0, "%.5f");
        moved |= dbl("Longitude", b.longitude, 0.001f, -180.0, 180.0, "%.5f");
        moved |= dbl("Heading", b.heading, 1.0f, 0.0, 360.0, "%.1f deg");
        if (moved)
        {
            const int body = sim.universe.find(b.body);
            if (body >= 0)
            {
                b.direction = base_direction(sim.universe, body, b);
                b.pole = sim.universe.pole(body);
            }
        }
        dbl("Level radius m", b.flat_radius, 1.0f, 0.0, 20000.0, "%.0f");
        dbl("Blend m", b.blend, 1.0f, 0.0, 20000.0, "%.0f");
        dbl("Elevation m", b.elevation, 1.0f, -500.0, 9000.0, "%.0f");
        ImGui::SeparatorText("Landing pad");
        dbl("Pad x", b.pad_x, 0.5f);
        dbl("Pad z", b.pad_z, 0.5f);
        dbl("Pad heading", b.pad_heading, 1.0f, -360.0, 360.0, "%.0f deg");
        dbl("Pad height m", b.pad_deck, 0.1f, -50.0, 200.0, "%.1f");
        ImGui::TextDisabled("Height a ship stands above the levelled ground (0 = from the pad object). Restart to apply.");
        if (ImGui::Button("Pad at flown ship"))
            ship_xz(b.pad_x, b.pad_z);
        ImGui::TextWrapped("Objects and cameras change at once. Moving the site or its levelled ground takes effect after a restart.");
        ImGui::EndTabItem();
    }

    void BaseEditor::stock_model_row(Base &b)
    {
        if (stock_models_.empty())
            stock_models_ = stock_base_models();
        std::vector<std::string> &stock = stock_models_;
        int &stock_pick = stock_pick_;
        if (!stock.empty())
        {
            combo("Stock model", stock_pick, stock);
            ImGui::SameLine();
            if (ImGui::Button("Add##stock"))
            {
                BaseObject model;
                model.type = "model";
                model.model = stock[static_cast<std::size_t>(std::clamp(stock_pick, 0, static_cast<int>(stock.size()) - 1))];
                b.objects.push_back(model);
                object_ = static_cast<int>(b.objects.size()) - 1;
            }
        }
    }

    void BaseEditor::blender_controls(Sim &sim, Base &b, const BaseObject &o)
    {
    ImGui::SeparatorText("Edit in Blender or KosmosCAD");
    {
        const bool cad = sim.props.get_string("/sim/settings/mesh-editor", "blender") == "kosmoscad";
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("Mesh editor", cad ? "KosmosCAD" : "Blender"))
        {
            if (ImGui::Selectable("Blender", !cad))
                sim.props.set_string("/sim/settings/mesh-editor", "blender");
            if (ImGui::Selectable("KosmosCAD", cad))
                sim.props.set_string("/sim/settings/mesh-editor", "kosmoscad");
            ImGui::EndCombo();
        }
    }
    picked_.resize(b.objects.size(), 0);
    if (ImGui::TreeNode("Pick objects to join"))
    {
        for (std::size_t i = 0; i < b.objects.size(); ++i)
        {
            bool on = picked_[i] != 0;
            const std::string label = std::to_string(i) + "  " + b.objects[i].type + "##pick" + std::to_string(i);
            if (ImGui::Checkbox(label.c_str(), &on))
                picked_[i] = on ? 1 : 0;
        }
        ImGui::TreePop();
    }
    if (blender_paused_)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Paused; editing in %s", blender_.name().c_str());
        ImGui::TextWrapped("Save in %s (Ctrl+S) to see it here; closing it resumes.", blender_.name().c_str());
        if (ImGui::Button("Resume, keep editor open"))
        {
            blender_paused_ = false;
            sim.props.set_bool("/sim/paused", blender_was_paused_);
            sim.props.set_bool("/sim/blender-edit", false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel and restore"))
        {
            blender_.stop();
            blender_.restore();
            if (blender_base_ == base_)
                b.objects = blender_saved_;
            end_blender(sim);
            blender_status_ = "Edit cancelled; the objects and the model file were restored";
        }
    }
    else
    {
        const bool is_model = o.type == "model";
        if (ImGui::Button(is_model ? "Edit this model" : "Convert this object + edit"))
            begin_blender(sim, b, {object_});
        std::vector<int> chosen;
        for (std::size_t i = 0; i < picked_.size() && i < b.objects.size(); ++i)
            if (picked_[i] && b.objects[i].type != "model")
                chosen.push_back(static_cast<int>(i));
        ImGui::BeginDisabled(chosen.size() < 2);
        ImGui::SameLine();
        if (ImGui::Button("Join picked + edit"))
        {
            if (begin_blender(sim, b, chosen))
                picked_.assign(b.objects.size(), 0);
        }
        ImGui::EndDisabled();
    }
    if (!blender_status_.empty())
        ImGui::TextWrapped("%s", blender_status_.c_str());
    }

    void BaseEditor::object_fields(BaseObject &o, const std::function<void(double &, double &)> &ship_xz)
    {
    if (o.type == "model")
    {
        text("Model file", o.model);
        dbl("x (right)", o.x, 0.5f);
        dbl("z (forward)", o.z, 0.5f);
        dbl("Lift y", o.y, 0.1f);
        dbl("Heading", o.heading, 1.0f, -360.0, 360.0, "%.0f deg");
        dbl("Scale", o.scale, 0.01f, 0.05, 50.0, "%.2f");
        ImGui::TextDisabled("Models are not repeated; add another object for another copy.");
    }
    else
    {
        const std::vector<std::string> &types = base_object_types();
        int ti = index_of(types, o.type);
        if (combo("Type", ti, types) && ti >= 0)
            o.type = types[static_cast<std::size_t>(ti)];
        dbl("x (right)", o.x, 0.5f);
        dbl("z (forward)", o.z, 0.5f);
        dbl("Lift y", o.y, 0.1f);
        dbl("Heading", o.heading, 1.0f, -360.0, 360.0, "%.0f deg");
        dbl("Scale", o.scale, 0.01f, 0.05, 50.0, "%.2f");
        if (ImGui::Button("Place at flown ship"))
            ship_xz(o.x, o.z);
        ImGui::SeparatorText("Row");
        ui::slider_int("Copies", &o.count, 1, 200);
        dbl("Step x", o.step_x, 0.5f);
        dbl("Step z", o.step_z, 0.5f);
        ImGui::SeparatorText("Size (0 keeps the default)");
        dbl("Size", o.size, 0.1f, 0.0, 1000.0, "%.2f");
        dbl("Width", o.width, 0.1f, 0.0, 1000.0, "%.2f");
        dbl("Depth", o.depth, 0.1f, 0.0, 1000.0, "%.2f");
        dbl("Height", o.height, 0.1f, 0.0, 1000.0, "%.2f");
        dbl("Length", o.length, 0.1f, 0.0, 1000.0, "%.2f");
        dbl("Radius", o.radius, 0.1f, 0.0, 1000.0, "%.2f");
        ui::slider_int("Number", &o.number, 0, 100);
        int seed = static_cast<int>(o.seed);
        if (ImGui::InputInt("Seed", &seed))
            o.seed = static_cast<unsigned>(std::max(0, seed));
        float c1[3] = {o.colour.red / 255.0f, o.colour.green / 255.0f, o.colour.blue / 255.0f};
        float c2[3] = {o.colour2.red / 255.0f, o.colour2.green / 255.0f, o.colour2.blue / 255.0f};
        ImGui::Checkbox("##c1", &o.has_colour);
        ImGui::SameLine();
        if (ImGui::ColorEdit3("Colour", c1))
        {
            o.has_colour = true;
            o.colour = {static_cast<std::uint8_t>(c1[0] * 255.0f + 0.5f), static_cast<std::uint8_t>(c1[1] * 255.0f + 0.5f), static_cast<std::uint8_t>(c1[2] * 255.0f + 0.5f), 255};
        }
        ImGui::Checkbox("##c2", &o.has_colour2);
        ImGui::SameLine();
        if (ImGui::ColorEdit3("Colour 2", c2))
        {
            o.has_colour2 = true;
            o.colour2 = {static_cast<std::uint8_t>(c2[0] * 255.0f + 0.5f), static_cast<std::uint8_t>(c2[1] * 255.0f + 0.5f), static_cast<std::uint8_t>(c2[2] * 255.0f + 0.5f), 255};
        }
    }
    }

    void BaseEditor::tab_objects(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz)
    {
        BaseObject fresh;
        fresh.type = "house";
        list_box("objects", b.objects, object_, [](const BaseObject &o) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s  (%.0f, %.0f)%s", o.type.c_str(), o.x, o.z, o.count > 1 ? "  x" : "");
            return std::string(buf) + (o.count > 1 ? std::to_string(o.count) : std::string());
        }, fresh);
        stock_model_row(b);
        if (!b.objects.empty())
        {
            object_ = std::clamp(object_, 0, static_cast<int>(b.objects.size()) - 1);
            BaseObject &o = b.objects[static_cast<std::size_t>(object_)];
            ImGui::Separator();
            blender_controls(sim, b, o);
            ImGui::Separator();
            object_fields(o, ship_xz);
        }
        ImGui::EndTabItem();
    }

    void BaseEditor::tab_runways(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz)
    {
        list_box("runways", b.runways, runway_, [](const Runway &r) { return r.name; }, Runway{});
        if (!b.runways.empty())
        {
            runway_ = std::clamp(runway_, 0, static_cast<int>(b.runways.size()) - 1);
            Runway &r = b.runways[static_cast<std::size_t>(runway_)];
            ImGui::Separator();
            text("Name", r.name);
            dbl("x", r.x, 0.5f);
            dbl("z", r.z, 0.5f);
            dbl("Heading", r.heading, 1.0f, -360.0, 360.0, "%.1f deg");
            dbl("Length m", r.length, 1.0f, 50.0, 6000.0, "%.0f");
            dbl("Width m", r.width, 0.5f, 5.0, 200.0, "%.0f");
            if (ImGui::Button("Centre at flown ship"))
                ship_xz(r.x, r.z);
            ImGui::SeparatorText("ILS");
            for (std::size_t i = 0; i < r.ils.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                dbl("MHz", r.ils[i].frequency, 0.05f, 108.0, 112.0, "%.2f");
                ImGui::SameLine();
                ImGui::Checkbox("Reverse", &r.ils[i].reverse);
                ImGui::SameLine();
                if (ImGui::SmallButton("x"))
                {
                    r.ils.erase(r.ils.begin() + static_cast<std::ptrdiff_t>(i));
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::Button("Add ILS"))
                r.ils.push_back({109.5, false});
        }
        ImGui::EndTabItem();
    }

    void BaseEditor::tab_navaids(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz)
    {
        BaseNavaid fresh;
        fresh.type = "vor";
        fresh.id = b.id + "-vor";
        fresh.name = b.name + " VOR";
        fresh.frequency = 112.0;
        list_box("navaids", b.navaids, navaid_, [](const BaseNavaid &n) { return n.type + "  " + n.id; }, fresh);
        if (!b.navaids.empty())
        {
            navaid_ = std::clamp(navaid_, 0, static_cast<int>(b.navaids.size()) - 1);
            BaseNavaid &n = b.navaids[static_cast<std::size_t>(navaid_)];
            ImGui::Separator();
            const std::vector<std::string> types = {"vor", "ndb", "ils", "vtol"};
            int ti = index_of(types, n.type);
            if (combo("Type", ti, types) && ti >= 0)
                n.type = types[static_cast<std::size_t>(ti)];
            text("Id", n.id);
            text("Name", n.name);
            dbl("x", n.x, 0.5f);
            dbl("z", n.z, 0.5f);
            dbl("Frequency", n.frequency, 0.05f, 0.0, 0.0, "%.2f");
            dbl("Heading", n.heading, 1.0f, -360.0, 360.0, "%.0f deg");
            dbl("Glide slope", n.glide, 0.1f, 0.0, 10.0, "%.1f deg");
            dbl("Range m", n.range, 100.0f, 0.0, 1000000.0, "%.0f");
            if (ImGui::Button("Place at flown ship"))
                ship_xz(n.x, n.z);
            ImGui::TextDisabled("Beacons are registered when the scenario starts.");
        }
        ImGui::EndTabItem();
    }

    void BaseEditor::tab_cameras(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz)
    {
        BaseCamera fresh;
        fresh.name = "Camera";
        list_box("cameras", b.cameras, camera_, [](const BaseCamera &c) { return c.name; }, fresh);
        if (!b.cameras.empty())
        {
            camera_ = std::clamp(camera_, 0, static_cast<int>(b.cameras.size()) - 1);
            BaseCamera &c = b.cameras[static_cast<std::size_t>(camera_)];
            ImGui::Separator();
            text("Name", c.name);
            dbl("x", c.x, 0.5f);
            dbl("y (up)", c.y, 0.5f);
            dbl("z", c.z, 0.5f);
            dbl("Field of view", c.fov, 0.5f, 10.0, 120.0, "%.0f deg");
            ImGui::Checkbox("Track the ship", &c.track);
            if (!c.track)
            {
                dbl("Yaw", c.yaw, 1.0f, -360.0, 360.0, "%.0f deg");
                dbl("Pitch", c.pitch, 1.0f, -90.0, 90.0, "%.0f deg");
            }
            if (ImGui::Button("Place at flown ship"))
                ship_xz(c.x, c.z);
        }
        ImGui::EndTabItem();
    }

    void BaseEditor::draw(bool &open, Sim &sim, FileDialog &dialog)
    {
        tick_blender(sim);
        if (!open)
        {
            was_open_ = false;
            return;
        }
        if (!was_open_)
        {
            was_open_ = true;
            // Start on the base nearest the flown ship (on the body it is orbiting).
            if (sim.ship_count() > 0)
            {
                const Ship *flown = sim.active_ship();
                double best = 1.0e300;
                for (std::size_t i = 0; i < sim.bases.size(); ++i)
                {
                    const Base &c = sim.bases[i];
                    if (sim.universe.find(c.body) != flown->body_id())
                        continue;
                    double x, z;
                    base_local_xz(sim.universe, c, flown->body().position, x, z);
                    if (std::hypot(x, z) < best)
                    {
                        best = std::hypot(x, z);
                        base_ = static_cast<int>(i);
                    }
                }
            }
        }
        ImGui::SetNextWindowPos(ImVec2(60.0f, 80.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480.0f, 700.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.92f);
        if (ImGui::Begin("Base editor", &open))
        {
            std::vector<std::string> ids;
            for (const Base &b : sim.bases)
                ids.push_back(b.id + "  -  " + b.name);
            base_ = std::clamp(base_, 0, std::max(0, static_cast<int>(sim.bases.size()) - 1));
            if (sim.bases.empty())
            {
                ImGui::TextDisabled("No bases loaded.");
                if (ImGui::Button("New base"))
                {
                    Base b;
                    b.id = "new_base";
                    b.name = "New base";
                    b.body = sim.universe.bodies[static_cast<std::size_t>(sim.universe.home_id())].name;
                    sim.bases.push_back(b);
                }
                ImGui::End();
                return;
            }
            combo("Base (nearest to you on opening)", base_, ids);
            Base &b = sim.bases[static_cast<std::size_t>(base_)];
            if (ImGui::Button("New"))
            {
                Base fresh = b;
                fresh.id = b.id + "_copy";
                fresh.name = b.name + " copy";
                sim.bases.push_back(fresh);
                base_ = static_cast<int>(sim.bases.size()) - 1;
                status_ = "Copied the base. Change its place and id.";
                ImGui::End();
                return;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Copy this base as the starting point of a new one");
            ImGui::SameLine();
            if (ImGui::Button("Save"))
            {
                const std::string path = (fs::path(user_folder("bases")) / (b.id + ".json")).string();
                std::string error;
                status_ = save_bases(path, {b}, error) ? "Saved " + path : error;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Write this base to your user folder; it replaces a shipped base with the same id");
            ImGui::SameLine();
            if (ImGui::Button("Export..."))
            {
                const Base copy = b;
                dialog.show(FileDialog::Mode::save, "Export base", user_folder("bases"), b.id + ".json", [this, copy](const std::string &path) {
                    std::string error;
                    status_ = save_bases(path, {copy}, error) ? "Saved " + path : error;
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Import..."))
                dialog.show(FileDialog::Mode::open, "Import bases", user_folder("bases"), "", [this, &sim](const std::string &path) {
                    std::vector<Base> loaded;
                    std::string error;
                    if (!load_bases(path, loaded, error))
                    {
                        status_ = error;
                        return;
                    }
                    for (Base &nb : loaded)
                    {
                        const int body = sim.universe.find(nb.body);
                        if (body >= 0)
                        {
                            nb.direction = base_direction(sim.universe, body, nb);
                            nb.pole = sim.universe.pole(body);
                            nb.height = nb.elevation;
                        }
                        sim.bases.push_back(std::move(nb));
                    }
                    base_ = static_cast<int>(sim.bases.size()) - 1;
                    status_ = "Imported " + path;
                });
            ImGui::BeginDisabled(undo_.empty());
            if (ImGui::Button("Undo"))
                undo(sim);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(redo_.empty());
            if (ImGui::Button("Redo"))
                redo(sim);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", status_.c_str());
            ImGui::Separator();

            const sl::dvec3 ship_pos = sim.ship_count() > 0 ? sim.active_ship()->body().position : sl::dvec3(0.0);
            const auto ship_xz = [&](double &x, double &z) { base_local_xz(sim.universe, b, ship_pos, x, z); };

            if (ImGui::BeginTabBar("##basetabs"))
            {
                if (ImGui::BeginTabItem("Site"))
                {
        tab_site(sim, b, ship_xz);
                }
                if (ImGui::BeginTabItem("Objects"))
                {
        tab_objects(sim, b, ship_xz);
                }
                if (ImGui::BeginTabItem("Runways"))
                {
        tab_runways(sim, b, ship_xz);
                }
                if (ImGui::BeginTabItem("Navaids"))
                {
        tab_navaids(sim, b, ship_xz);
                }
                if (ImGui::BeginTabItem("Cameras"))
                {
        tab_cameras(sim, b, ship_xz);
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        snapshot_changes(sim);
    }

    void BaseEditor::snapshot_changes(Sim &sim)
    {
        if (base_ < 0 || base_ >= static_cast<int>(sim.bases.size()))
            return;
        const std::string now = bases_to_json({sim.bases[static_cast<std::size_t>(base_)]});
        if (committed_base_ != base_)
        {
            committed_base_ = base_;
            committed_ = now;
            undo_.clear();
            redo_.clear();
            return;
        }
        commit(now, committed_, undo_, redo_);
    }

    // --------------------------------------------------------------- menu bar

    namespace
    {
        const char *control_help = "Flight\n"
                                   "  Keypad + / *     full / zero throttle\n"
                                   "  Ctrl+Keypad + -  throttle up / down\n"
                                   "  Space            stage\n"
                                   "  Keypad 2 8 1 3 4 6  pitch, yaw, roll as in Orbiter (Ctrl: fine)\n"
                                   "  Keypad /         RCS rotation / linear   Ctrl+Keypad /  RCS off\n"
                                   "  Linear RCS       Keypad 2 8 up/down, 1 3 left/right, 6 9 forward/back\n"
                                   "  Arrows           RCS translation\n"
                                   "  9/3 8/2 (digits) engine pods (Shuttle-A): thrust, swing; 4/5/6 retro/hover/fwd\n"
                                   "  Keypad 5         SAS kill rotation   [ ] ; '  prograde / retrograde / normal / anti-normal\n"
                                   "  P                autopilot on/off; cancel autoland, timed burn or ship-specific ascent\n"
                                   "  Ctrl+D           undock\n"
                                   "  Home             reset cockpit view to forwards\n"
                                   "  Ctrl+Home/End    hover full / off\n"
                                   "  R / T            time warp down / up (to 50x while thrusting in vacuum)\n"
                                   "                   Other vessels flying in air limit warp; stationary inactive undocked vessels can be held on the ground during rails warp.\n"
                                   "  Shift+R          restart scenario\n"
                                   "View\n"
                                   "  C                chase / cockpit / 3D cockpit   View > Camera: Standard or Dynamic (cockpit eye sways with acceleration)\n"
                                   "  V                switch ship\n"
                                   "  Right-drag       look around\n"
                                   "  - =              zoom   X / Z  field of view (Ctrl: steps)\n"
                                   "  O                orbit line\n"
                                   "  Ctrl+M / N       map / galaxy map (Enter dials the quantum portal)   Ctrl+I  on-screen text\n"
                                   "Time\n"
                                   "  Ctrl+P           pause / resume   Ctrl+C  start / stop video recording\n"
                                   "  Ctrl+Esc         quit (fixed)   Ctrl+Q  quit (default, rebindable)\n"
                                   "On foot (Enter to leave the ship, board one within 30 m, or enter a building at its door; inside, face a wall panel and Enter to use it, and leave through the airlock hatch)\n"
                                   "  F3 Settings     toggle building entrance labels\n"
                                   "  W A S D          walk / strafe       Shift   boost\n"
                                   "  Space jump  Ctrl crouch              Mouse or arrows   look (` or Alt frees the mouse)\n"
                                   "  F                manoeuvring pack on / off (airless worlds, orbit)\n"
                                   "  Q / E            roll (when floating free)\n"
                                   "  1-9              pick a pack item   Left mouse   use it (pistol, plasma rifle, charges; with the tricorder it flips its pages: environment, suit, samples, pack)\n"
                                   "Lights\n"
                                   "  Y                navigation lights and strobes on / off   Shift+Y   landing and nose lights (also View menu)\n"
                                   "Ordnance (flying a ship with hardpoints, such as the CVEL Sentinel ICBM)\n"
                                   "  K                release the next store   Shift+K   release the special store\n"
                                   "Animations\n"
                                   "  View > Animations lists the ship's hatches, doors and other moving parts; click one to open or close it. Soyuz solar arrays must be deployed to generate power.\n"
"Instruments\n"
"  Probes with cameras, radar or a spectrometer have an INST MFD: switch the instrument LOAD on in Systems, point it with AZ/EL, SHOOT saves a picture or spectrum, GALLERY browses them\n"
"  Luna E3 RTG is a fictional radioisotope-powered variant for missions beyond Earth's orbit; the original uses solar cells.\n"
"Planets\n"
                                   "  Atmospheres have haze (Earth, Mars, Venus, Titan, gas giants); airless worlds shade like the real Moon; Earth has night-side city lights and sharp tiled clouds (data/textures-hires/earth_clouds)\n"
                                   "  Installed Orbiter Mars elevation shapes the terrain in low and medium orbit as well as near the ground.\n"
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_RELIEF
                                   "  Orbiter relief is the default when assets are available; --no-orbital-relief-ground restores old ground terrain.\n"
#endif
#ifdef KOSMOS_EXPERIMENTAL_ORBITAL_MICRODETAIL
                                   "  Microdetail uses assets in import/orbiter or --orbiter-microtexture-root PATH; --no-detail disables it.\n"
#endif
                                   "  --cam-planet R --cam-phase DEG (command line) frames the planet alone for screenshots\n"
                                   "Windows\n"
                                   "  F2 cameras   F3 settings   F4 properties   F11 Lua\n"
                                   "  Alt+Enter toggles full screen, including while loading; the window can also be resized or maximized.\n"
                                   "  Keypad 7 / Insert: fire primary / secondary weapons (ships that carry them)\n"
                                   "  Delete parachute deploy   Shift+Delete cut chutes\n"
                                   "  Automatic parachutes arm after climbing above their arm altitude; manual deployment needs no arming.\n"
                                   "  Cockpit controls show a tooltip on hover   F5-F9 MFDs (F5 cycles off, panels, HUD both, left, right; see the MFD Operations Manual on the website)   F10 next scenario   View > Joystick: bind sticks   View > Keyboard: rebind keys\n"
                                   "  Staging to a payload keeps the first two MFDs where their modes are available on the new vessel.\n"
                                   " Ares, Deltaglider and Shuttle-A have experimental ship-specific ascent MFDs; set altitude and heading, START from rest, monitor and take over if needed. Passive docked payloads are allowed.\n"
                                   " Autoland offers untargeted Mars descent, local targets, or experimental orbital base landing for Ares and Shuttle-A. Orbital planning and checking run at 1x; flight allows up to 4x. Monitor and take over; time skips are blocked.\n"
                                   " File > Record flight saves sampled states; Open flight recording plays snapshots. Video recording is separate.\n"
                                   " Tools > Scenario thumbnail captures the view for the active saved scenario, replacing its previous generated thumbnail; the launcher refreshes automatically.\n"
                                   " F3 offers optional harmonic gravity for Mercury, Venus, Earth, Moon, Mars, Ceres and Vesta; rails and time skips suspend it, and predictions stay approximate.\n"
                                   " Autopilot > Spacecraft > earth-communications points the active ship's nose at Earth in physics flight, not on rails.\n"
                                   "  Orbit MFD Orb is planet-centred speed; RADALT estimates clearance and samples a constant-speed terrain track, not obstacles or braking.\n"
                                   "  Sync Orbit APPR shows short-range relative motion; RVEL points against target-relative velocity for a manual burn. Cut thrust before zero; use Docking for port alignment.\n"
                                   "  Autopilot > Spacecraft includes timed main/hover burns; allow time for SAS alignment and monitor fuel.\n"
                                   "  View > Native addons: allow or stop external programs (the Kosmos SDK) that connect to the sim (the tools bundled with Kosmos are allowed already); KosmosMission plans multi-leg missions (ascent, transfers, delta-v budget) and can send a burn to OrbitComp\n"
                                   "  File > Set time to T-5:00 before burn: skip the clock to just before the Transfer / OrbitComp burn; File > Transfer windows: pick a departure window to a target\n"
                                   "  OrbitComp planet-to-planet windows are estimates; check the predicted encounter and correct the flight.\n"
                                   "  Move the mouse to the top of the screen for the menu bar.\n"
                                   "  Esc closes menus and windows or leaves a habitat panel; Ctrl+Esc quits.";
    }

    bool MenuBar::has_open_window(const std::vector<MenuWindow> &windows) const
    {
        if (dialog.active() || scenario_editor_open || base_editor_open || help_open || about_open ||
            aircraft_autopilot_open || spacecraft_autopilot_open || transfer_windows_open)
            return true;
        return std::any_of(windows.begin(), windows.end(), [](const MenuWindow &window) {
            return window.open && *window.open;
        });
    }

    bool MenuBar::dismiss(std::vector<MenuWindow> &windows)
    {
        if (menu_open_ || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        {
            close_menus_ = true;
            return true;
        }
        if (dialog.active())
        {
            dialog.close();
            return true;
        }
        bool any = false;
        for (bool *flag : {&scenario_editor_open, &base_editor_open, &help_open, &about_open, &aircraft_autopilot_open, &spacecraft_autopilot_open, &transfer_windows_open})
            if (*flag)
            {
                *flag = false;
                any = true;
            }
        for (MenuWindow &w : windows)
            if (w.open && *w.open)
            {
                *w.open = false;
                any = true;
            }
        return any;
    }

    namespace
    {
        constexpr double metres_to_feet = 3.28084, ms_to_knots = 1.94384, ms_to_fpm = 196.8504;

        // A mode checkbox; turning it on first copies the aircraft's current value into the target.
        bool mode_checkbox(sl::PropertyTree &p, const char *label, const char *mode, const char *target, double current)
        {
            const std::string path = std::string("/autopilot/ac/") + mode;
            bool on = p.get_bool(path);
            if (!ImGui::Checkbox(label, &on))
                return false;
            if (on && target)
                p.set_double(std::string("/autopilot/ac/") + target, current);
            p.set_bool(path, on);
            return true;
        }

        // A target field shown in `scale` units (the property stores SI), with a button that takes the current value.
        void target_field(sl::PropertyTree &p, const char *id, const char *target, double scale, double current, const char *format, double wrap)
        {
            const std::string path = std::string("/autopilot/ac/") + target;
            float shown = static_cast<float>(p.get_double(path) * scale);
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::InputFloat((std::string("##") + id).c_str(), &shown, 0.0f, 0.0f, format))
            {
                double v = shown / scale;
                if (wrap > 0.0)
                    v = std::fmod(std::fmod(v, wrap) + wrap, wrap);
                p.set_double(path, v);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton((std::string("Current##") + id).c_str()))
                p.set_double(path, current);
        }
    } // namespace

    void draw_aircraft_autopilot(Sim &sim, bool &open)
    {
        ImGui::SetNextWindowPos(ImVec2(120.0f, 120.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Aircraft autopilot", &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }
        sl::PropertyTree &p = sim.props;
        Ship *ship = sim.active_ship();
        if (!ship)
        {
            ImGui::TextUnformatted("No vessel.");
            ImGui::End();
            return;
        }
        const Telemetry &tm = ship->telemetry();
        if (!ship->flies_as_aircraft())
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s is not flying as an aircraft; these modes need wings.", ship->name.c_str());

        bool engaged = p.get_bool("/autopilot/enabled");
        if (ImGui::Checkbox("Autopilot engaged  [P]", &engaged))
        {
            if (engaged)
                sim.cancel_vessel_ascent("Ascent cancelled: aircraft autopilot engaged");
            p.set_bool("/autopilot/enabled", engaged);
            if (!engaged)
                p.set_string("/autopilot/status", "");
        }
        const std::string status = p.get_string("/autopilot/status");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", engaged && !status.empty() ? status.c_str() : "off");
        ImGui::Text("Now: heading %03.0f   altitude %.0f ft   speed %.0f kt   climb %+.0f ft/min", tm.heading, tm.altitude * metres_to_feet, tm.surface_speed * ms_to_knots, tm.vertical_speed * ms_to_fpm);
        ImGui::Separator();

        ImGui::TextUnformatted("Lateral");
        mode_checkbox(p, "Heading hold##hdg", "hdg-hold", "heading", tm.heading);
        ImGui::SameLine(190.0f);
        target_field(p, "hdg", "heading", 1.0, tm.heading, "%.0f deg", 360.0);

        ImGui::Separator();
        ImGui::TextUnformatted("Vertical");
        if (mode_checkbox(p, "Altitude hold##alt", "alt-hold", "altitude", tm.altitude) && p.get_bool("/autopilot/ac/alt-hold"))
            p.set_bool("/autopilot/ac/vs-hold", false);
        ImGui::SameLine(190.0f);
        target_field(p, "alt", "altitude", metres_to_feet, tm.altitude, "%.0f ft", 0.0);
        if (mode_checkbox(p, "Vertical speed hold##vs", "vs-hold", "vs", tm.vertical_speed) && p.get_bool("/autopilot/ac/vs-hold"))
            p.set_bool("/autopilot/ac/alt-hold", false);
        ImGui::SameLine(190.0f);
        target_field(p, "vs", "vs", ms_to_fpm, tm.vertical_speed, "%.0f ft/min", 0.0);

        ImGui::Separator();
        ImGui::TextUnformatted("Autothrottle");
        mode_checkbox(p, "Speed hold##spd", "spd-hold", "speed", tm.surface_speed);
        ImGui::SameLine(190.0f);
        target_field(p, "spd", "speed", ms_to_knots, tm.surface_speed, "%.0f kt", 0.0);

        ImGui::Separator();
        ImGui::TextUnformatted("Radio navigation");
        int receiver = p.get_int("/autopilot/ac/receiver");
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("Receiver", &receiver, "NAV1\0NAV2\0"))
            p.set_int("/autopilot/ac/receiver", receiver);
        const std::string nav = "/nav/nav[" + std::to_string(receiver) + "]/";
        const bool ils = p.get_bool(nav + "ils/valid");
        ImGui::SameLine();
        if (ils)
            ImGui::Text("%.2f  %s  ILS  loc %+.1f  gs %+.1f", p.get_double(nav + "frequency"), p.get_string(nav + "ident").c_str(), p.get_double(nav + "ils/localizer"), p.get_double(nav + "ils/glideslope"));
        else
            ImGui::TextDisabled("%.2f  no ILS signal", p.get_double(nav + "frequency"));
        bool loc = p.get_bool("/autopilot/ac/nav"), appr = p.get_bool("/autopilot/ac/appr");
        if (ImGui::Checkbox("NAV: fly the localizer", &loc))
        {
            p.set_bool("/autopilot/ac/nav", loc);
            if (loc)
                p.set_bool("/autopilot/ac/appr", false);
        }
        if (ImGui::Checkbox("APPR: localizer and glideslope", &appr))
        {
            p.set_bool("/autopilot/ac/appr", appr);
            if (appr)
                p.set_bool("/autopilot/ac/nav", false);
        }
        ImGui::TextDisabled("Pitch, yaw or roll input hands control back to you.");
        ImGui::End();
    }

    void draw_spacecraft_autopilot(Sim &sim, bool &open)
    {
        ImGui::SetNextWindowPos(ImVec2(160.0f, 140.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Spacecraft autopilot", &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }
        sl::PropertyTree &p = sim.props;
        Ship *ship = sim.active_ship();
        if (!ship)
        {
            ImGui::TextUnformatted("No vessel.");
            ImGui::End();
            return;
        }
        if (ship->flies_as_aircraft())
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s is flying as an aircraft; see Autopilot > Aircraft.", ship->name.c_str());

        const bool scripted = !ship->spec().script.empty();
        bool engaged = p.get_bool("/autopilot/enabled");
        ImGui::TextUnformatted("Programmed flight");
        if (scripted)
        {
            if (ImGui::Checkbox("Ascent autopilot engaged  [P]", &engaged))
            {
                if (engaged)
                {
                    sim.cancel_timed_burn("Timed burn cancelled: ascent engaged");
                    sim.cancel_vessel_ascent("Ascent cancelled: another autopilot engaged");
                }
                p.set_bool("/autopilot/enabled", engaged);
                if (!engaged)
                    p.set_string("/autopilot/status", "");
            }
            const std::string status = p.get_string("/autopilot/status");
            ImGui::SameLine();
            ImGui::TextDisabled("%s", engaged && !status.empty() ? status.c_str() : "off");
            float altitude = static_cast<float>(p.get_double("/autopilot/sc/target-altitude")), heading = static_cast<float>(p.get_double("/autopilot/sc/heading"));
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::InputFloat("Target orbit (km)", &altitude, 10.0f, 100.0f, "%.0f"))
                p.set_double("/autopilot/sc/target-altitude", std::clamp(static_cast<double>(altitude), 80.0, 2000.0));
            ImGui::SetNextItemWidth(140.0f);
            if (ui::slider_float("Launch heading", &heading, 0.0f, 360.0f, "%.0f deg"))
                p.set_double("/autopilot/sc/heading", heading);
            ImGui::TextDisabled("Liftoff, gravity turn, staging, coast and circularise.");
        }
        else
            ImGui::TextDisabled("%s has no programmed autopilot.", ship->name.c_str());

        ImGui::Separator();
        ImGui::TextUnformatted("Autoland (experimental)");
        bool landing = p.get_bool("/autopilot/land/armed");
        if (ImGui::Checkbox("Autoland engaged", &landing))
        {
            if (landing)
            {
                sim.cancel_timed_burn("Timed burn cancelled: autoland engaged");
                sim.cancel_vessel_ascent("Ascent cancelled: autoland engaged");
                p.set_bool("/autopilot/enabled", false);
                p.set_bool("/autopilot/burn/armed", false);
                p.set_string("/autopilot/land/state", "off");
                p.set_bool("/autopilot/land/armed", true);
            }
            else
                sim.cancel_autoland("Autoland cancelled");
        }
        if (landing)
            ImGui::BeginDisabled();
        bool targeted = p.get_bool("/autopilot/land/targeted");
        if (ImGui::Checkbox("Target landing", &targeted))
            p.set_bool("/autopilot/land/targeted", targeted);
        if (targeted)
        {
            double latitude = p.get_double("/autopilot/land/latitude");
            double longitude = p.get_double("/autopilot/land/longitude");
            if (ImGui::InputDouble("Target latitude", &latitude, 0.01, 1.0, "%.5f"))
            {
                p.set_double("/autopilot/land/latitude", latitude);
                p.set_string("/autopilot/land/base", "");
            }
            if (ImGui::InputDouble("Target longitude", &longitude, 0.01, 1.0, "%.5f"))
            {
                p.set_double("/autopilot/land/longitude", longitude);
                p.set_string("/autopilot/land/base", "");
            }
            const std::string selected_base = p.get_string("/autopilot/land/base");
            const int soi = ship->telemetry().soi;
            const std::string body_name = soi >= 0 && soi < static_cast<int>(sim.universe.bodies.size()) ?
                                              sim.universe.bodies[static_cast<std::size_t>(soi)].name : "";
            if (ImGui::BeginCombo("Base", selected_base.empty() ? "Coordinates" : selected_base.c_str()))
            {
                for (const Base &base : sim.bases)
                    if (base.body == body_name && ImGui::Selectable(base.name.c_str()))
                    {
                        p.set_double("/autopilot/land/latitude", base.latitude);
                        p.set_double("/autopilot/land/longitude", base.longitude);
                        p.set_string("/autopilot/land/base", base.id);
                    }
                ImGui::EndCombo();
            }
        }
        if (landing)
            ImGui::EndDisabled();
        if (targeted)
        {
            ImGui::TextDisabled("Local targets: Ares on Mars or Moon; Shuttle-A on any suitable body.");
            ImGui::TextDisabled("Start 300-3000 m above ground, within 2 km, below 50 m/s horizontal");
            ImGui::TextDisabled("and below 30 m/s vertical speed.");
            ImGui::TextDisabled("From orbit, choose a base; spherical gravity and no weather are required.");
            ImGui::TextDisabled("Planning and checking hold 1x; flight allows up to 4x.");
            ImGui::TextDisabled("Use time warp while waiting for the window; planning may take time.");
            if (landing)
            {
                ImGui::Text("Target distance: %.0f m", p.get_double("/autopilot/land/distance"));
                if (p.get_string("/autopilot/land/state") == "Planning orbit")
                    ImGui::Text("Forecast candidate %d of %d", p.get_int("/autopilot/land/candidate"),
                                p.get_int("/autopilot/land/candidates"));
                const std::string phase = p.get_string("/autopilot/land/state");
                if (phase == "Waiting for window" || phase == "Checking orbit" || phase == "Plane alignment")
                    ImGui::Text("Time to plane burn: %.0f s", p.get_double("/autopilot/land/window"));
            }
        }
        else
            ImGui::TextDisabled("Untargeted Mars landing; start in orbit.");
        ImGui::TextDisabled("A selected base aims at its pad; orbital success is within 3 km of the base.");
        ImGui::TextDisabled("Forecasts are approximate; no terrain or obstacle avoidance. Monitor and take over.");
        const std::string landing_status = p.get_string("/autopilot/land/state");
        if (!landing_status.empty() && landing_status != "off")
            ImGui::TextWrapped("%s", landing_status.c_str());

        ImGui::Separator();
        ImGui::TextUnformatted("Attitude hold");
        const bool timed = p.get_bool("/autopilot/timed/armed");
        if ((engaged && scripted) || landing || timed)
            ImGui::BeginDisabled();
        static const char *const modes[] = {"stability", "prograde", "retrograde", "target-retrograde", "earth-communications", "radial-out", "radial-in", "normal", "antinormal", "attitude", "off"};
        const std::string sas = p.get_string("/controls/sas", "stability");
        int current = 0;
        for (int i = 0; i < static_cast<int>(sizeof(modes) / sizeof(modes[0])); ++i)
            if (sas == modes[i])
                current = i;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("Mode", modes[current]))
        {
            for (int i = 0; i < static_cast<int>(sizeof(modes) / sizeof(modes[0])); ++i)
                if (ImGui::Selectable(modes[i], i == current))
                {
                    sim.cancel_vessel_ascent("Ascent cancelled: manual SAS");
                    p.set_string("/controls/sas", modes[i]);
                }
            ImGui::EndCombo();
        }
        if (sas == "earth-communications")
        {
            bool valid = false;
            if (ship)
            {
                Controls controls;
                controls.sas = sas;
                ship->sas_direction(controls, valid);
            }
            if (!valid)
                ImGui::TextWrapped("Earth unavailable; SAS damps rotation.");
            else if (ship->on_rails())
                ImGui::TextWrapped("Earth hold suspended on rails.");
            else if (!ship->attitude_control_available() ||
                     (ship->spec().rcs_modes && p.get_string("/controls/rcs-mode", "rot") == "off"))
                ImGui::TextWrapped("Earth hold unavailable: attitude control is off or exhausted.");
            else
                ImGui::TextWrapped("Points the nose at Earth during physics flight; rails suspend attitude control.");
        }
        if (sas == "attitude")
        {
            float pitch = static_cast<float>(p.get_double("/controls/target-pitch")), heading = static_cast<float>(p.get_double("/controls/target-heading"));
            ImGui::SetNextItemWidth(160.0f);
            if (ui::slider_float("Pitch", &pitch, -90.0f, 90.0f, "%.0f deg"))
            {
                sim.cancel_vessel_ascent("Ascent cancelled: manual attitude");
                p.set_double("/controls/target-pitch", pitch);
            }
            ImGui::SetNextItemWidth(160.0f);
            if (ui::slider_float("Heading", &heading, 0.0f, 360.0f, "%.0f deg"))
            {
                sim.cancel_vessel_ascent("Ascent cancelled: manual attitude");
                p.set_double("/controls/target-heading", heading);
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Throttle");
        float throttle = static_cast<float>(p.get_double("/controls/throttle") * 100.0);
        ImGui::SetNextItemWidth(160.0f);
        if (ui::slider_float("##throttle", &throttle, 0.0f, 100.0f, "%.0f%%"))
        {
            sim.cancel_vessel_ascent("Ascent cancelled: manual throttle");
            p.set_double("/controls/throttle", throttle / 100.0);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Cut"))
        {
            sim.cancel_vessel_ascent("Ascent cancelled: manual throttle");
            p.set_double("/controls/throttle", 0.0);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Full"))
        {
            sim.cancel_vessel_ascent("Ascent cancelled: manual throttle");
            p.set_double("/controls/throttle", 1.0);
        }
        bool has_hover = false;
        if (ship->active_stage() >= 0 && ship->active_stage() < static_cast<int>(ship->spec().stages.size()))
            for (const EngineSpec &engine : ship->spec().stages[static_cast<std::size_t>(ship->active_stage())].engines)
                has_hover = has_hover || engine.hover;
        if (has_hover)
        {
            float hover = static_cast<float>(p.get_double("/controls/hover") * 100.0);
            ImGui::SetNextItemWidth(160.0f);
            if (ui::slider_float("Hover", &hover, 0.0f, 100.0f, "%.0f%%"))
            {
                sim.cancel_vessel_ascent("Ascent cancelled: manual hover");
                p.set_double("/controls/hover", hover / 100.0);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Cut##hover"))
            {
                sim.cancel_vessel_ascent("Ascent cancelled: manual hover");
                p.set_double("/controls/hover", 0.0);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Full##hover"))
            {
                sim.cancel_vessel_ascent("Ascent cancelled: manual hover");
                p.set_double("/controls/hover", 1.0);
            }
        }
        if ((engaged && scripted) || landing || timed)
            ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextUnformatted("Timed burn (ignition)");
        static int time_mode = 0, engine_mode = 0, burn_sas = 0;
        static double delay = 60.0, duration = 10.0, burn_throttle = 100.0;
        static char utc[64] = "";
        static const char *const burn_modes[] = {"stability", "prograde", "retrograde", "radial-out", "radial-in", "normal", "antinormal", "attitude"};
        ImGui::BeginDisabled(timed);
        ImGui::RadioButton("Countdown", &time_mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("UTC date", &time_mode, 1);
        ImGui::SetNextItemWidth(160.0f);
        if (time_mode == 0)
            ImGui::InputDouble("Ignition in (s)", &delay, 10.0, 60.0, "%.1f");
        else
            ImGui::InputText("Ignition UTC", utc, sizeof utc);
        if (time_mode == 1)
            ImGui::TextDisabled("YYYY-MM-DD HH:MM:SS UTC or MJD.");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputDouble("Duration (s)", &duration, 1.0, 10.0, "%.1f");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputDouble("Burn throttle (%%)", &burn_throttle, 5.0, 10.0, "%.1f");
        ImGui::RadioButton("Main", &engine_mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Hover##burn", &engine_mode, 1);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("Burn attitude", burn_modes[burn_sas]))
        {
            for (int i = 0; i < 8; ++i)
                if (ImGui::Selectable(burn_modes[i], i == burn_sas))
                    burn_sas = i;
            ImGui::EndCombo();
        }
        if (burn_sas == 7)
            ImGui::TextDisabled("Uses the pitch and heading set in Attitude hold.");
        if (ImGui::Button("Arm timed burn"))
        {
            double ignition_delay = delay;
            double jd = 0.0;
            if (time_mode == 1 && !parse_burn_date(utc, jd))
            {
                p.set_string("/autopilot/timed/state", "Not armed: invalid UTC date");
                sim.log("Timed burn not armed: invalid UTC date");
            }
            else
            {
                if (time_mode == 1)
                    ignition_delay = (jd - sim.universe.julian_date()) * 86400.0;
                sim.arm_timed_burn(ignition_delay, duration, burn_throttle / 100.0, engine_mode == 0 ? "main" : "hover", burn_modes[burn_sas]);
            }
        }
        ImGui::EndDisabled();
        if (timed)
        {
            ImGui::SameLine();
            if (ImGui::Button("Cancel timed burn"))
                sim.cancel_timed_burn("Timed burn cancelled");
            ImGui::Text("Remaining: %.1f s", p.get_double("/autopilot/timed/remaining"));
        }
        const std::string burn_state = p.get_string("/autopilot/timed/state");
        if (!burn_state.empty())
            ImGui::TextWrapped("%s", burn_state.c_str());
        ImGui::TextDisabled("Warp held at 1x; allow time to align before ignition.");
        ImGui::End();
    }

    void MenuBar::draw_windows(Sim &sim, const MenuActions &actions)
    {
        scenario_editor.draw(scenario_editor_open, sim, dialog, actions);
        base_editor.draw(base_editor_open, sim, dialog);
        dialog.draw();
        if (aircraft_autopilot_open)
            draw_aircraft_autopilot(sim, aircraft_autopilot_open);
        if (spacecraft_autopilot_open)
            draw_spacecraft_autopilot(sim, spacecraft_autopilot_open);
        if (transfer_windows_open)
            draw_transfer_windows(sim, actions, transfer_windows_open);
        if (help_open)
        {
            ImGui::SetNextWindowPos(ImVec2(80.0f, 100.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Controls", &help_open, ImGuiWindowFlags_AlwaysAutoResize))
                ImGui::TextUnformatted(control_help);
            ImGui::End();
        }
        if (about_open)
        {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.4f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("About Kosmos", &about_open, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Kosmos %s - a space flight simulator", KOSMOS_VERSION);
                ImGui::TextDisabled("Built on simlib");
            }
            ImGui::End();
        }
    }

    void MenuBar::scenario_menu(const std::vector<Scenario> &all, const std::string &prefix, const Scenario *&chosen)
    {
        const std::string head = prefix.empty() ? std::string() : prefix + "/";
        std::vector<std::string> children;
        for (const Scenario &sc : all)
        {
            if (sc.group.compare(0, head.size(), head) != 0 || sc.group.size() <= head.size() || (!prefix.empty() && sc.group == prefix))
                continue;
            const std::string rest = sc.group.substr(head.size());
            const std::string child = rest.substr(0, rest.find('/'));
            if (std::find(children.begin(), children.end(), child) == children.end())
                children.push_back(child);
        }
        for (const std::string &child : children)
            if (ImGui::BeginMenu(child.c_str()))
            {
                scenario_menu(all, head + child, chosen);
                ImGui::EndMenu();
            }
        for (const Scenario &sc : all)
            if (sc.group == prefix && ImGui::MenuItem(sc.name.c_str()))
                chosen = &sc;
    }

    namespace
    {
        std::string date_after(Sim &sim, double seconds)
        {
            int y = 0, mo = 0, d = 0;
            double hours = 0.0;
            civil_from_julian(sim.universe.julian_date() + seconds / 86400.0, y, mo, d, hours);
            const int total_min = static_cast<int>(hours * 60.0);
            char buf[48];
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", y, mo, d, total_min / 60, total_min % 60);
            return buf;
        }

        std::string span_text(double s)
        {
            char buf[48];
            const double a = std::fabs(s);
            if (a >= 86400.0)
                std::snprintf(buf, sizeof buf, "%.1f d", a / 86400.0);
            else if (a >= 3600.0)
                std::snprintf(buf, sizeof buf, "%dh %02dm", static_cast<int>(a / 3600.0), static_cast<int>(std::fmod(a, 3600.0) / 60.0));
            else
                std::snprintf(buf, sizeof buf, "%dm %02ds", static_cast<int>(a / 60.0), static_cast<int>(std::fmod(a, 60.0)));
            return buf;
        }

        // Skips to `lead` seconds before the ignition `ignition_in` seconds ahead; the reason goes to the log when it cannot.
        void skip_to_burn(Sim &sim, const MenuActions &actions, double ignition_in, bool follow)
        {
            std::string why;
            if (actions.multiplayer && actions.multiplayer())
                why = "The clock cannot be skipped in a multiplayer session";
            else if (sim.skip_to_burn(ignition_in, follow, why))
                return;
            sim.log(why);
        }

        std::string lead_text(Sim &sim)
        {
            return span_text(sim.burn_lead_seconds());
        }
    } // namespace

    void file_menu_time_skip(Sim &sim, const MenuActions &actions, bool &windows_open)
    {
        const Sim::PlannedBurn burn = sim.planned_burn();
        const double skip = burn.ignition_in - sim.burn_lead_seconds();
        std::string blocked;
        if (!burn.valid)
            blocked = burn.why;
        else if (actions.multiplayer && actions.multiplayer())
            blocked = "Not available in multiplayer";
        else if (skip <= 0.0)
            blocked = "Already within the lead time of the burn";
        const std::string label = "Set time to T-" + lead_text(sim) + " before burn";
        if (ImGui::MenuItem(label.c_str(), nullptr, false, blocked.empty()))
            skip_to_burn(sim, actions, burn.ignition_in, true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip();
            if (blocked.empty())
                ImGui::Text("%s: ignition in %s, on %s.\nSkips to %s.", burn.source.c_str(), span_text(burn.ignition_in).c_str(),
                            date_after(sim, burn.ignition_in).c_str(), date_after(sim, skip).c_str());
            else
                ImGui::TextUnformatted(blocked.c_str());
            ImGui::EndTooltip();
        }
        if (ImGui::MenuItem("Transfer windows..."))
            windows_open = true;
    }

    void draw_transfer_windows(Sim &sim, const MenuActions &actions, bool &open)
    {
        ImGui::SetNextWindowPos(ImVec2(120.0f, 120.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Transfer windows", &open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }
        int lead = std::clamp(sim.props.get_int("/sim/settings/burn-lead-min", 5), 0, 120);
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputInt("Lead (minutes before ignition, at least 5)", &lead))
            sim.props.set_int("/sim/settings/burn-lead-min", std::clamp(lead, 0, 120));
        std::string why;
        const std::vector<Sim::TransferWindow> windows = sim.transfer_windows(8, why);
        if (windows.empty())
            ImGui::TextDisabled("%s", why.c_str());
        else
        {
            ImGui::TextDisabled("Hohmann departures to %s (circular, coplanar estimate)", sim.props.get_string("/sim/target").c_str());
            if (ImGui::BeginTable("windows", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
            {
                for (const char *h : {"Ignition", "In", "dV", "Burn", "Arrival", ""})
                    ImGui::TableSetupColumn(h);
                ImGui::TableHeadersRow();
                int id = 0;
                for (const Sim::TransferWindow &w : windows)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(date_after(sim, w.ignition_in).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(span_text(w.ignition_in).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f m/s", w.dv1);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(w.burn_length > 0.0 ? span_text(w.burn_length).c_str() : "--");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(date_after(sim, w.ignition_in + w.burn_length / 2.0 + w.flight_time).c_str());
                    ImGui::TableNextColumn();
                    ImGui::PushID(id++);
                    const bool multi = actions.multiplayer && actions.multiplayer();
                    ImGui::BeginDisabled(multi || w.ignition_in - sim.burn_lead_seconds() <= 0.0);
                    if (ImGui::SmallButton(("Go to T-" + lead_text(sim)).c_str()))
                        skip_to_burn(sim, actions, w.ignition_in, false);
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    void MenuBar::menu_file(Sim &sim, const MenuActions &actions)
    {
        if (ImGui::BeginMenu("Open scenario"))
        {
            // The folders are re-read while the menu is open (at most once a second), so edits made outside Kosmos appear.
            const double now = ImGui::GetTime();
            if (now - scenario_scan_time > 1.0)
            {
                sim.refresh_scenarios();
                scenario_scan_time = now;
            }
            const std::vector<Scenario> &all = sim.scenarios();
            const Scenario *chosen = nullptr;
            scenario_menu(all, "", chosen);
            ImGui::EndMenu();
            if (chosen && actions.start_scenario)
            {
                const Scenario copy = *chosen;
                actions.start_scenario(copy);
            }
        }
        if (ImGui::MenuItem("Open scenario file..."))
            dialog.show(FileDialog::Mode::open, "Open scenario", user_folder("scenarios"), "", [this, &actions](const std::string &path) {
                Scenario sc;
                std::string error;
                if (!load_scenario(path, sc, error))
                    std::fprintf(stderr, "kosmos: %s\n", error.c_str());
                else if (actions.start_scenario)
                    actions.start_scenario(sc);
            });
        if (ImGui::MenuItem("Save scenario..."))
        {
            Scenario sc = capture_scenario(sim, "My scenario");
            dialog.show(FileDialog::Mode::save, "Save scenario", user_folder("scenarios"), "my_scenario.json", [&sim, &actions, sc](const std::string &path) mutable {
                sc.name = fs::path(path).stem().string();
                std::string error;
                if (save_scenario(path, sc, error))
                {
                    if (actions.scenario_saved)
                        actions.scenario_saved(path);
                    sim.load_user_content();
                }
                else
                    std::fprintf(stderr, "kosmos: %s\n", error.c_str());
            });
        }
        ImGui::Separator();
        if (actions.flight_recording && actions.flight_recording())
        {
            if (ImGui::MenuItem("Stop flight recording") && actions.stop_flight_recording)
                actions.stop_flight_recording();
        }
        else if (actions.unsaved_flight_recording && actions.unsaved_flight_recording())
        {
            if (ImGui::MenuItem("Retry saving flight recording...") && actions.save_unsaved_flight_recording)
                dialog.show(FileDialog::Mode::save, "Save flight recording", user_folder("recordings"), "flight-recovered.json",
                            [&actions](const std::string &path) { actions.save_unsaved_flight_recording(path); });
        }
        else if (ImGui::MenuItem("Record flight...") && actions.start_flight_recording)
            dialog.show(FileDialog::Mode::save, "Record flight", user_folder("recordings"), "flight.json",
                        [&actions](const std::string &path) { actions.start_flight_recording(path); });
        if (ImGui::MenuItem("Open flight recording...") && actions.open_flight_recording)
            dialog.show(FileDialog::Mode::open, "Open flight recording", user_folder("recordings"), "",
                        [&actions](const std::string &path) { actions.open_flight_recording(path); });
        ImGui::Separator();
        file_menu_time_skip(sim, actions, transfer_windows_open);
        ImGui::Separator();
        if (ImGui::MenuItem("Restart scenario", "R") && actions.restart)
            actions.restart();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Esc") && actions.quit)
            actions.quit();
        ImGui::EndMenu();
    }
    void MenuBar::menu_edit(Sim &sim, std::vector<MenuWindow> &windows, const MenuActions &actions)
    {
        if (ImGui::MenuItem("Undo", nullptr, false, true))
        {
            if (base_editor_open)
                base_editor.undo(sim);
            else
                scenario_editor.undo();
        }
        if (ImGui::MenuItem("Redo", nullptr, false, true))
        {
            if (base_editor_open)
                base_editor.redo(sim);
            else
                scenario_editor.redo();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Camera"))
        {
            const std::string mode = sim.props.get_string("/sim/settings/camera-mode", "dynamic");
            if (ImGui::MenuItem("Standard Camera", nullptr, mode == "standard"))
                sim.props.set_string("/sim/settings/camera-mode", "standard");
            if (ImGui::MenuItem("Dynamic Camera", nullptr, mode == "dynamic"))
                sim.props.set_string("/sim/settings/camera-mode", "dynamic");
            if (mode == "dynamic")
            {
                int strength = sim.props.get_int("/sim/settings/camera-strength", 100);
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::SliderInt("Strength", &strength, 0, 200, "%d%%"))
                    sim.props.set_int("/sim/settings/camera-strength", strength);
            }
            ImGui::TextDisabled("Dynamic: the cockpit eye sways with acceleration.");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        for (MenuWindow &w : windows)
            if ((w.label == "Settings" || w.label == "Performance settings" || w.label == "Properties") && w.open && ImGui::MenuItem((w.label + "...").c_str()))
                *w.open = true;
        ImGui::EndMenu();
    }
    void MenuBar::menu_view(Sim &sim, std::vector<MenuWindow> &windows, const MenuActions &actions)
    {
        if (actions.toggle_eva && ImGui::MenuItem(actions.on_foot && actions.on_foot() ? "Board ship / enter building" : actions.in_building && actions.in_building() ? "Leave building" : "Leave ship (EVA / walk)", "Enter"))
            actions.toggle_eva();
        ImGui::Separator();
        for (MenuWindow &w : windows)
            if (w.open)
                ImGui::MenuItem(w.label.c_str(), w.key.empty() ? nullptr : w.key.c_str(), w.open);
        ImGui::Separator();
        if (ImGui::BeginMenu("Fuel transfer", sim.props.get_bool("/dock/docked")))
        {
            const std::string mode = sim.props.get_string("/dock/transfer", "auto");
            ImGui::Text("Partners hold %.0f of %.0f kg", sim.props.get_double("/dock/partner-fuel"), sim.props.get_double("/dock/partner-capacity"));
            ImGui::Separator();
            const std::pair<const char *, const char *> modes[] = {{"auto", "Automatic (tank pods top up this ship)"}, {"off", "Off"}, {"give", "Pump to docked ships"}, {"take", "Pump from docked ships"}};
            for (const auto &m : modes)
                if (ImGui::MenuItem(m.second, nullptr, mode == m.first))
                    sim.props.set_string("/dock/transfer", m.first);
            ImGui::EndMenu();
        }
        {
            bool clock = sim.props.get_bool("/sim/settings/clock");
            if (ImGui::MenuItem("Warp and time display", nullptr, &clock))
                sim.props.set_bool("/sim/settings/clock", clock);
        }
        ImGui::Separator();
        {
            const Ship *flown = sim.active_ship();
            const auto panels = flown ? flown->spec().cockpit.panels_2d() : std::vector<Cockpit::Panel2D>();
            if (ImGui::BeginMenu("2D Panels", !panels.empty()))
            {
                for (std::size_t pi = 0; pi < panels.size(); ++pi)
                    if (ImGui::MenuItem(panels[pi].name.c_str()) && actions.show_panel)
                        actions.show_panel(static_cast<int>(pi));
                ImGui::EndMenu();
            }
            const auto &anims = flown ? flown->spec().animation_controls : std::vector<ShipSpec::AnimationControl>();
            if (ImGui::BeginMenu("Animations", !anims.empty()))
            {
                for (std::size_t ai = 0; ai < anims.size(); ++ai)
                {
                    const std::string path = "/controls/anim/" + anims[ai].property;
                    const double progress = flown->animation_position(ai);
                    bool on = (sim.props.exists(path) ? sim.props.get_double(path) : progress) > 0.5;
                    char label[160];
                    std::snprintf(label, sizeof label, "%s (%s)", anims[ai].name.c_str(), progress >= 0.999 ? "open" : progress <= 0.001 ? "closed" : on ? "opening" : "closing");
                    if (ImGui::MenuItem(label, nullptr, &on))
                        sim.props.set_double(path, on ? 1.0 : 0.0);
                }
                ImGui::EndMenu();
            }
        }
        if (ImGui::BeginMenu("MFD modes", sim.mfd.unit_count() > 0))
        {
            const char *slots[2] = {"Left MFD", "Right MFD"};
            const auto &modes = sim.mfd.modes();
            // A panel layout can list its instruments in any order, so name the units by where they sit on screen.
            const Ship *flown = sim.active_ship();
            const auto &placed = flown ? flown->spec().cockpit.instruments : std::vector<Cockpit::PanelInstrument>();
            const bool swapped = placed.size() >= 2 && placed[0].x > placed[1].x;
            for (int slot = 0; slot < 2 && slot < sim.mfd.unit_count(); ++slot)
            {
                const int side = swapped ? 1 - slot : slot;
                const int current = sim.mfd.unit(side).mode;
                const std::string title = std::string(slots[slot]) + ": " + (current >= 0 && current < static_cast<int>(modes.size()) ? modes[static_cast<std::size_t>(current)].name : std::string("none"));
                if (ImGui::BeginMenu(title.c_str()))
                {
                    for (int m = 0; m < static_cast<int>(modes.size()); ++m)
                        if (sim.mfd.allowed(m) && ImGui::MenuItem(modes[static_cast<std::size_t>(m)].name.c_str(), nullptr, m == current))
                            sim.mfd.set_mode(side, m);
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        {
            bool nav = sim.props.get_bool("/controls/lighting/nav", false), lamps = sim.props.get_bool("/controls/lighting/lamps", true);
            if (ImGui::MenuItem("Navigation lights", actions.key_label ? actions.key_label("nav_lights").c_str() : nullptr, &nav))
            {
                sim.props.set_bool("/controls/lighting/nav", nav);
                sim.props.set_bool("/controls/lighting/strobe", nav);
            }
            if (ImGui::MenuItem("Landing and nose lights", actions.key_label ? actions.key_label("lamps").c_str() : nullptr, &lamps))
                sim.props.set_bool("/controls/lighting/lamps", lamps);
        }
        ImGui::Separator();
        ImGui::MenuItem("Scenario editor", nullptr, &scenario_editor_open);
        ImGui::MenuItem("Base editor", nullptr, &base_editor_open);
        ImGui::Separator();
        ImGui::MenuItem("Keep menu bar visible", nullptr, &keep_visible);
        if (ImGui::MenuItem("Full screen", "Alt+Enter", sl::is_fullscreen()))
            sl::toggle_fullscreen();
        ImGui::EndMenu();
    }

    void MenuBar::draw(Sim &sim, std::vector<MenuWindow> &windows, const MenuActions &actions)
    {
        ImGuiIO &io = ImGui::GetIO();
        const float height = ImGui::GetFrameHeight();
        const bool mouse_known = io.MousePos.x > -1.0e6f;
        const bool at_top = mouse_known && io.MousePos.y >= 0.0f && io.MousePos.y <= 2.0f;
        const bool near_bar = mouse_known && io.MousePos.y >= 0.0f && io.MousePos.y <= height + 8.0f;
        const bool want = keep_visible || menu_open_ || at_top || (slide_ > 0.01f && near_bar);
        const float step = io.DeltaTime * 7.0f;
        slide_ = want ? std::min(1.0f, slide_ + step) : std::max(0.0f, slide_ - step);
        if (close_menus_)
        {
            ImGui::ClosePopupToLevel(0, true);
            close_menus_ = false;
        }
        if (slide_ <= 0.001f)
            return;

        menu_open_ = false;
        const float eased = 1.0f - (1.0f - slide_) * (1.0f - slide_);
        ImGui::SetNextWindowPos(ImVec2(0.0f, -height * (1.0f - eased)));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, height));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("##kosmos_menubar", nullptr, flags))
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    menu_open_ = true;
                    menu_file(sim, actions);
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    menu_open_ = true;
                    menu_edit(sim, windows, actions);
                }
                if (ImGui::BeginMenu("View"))
                {
                    menu_open_ = true;
                    menu_view(sim, windows, actions);
                }
                if (ImGui::BeginMenu("Autopilot"))
                {
                    menu_open_ = true;
                    ImGui::MenuItem("Aircraft...", nullptr, &aircraft_autopilot_open);
                    ImGui::MenuItem("Spacecraft...", nullptr, &spacecraft_autopilot_open);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Tools"))
                {
                    menu_open_ = true;
                    ImGui::MenuItem("Scenario editor", nullptr, &scenario_editor_open);
                    ImGui::MenuItem("Base editor", nullptr, &base_editor_open);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Screenshot") && actions.screenshot)
                        actions.screenshot();
                    if (ImGui::MenuItem("Scenario thumbnail") && actions.scenario_thumbnail)
                        actions.scenario_thumbnail();
                    if (actions.toggle_recording && ImGui::MenuItem(actions.recording && actions.recording() ? "Stop recording" : "Start recording video"))
                        actions.toggle_recording();
                    if (ImGui::MenuItem("Reload user scenarios and bases"))
                        sim.load_user_content();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help"))
                {
                    menu_open_ = true;
                    ImGui::MenuItem("Controls", nullptr, &help_open);
                    ImGui::MenuItem("About Kosmos", nullptr, &about_open);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
} // namespace kosmos
