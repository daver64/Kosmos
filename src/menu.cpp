#include "menu.h"

#include <imgui.h>
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
        sc.active = std::max(0, sim.props.get_int("/sim/active-ship"));
        sc.target = sim.props.get_string("/sim/target");
        for (int i = 0; i < NavSystem::receivers; ++i)
            sc.nav.push_back(sim.props.get_double("/nav/nav[" + std::to_string(i) + "]/frequency"));
        for (int si = 0; si < sim.ship_count(); ++si)
        {
            Ship &ship = *sim.ship(si);
            const Telemetry &t = ship.telemetry();
            ScenarioShip s;
            s.ship = ship.spec().name;
            if (ship.name != ship.spec().name)
                s.label = ship.name;
            const int soi = std::clamp(t.soi, 0, static_cast<int>(sim.universe.bodies.size()) - 1);
            s.body = sim.universe.bodies[static_cast<std::size_t>(soi)].name;
            // A ship low and slow over the ground is "surface"; anything else is saved as a circular orbit.
            const bool near_ground = t.landed || (t.altitude < 30000.0 && t.surface_speed < 400.0);
            if (near_ground)
            {
                s.landed = true;
                s.latitude = t.latitude;
                s.longitude = t.longitude;
                s.heading = t.heading;
                if (!t.landed)
                {
                    s.altitude = std::max(1.0, t.altitude);
                    s.speed = t.surface_speed;
                }
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
            sc.ships.push_back(std::move(s));
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
            if (ImGui::Button("From flight"))
            {
                set(capture_scenario(sim, scenario_.name.empty() ? "My scenario" : scenario_.name));
                status_ = "Captured the ships as they are now.";
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Replace this scenario with the live positions of every ship. Orbits are saved as circular.");
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

            text("Name", scenario_.name);
            text("File id", scenario_.id);
            char desc[1024];
            std::snprintf(desc, sizeof desc, "%s", scenario_.description.c_str());
            if (ImGui::InputTextMultiline("Description", desc, sizeof desc, ImVec2(-FLT_MIN, 52.0f)))
                scenario_.description = desc;
            ImGui::SliderInt("Flown ship", &scenario_.active, 0, std::max(0, static_cast<int>(scenario_.ships.size()) - 1));
            text("Target", scenario_.target);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("A ship or body name for the Target MFD; empty for none");
            for (std::size_t i = 0; i < 2; ++i)
            {
                if (scenario_.nav.size() <= i)
                    scenario_.nav.resize(2, 0.0);
                dbl(i == 0 ? "NAV1 MHz" : "NAV2 MHz", scenario_.nav[i], 0.05f, 0.0, 0.0, "%.2f");
            }
            while (!scenario_.nav.empty() && scenario_.nav.back() == 0.0)
                scenario_.nav.pop_back();

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
            int ship_index = index_of(ships, s.ship);
            if (combo("Ship", ship_index, ships) && ship_index >= 0)
                s.ship = ships[static_cast<std::size_t>(ship_index)];
            text("Label", s.label);
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
            }
            else
            {
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

    void BaseEditor::draw(bool &open, Sim &sim, FileDialog &dialog)
    {
        if (!open)
            return;
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
            combo("Base", base_, ids);
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
                    text("Id", b.id);
                    text("Name", b.name);
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
                            b.direction = base_direction(sim.universe, body, b);
                    }
                    dbl("Level radius m", b.flat_radius, 1.0f, 0.0, 20000.0, "%.0f");
                    dbl("Blend m", b.blend, 1.0f, 0.0, 20000.0, "%.0f");
                    dbl("Elevation m", b.elevation, 1.0f, -500.0, 9000.0, "%.0f");
                    ImGui::SeparatorText("Landing pad");
                    dbl("Pad x", b.pad_x, 0.5f);
                    dbl("Pad z", b.pad_z, 0.5f);
                    dbl("Pad heading", b.pad_heading, 1.0f, -360.0, 360.0, "%.0f deg");
                    if (ImGui::Button("Pad at flown ship"))
                        ship_xz(b.pad_x, b.pad_z);
                    ImGui::TextWrapped("Objects and cameras change at once. Moving the site or its levelled ground takes effect after a restart.");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Objects"))
                {
                    BaseObject fresh;
                    fresh.type = "house";
                    list_box("objects", b.objects, object_, [](const BaseObject &o) {
                        char buf[96];
                        std::snprintf(buf, sizeof buf, "%s  (%.0f, %.0f)%s", o.type.c_str(), o.x, o.z, o.count > 1 ? "  x" : "");
                        return std::string(buf) + (o.count > 1 ? std::to_string(o.count) : std::string());
                    }, fresh);
                    if (!b.objects.empty())
                    {
                        object_ = std::clamp(object_, 0, static_cast<int>(b.objects.size()) - 1);
                        BaseObject &o = b.objects[static_cast<std::size_t>(object_)];
                        ImGui::Separator();
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
                        ImGui::SliderInt("Copies", &o.count, 1, 200);
                        dbl("Step x", o.step_x, 0.5f);
                        dbl("Step z", o.step_z, 0.5f);
                        ImGui::SeparatorText("Size (0 keeps the default)");
                        dbl("Size", o.size, 0.1f, 0.0, 1000.0, "%.2f");
                        dbl("Width", o.width, 0.1f, 0.0, 1000.0, "%.2f");
                        dbl("Depth", o.depth, 0.1f, 0.0, 1000.0, "%.2f");
                        dbl("Height", o.height, 0.1f, 0.0, 1000.0, "%.2f");
                        dbl("Length", o.length, 0.1f, 0.0, 1000.0, "%.2f");
                        dbl("Radius", o.radius, 0.1f, 0.0, 1000.0, "%.2f");
                        ImGui::SliderInt("Number", &o.number, 0, 100);
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
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Runways"))
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
                if (ImGui::BeginTabItem("Navaids"))
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
                if (ImGui::BeginTabItem("Cameras"))
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
                                   "  Z / X            full / zero throttle\n"
                                   "  Shift / Ctrl     throttle up / down\n"
                                   "  Space            stage\n"
                                   "  W A S D Q E      rotate (pitch, yaw, roll)\n"
                                   "  Arrows           RCS translation\n"
                                   "  T                cycle attitude hold\n"
                                   "  P                autopilot\n"
                                   "  U                undock\n"
                                   "  R                restart scenario\n"
                                   "View\n"
                                   "  C                chase / cockpit / 3D cockpit\n"
                                   "  V                switch ship\n"
                                   "  Right-drag       look around\n"
                                   "  [ ]              zoom\n"
                                   "  O                orbit line\n"
                                   "  Tab / G          map / galaxy map\n"
                                   "Time\n"
                                   "  , .              slower / faster time warp\n"
                                   "Windows\n"
                                   "  F2 cameras   F3 settings   F4 properties   F11 Lua\n"
                                   "  F5-F9 MFDs   F10 next scenario\n"
                                   "  Move the mouse to the top of the screen for the menu bar.\n"
                                   "  Esc closes menus and windows; with none open it quits.";
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
        for (bool *flag : {&scenario_editor_open, &base_editor_open, &help_open, &about_open})
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

    void MenuBar::draw_windows(Sim &sim, const MenuActions &actions)
    {
        scenario_editor.draw(scenario_editor_open, sim, dialog, actions);
        base_editor.draw(base_editor_open, sim, dialog);
        dialog.draw();
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

    void MenuBar::draw(Sim &sim, std::vector<MenuWindow> &windows, const MenuActions &actions)
    {
        ImGuiIO &io = ImGui::GetIO();
        const float height = ImGui::GetFrameHeight();
        const bool mouse_known = io.MousePos.x > -1.0e6f;
        const bool at_top = mouse_known && io.MousePos.y >= 0.0f && io.MousePos.y <= 2.0f;
        const bool near_bar = mouse_known && io.MousePos.y >= 0.0f && io.MousePos.y <= height + 8.0f;
        const bool popups = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        const bool want = keep_visible || popups || at_top || (slide_ > 0.01f && near_bar);
        const float step = io.DeltaTime * 7.0f;
        slide_ = want ? std::min(1.0f, slide_ + step) : std::max(0.0f, slide_ - step);
        menu_open_ = popups;
        if (close_menus_)
        {
            ImGui::ClosePopupToLevel(0, true);
            close_menus_ = false;
        }
        if (slide_ <= 0.001f)
            return;

        const float eased = 1.0f - (1.0f - slide_) * (1.0f - slide_);
        ImGui::SetNextWindowPos(ImVec2(0.0f, -height * (1.0f - eased)));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, height));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("##kosmos_menubar", nullptr, flags))
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::BeginMenu("Open scenario"))
                    {
                        for (const Scenario &sc : sim.scenarios())
                            if (ImGui::MenuItem(sc.name.c_str()) && actions.start_scenario)
                                actions.start_scenario(sc);
                        ImGui::EndMenu();
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
                        dialog.show(FileDialog::Mode::save, "Save scenario", user_folder("scenarios"), "my_scenario.json", [&sim, sc](const std::string &path) mutable {
                            sc.name = fs::path(path).stem().string();
                            std::string error;
                            if (save_scenario(path, sc, error))
                                sim.load_user_content();
                            else
                                std::fprintf(stderr, "kosmos: %s\n", error.c_str());
                        });
                    }
                    if (ImGui::MenuItem("Restart scenario", "R") && actions.restart)
                        actions.restart();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Quit", "Esc") && actions.quit)
                        actions.quit();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
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
                    for (MenuWindow &w : windows)
                        if ((w.label == "Settings" || w.label == "Properties") && w.open && ImGui::MenuItem((w.label + "...").c_str()))
                            *w.open = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View"))
                {
                    for (MenuWindow &w : windows)
                        if (w.open)
                            ImGui::MenuItem(w.label.c_str(), w.key.empty() ? nullptr : w.key.c_str(), w.open);
                    ImGui::Separator();
                    ImGui::MenuItem("Scenario editor", nullptr, &scenario_editor_open);
                    ImGui::MenuItem("Base editor", nullptr, &base_editor_open);
                    ImGui::Separator();
                    ImGui::MenuItem("Keep menu bar visible", nullptr, &keep_visible);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Tools"))
                {
                    ImGui::MenuItem("Scenario editor", nullptr, &scenario_editor_open);
                    ImGui::MenuItem("Base editor", nullptr, &base_editor_open);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Screenshot") && actions.screenshot)
                        actions.screenshot();
                    if (ImGui::MenuItem("Reload user scenarios and bases"))
                        sim.load_user_content();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help"))
                {
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
