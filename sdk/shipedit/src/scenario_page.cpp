#include "scenario_page.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <set>

namespace shipedit
{
    namespace
    {
        template <class F> void on_signal(GtkWidget *widget, const char *signal, F f)
        {
            auto *fn = new std::function<void()>(std::move(f));
            g_signal_connect_data(widget, signal, G_CALLBACK(+[](GtkWidget *, gpointer p) {
                (*static_cast<std::function<void()> *>(p))();
            }), fn, [](gpointer p, GClosure *) { delete static_cast<std::function<void()> *>(p); }, G_CONNECT_DEFAULT);
        }

        void alert(GtkWindow *window, const std::string &text, GtkMessageType type = GTK_MESSAGE_ERROR)
        {
            GtkWidget *dialog = gtk_message_dialog_new(window, GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", text.c_str());
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }

        GtkWidget *entry(const char *text)
        {
            GtkWidget *e = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(e), text);
            return e;
        }

        std::string text(GtkWidget *e) { return gtk_entry_get_text(GTK_ENTRY(e)); }

        void row(GtkWidget *grid, int n, const char *label, GtkWidget *widget)
        {
            GtkWidget *l = gtk_label_new(label);
            gtk_widget_set_halign(l, GTK_ALIGN_START);
            gtk_grid_attach(GTK_GRID(grid), l, 0, n, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), widget, 1, n, 1, 1);
        }
    }

    class ScenarioPage
    {
    public:
        GtkWindow *window;
        fs::path root, file;
        GtkWidget *box = nullptr, *editor = nullptr;
        GtkTextBuffer *buffer = nullptr;
        bool dirty = false, updating = false;

        ScenarioPage(GtkWindow *parent, const fs::path &root_path) : window(parent), root(root_path)
        {
            box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_container_set_border_width(GTK_CONTAINER(box), 8);
            GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            auto add = [&](const char *name, std::function<void()> fn) {
                GtkWidget *b = gtk_button_new_with_label(name);
                on_signal(b, "clicked", std::move(fn));
                gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
            };
            add("New", [this] { fresh(); });
            add("Open...", [this] { open(); });
            add("Save as...", [this] { save(); });
            add("Scenario settings...", [this] { settings(); });
            add("Add ship / piggyback...", [this] { add_ship(); });
            gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
            GtkWidget *hint = gtk_label_new("Edit the scenario JSON below. Add ship / piggyback creates surface, orbit and docked entries; existing fields are preserved.");
            gtk_widget_set_halign(hint, GTK_ALIGN_START);
            gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
            gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);
            editor = gtk_text_view_new();
            gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor), TRUE);
            buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
            g_signal_connect(buffer, "changed", G_CALLBACK(+[](GtkTextBuffer *, gpointer p) {
                auto *self = static_cast<ScenarioPage *>(p);
                if (!self->updating)
                    self->dirty = true;
            }), this);
            GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_container_add(GTK_CONTAINER(scroll), editor);
            gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
            set(json{{"name", "New scenario"}, {"description", ""}, {"active", 0}, {"ships", json::array()}});
            dirty = false;
        }

        std::string contents() const
        {
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            gchar *p = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
            std::string result(p);
            g_free(p);
            return result;
        }

        void set(const json &value)
        {
            updating = true;
            const std::string data = value.dump(2) + "\n";
            gtk_text_buffer_set_text(buffer, data.c_str(), -1);
            updating = false;
            dirty = true;
        }

        bool parse(json &value)
        {
            try
            {
                value = json::parse(contents());
                if (!value.is_object() || !value.contains("ships") || !value["ships"].is_array())
                    throw std::runtime_error("A scenario must be an object with a ships array.");
                for (const char *key : {"name", "description", "target"})
                    if (value.contains(key) && !value[key].is_string())
                        throw std::runtime_error(std::string(key) + " must be text.");
                if (value.contains("active") && !value["active"].is_number_integer())
                    throw std::runtime_error("active must be an integer ship index.");
                for (const json &ship : value["ships"])
                {
                    if (!ship.is_object() || !ship.contains("ship") || !ship["ship"].is_string() ||
                        (ship.contains("dock") && !ship["dock"].is_object()))
                        throw std::runtime_error("Each ship must have a ship name and an object-valued dock.");
                    if (ship.contains("body") && !ship["body"].is_string())
                        throw std::runtime_error("Ship body must be text.");
                    for (const char *key : {"surface", "orbit"})
                        if (ship.contains(key) && !ship[key].is_object())
                            throw std::runtime_error(std::string("Ship ") + key + " must be an object.");
                    if (ship.contains("dock"))
                        for (const char *key : {"to", "port", "own_port"})
                            if (ship["dock"].contains(key) && !ship["dock"][key].is_number_integer())
                                throw std::runtime_error(std::string("Dock ") + key + " must be an integer.");
                }
                return true;
            }
            catch (const json::exception &e)
            {
                alert(window, std::string("Invalid scenario: ") + e.what());
                return false;
            }
            catch (const std::runtime_error &e)
            {
                alert(window, std::string("Invalid scenario: ") + e.what());
                return false;
            }
        }

        bool discard()
        {
            if (!dirty)
                return true;
            GtkWidget *d = gtk_message_dialog_new(window, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                                   "Discard unsaved scenario changes?");
            const bool yes = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
            gtk_widget_destroy(d);
            return yes;
        }

        void fresh()
        {
            if (!discard()) return;
            set(json{{"name", "New scenario"}, {"description", ""}, {"active", 0}, {"ships", json::array()}});
            file.clear();
            dirty = false;
        }

        void open()
        {
            if (!discard()) return;
            GtkWidget *d = gtk_file_chooser_dialog_new("Open scenario", window, GTK_FILE_CHOOSER_ACTION_OPEN,
                                                        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), (root / "scenarios").string().c_str());
            if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
            {
                gchar *name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
                std::ifstream in(name);
                try
                {
                    if (!in) throw std::runtime_error("Cannot open file.");
                    json j = json::parse(in);
                    if (!j.is_object() || !j.contains("ships") || !j["ships"].is_array())
                        throw std::runtime_error("A scenario must contain a ships array.");
                    set(j);
                    file = name;
                    dirty = false;
                }
                catch (const json::exception &e) { alert(window, std::string("Cannot open scenario: ") + e.what()); }
                catch (const std::runtime_error &e) { alert(window, std::string("Cannot open scenario: ") + e.what()); }
                g_free(name);
            }
            gtk_widget_destroy(d);
        }

        void save()
        {
            json j;
            if (!parse(j)) return;
            if (j["ships"].empty()) { alert(window, "Add at least one ship before saving."); return; }
            const int active = j.value("active", 0);
            if (active < 0 || active >= static_cast<int>(j["ships"].size()))
            {
                alert(window, "The flown ship index is outside the ships list.");
                return;
            }
            std::set<std::pair<int, int>> used_ports;
            for (std::size_t i = 0; i < j["ships"].size(); ++i)
            {
                const json &s = j["ships"][i];
                if (s["ship"].get<std::string>().empty() || (!s.contains("surface") && !s.contains("orbit")))
                {
                    alert(window, "Each ship needs a name and a surface or orbit placement.");
                    return;
                }
                if (s.contains("dock"))
                {
                    const json &dock = s["dock"];
                    const int to = dock.value("to", -1), port = dock.value("port", 0), own = dock.value("own_port", 0);
                    if (to < 0 || to >= static_cast<int>(i) || port < 0 || own < 0 ||
                        !used_ports.insert({to, port}).second || !used_ports.insert({static_cast<int>(i), own}).second)
                    {
                        alert(window, "A docking link has an invalid carrier or port, or a port is already in use.");
                        return;
                    }
                }
            }
            GtkWidget *d = gtk_file_chooser_dialog_new("Save scenario", window, GTK_FILE_CHOOSER_ACTION_SAVE,
                                                        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
            if (!file.empty()) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(d), file.string().c_str());
            else
            {
                gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), (root / "scenarios").string().c_str());
                gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "new_scenario.json");
            }
            if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
            {
                gchar *name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
                const std::string data = contents();
                GError *error = nullptr;
                if (!g_file_set_contents(name, data.c_str(), static_cast<gssize>(data.size()), &error))
                {
                    alert(window, "Could not write scenario: " + std::string(error->message));
                    g_error_free(error);
                }
                else
                {
                    file = name;
                    dirty = false;
                    if (j.contains("thumbnail") && j["thumbnail"].is_string())
                    {
                        const fs::path thumbnail = j["thumbnail"].get<std::string>();
                        std::error_code ec;
                        if (!thumbnail.empty() && thumbnail.is_relative() && !fs::exists(file.parent_path() / thumbnail, ec))
                            alert(window, "The saved scenario's relative thumbnail was not found beside it. Update the thumbnail path or copy the image.", GTK_MESSAGE_WARNING);
                    }
                }
                g_free(name);
            }
            gtk_widget_destroy(d);
        }

        void settings()
        {
            json j;
            if (!parse(j)) return;
            GtkWidget *d = gtk_dialog_new_with_buttons("Scenario settings", window, GTK_DIALOG_MODAL,
                                                       "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT, nullptr);
            GtkWidget *grid = gtk_grid_new();
            gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
            gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
            gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
            gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), grid);
            GtkWidget *name = entry(j.value("name", std::string("New scenario")).c_str());
            GtkWidget *description = entry(j.value("description", std::string()).c_str());
            GtkWidget *active = gtk_spin_button_new_with_range(0, std::max(0, static_cast<int>(j["ships"].size()) - 1), 1);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(active), j.value("active", 0));
            GtkWidget *target = entry(j.value("target", std::string()).c_str());
            row(grid, 0, "Name", name);
            row(grid, 1, "Description", description);
            row(grid, 2, "Flown ship index (0-based)", active);
            row(grid, 3, "Target", target);
            gtk_widget_show_all(d);
            if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
            {
                if (text(name).empty()) alert(window, "Enter a scenario name.");
                else
                {
                    j["name"] = text(name);
                    j["description"] = text(description);
                    j["active"] = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(active));
                    j["target"] = text(target);
                    set(j);
                }
            }
            gtk_widget_destroy(d);
        }

        void add_ship()
        {
            json j;
            if (!parse(j)) return;
            struct AvailableShip { std::string name, label; json ports; };
            std::vector<AvailableShip> ships;
            for (const AddonInfo &addon : scan_addons(root / "addons"))
                for (const std::string &filename : addon.ships)
                {
                    Document document;
                    std::string error;
                    if (!document.load(addon, filename, error))
                    {
                        std::fprintf(stderr, "shipedit: cannot load %s/%s: %s\n", addon.id.c_str(), filename.c_str(), error.c_str());
                        continue;
                    }
                    const json ports = document.root().value("ports", json::array());
                    ships.push_back({document.name(), document.name() + " (" + addon.id + "/" + filename + ")",
                                     ports.is_array() ? ports : json::array()});
                }
            if (ships.empty()) { alert(window, "No ships could be loaded from addons."); return; }
            GtkWidget *d = gtk_dialog_new_with_buttons("Add ship / piggyback", window, GTK_DIALOG_MODAL,
                                                       "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_ACCEPT, nullptr);
            GtkWidget *grid = gtk_grid_new();
            gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
            gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
            gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
            gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), grid);
            GtkWidget *ship = gtk_combo_box_text_new(), *body = entry("Earth"), *base = entry("chippenham");
            for (const AvailableShip &s : ships)
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ship), s.label.c_str());
            gtk_combo_box_set_active(GTK_COMBO_BOX(ship), 0);
            GtkWidget *mode = gtk_combo_box_text_new();
            for (const char *s : {"Surface base", "Surface coordinates", "Orbit", "Piggyback (docked)"})
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), s);
            gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
            GtkWidget *alt = gtk_spin_button_new_with_range(0, 100000000, 10);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(alt), 200);
            GtkWidget *leader = gtk_combo_box_text_new();
            for (std::size_t i = 0; i < j["ships"].size(); ++i)
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(leader),
                    (std::to_string(i) + ": " + j["ships"][i].value("ship", std::string("(invalid ship)"))).c_str());
            if (!j["ships"].empty()) gtk_combo_box_set_active(GTK_COMBO_BOX(leader), 0);
            GtkWidget *leader_port = gtk_combo_box_text_new(), *own_port = gtk_combo_box_text_new();
            GtkWidget *latitude = gtk_spin_button_new_with_range(-90, 90, 0.1);
            GtkWidget *longitude = gtk_spin_button_new_with_range(-180, 180, 0.1);
            GtkWidget *heading = gtk_spin_button_new_with_range(0, 360, 1);
            GtkWidget *inclination = gtk_spin_button_new_with_range(-180, 180, 0.1);
            GtkWidget *phase = gtk_spin_button_new_with_range(-360, 360, 0.1);
            gtk_spin_button_set_digits(GTK_SPIN_BUTTON(latitude), 4);
            gtk_spin_button_set_digits(GTK_SPIN_BUTTON(longitude), 4);
            gtk_spin_button_set_digits(GTK_SPIN_BUTTON(inclination), 2);
            gtk_spin_button_set_digits(GTK_SPIN_BUTTON(phase), 2);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(heading), 90);
            auto fill_ports = [](GtkWidget *combo, const json &ports) {
                gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combo));
                for (std::size_t i = 0; i < ports.size(); ++i)
                {
                    const std::string name = ports[i].is_object() ? ports[i].value("name", std::string("Port")) : "Invalid port";
                    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), (std::to_string(i) + ": " + name).c_str());
                }
                if (!ports.empty()) gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
            };
            auto refresh_ports = [&] {
                const int selected = gtk_combo_box_get_active(GTK_COMBO_BOX(ship));
                fill_ports(own_port, selected >= 0 ? ships[static_cast<std::size_t>(selected)].ports : json::array());
                const int carrier = gtk_combo_box_get_active(GTK_COMBO_BOX(leader));
                const std::string name = carrier >= 0 ? j["ships"][static_cast<std::size_t>(carrier)].value("ship", std::string()) : "";
                auto found = std::find_if(ships.begin(), ships.end(), [&](const AvailableShip &s) { return s.name == name; });
                fill_ports(leader_port, found != ships.end() ? found->ports : json::array());
            };
            on_signal(ship, "changed", refresh_ports);
            on_signal(leader, "changed", refresh_ports);
            refresh_ports();
            row(grid, 0, "Ship", ship);
            row(grid, 1, "Placement", mode);
            row(grid, 2, "Body", body);
            row(grid, 3, "Surface base id", base);
            row(grid, 4, "Orbit altitude km", alt);
            row(grid, 5, "Carrier ship", leader);
            row(grid, 6, "Carrier port", leader_port);
            row(grid, 7, "This ship's port", own_port);
            row(grid, 8, "Latitude degrees", latitude);
            row(grid, 9, "Longitude degrees", longitude);
            row(grid, 10, "Heading degrees", heading);
            row(grid, 11, "Inclination degrees", inclination);
            row(grid, 12, "Phase degrees", phase);
            auto refresh_mode = [&] {
                const int placement = gtk_combo_box_get_active(GTK_COMBO_BOX(mode));
                gtk_widget_set_sensitive(base, placement == 0);
                for (GtkWidget *w : {latitude, longitude, heading}) gtk_widget_set_sensitive(w, placement == 1);
                for (GtkWidget *w : {alt, inclination, phase}) gtk_widget_set_sensitive(w, placement == 2);
                for (GtkWidget *w : {leader, leader_port, own_port}) gtk_widget_set_sensitive(w, placement == 3);
                gtk_widget_set_sensitive(body, placement != 3);
            };
            on_signal(mode, "changed", refresh_mode);
            refresh_mode();
            gtk_widget_show_all(d);
            if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
            {
                const int placement = gtk_combo_box_get_active(GTK_COMBO_BOX(mode));
                const int selected = gtk_combo_box_get_active(GTK_COMBO_BOX(ship));
                if (selected < 0 || (placement != 3 && text(body).empty()))
                    alert(window, "Select a ship and enter a body.");
                else if (placement == 0 && text(base).empty())
                    alert(window, "Enter a surface base id.");
                else if (placement == 3 && j["ships"].empty())
                    alert(window, "Add the carrier ship first.");
                else if (placement == 3 && (gtk_combo_box_get_active(GTK_COMBO_BOX(leader_port)) < 0 ||
                                            gtk_combo_box_get_active(GTK_COMBO_BOX(own_port)) < 0))
                    alert(window, "Both ships need an attachment port. Add ports in the ShipEdit tab and save the addons first.");
                else
                {
                    json s = {{"ship", ships[static_cast<std::size_t>(selected)].name}, {"body", text(body)}};
                    if (placement == 0) s["surface"] = {{"base", text(base)}};
                    else if (placement == 1)
                        s["surface"] = {{"latitude", gtk_spin_button_get_value(GTK_SPIN_BUTTON(latitude))},
                                        {"longitude", gtk_spin_button_get_value(GTK_SPIN_BUTTON(longitude))},
                                        {"heading", gtk_spin_button_get_value(GTK_SPIN_BUTTON(heading))}};
                    else if (placement == 2)
                        s["orbit"] = {{"altitude_km", gtk_spin_button_get_value(GTK_SPIN_BUTTON(alt))},
                                      {"inclination", gtk_spin_button_get_value(GTK_SPIN_BUTTON(inclination))},
                                      {"phase", gtk_spin_button_get_value(GTK_SPIN_BUTTON(phase))}};
                    else
                    {
                        const int index = gtk_combo_box_get_active(GTK_COMBO_BOX(leader));
                        std::string error;
                        if (!add_scenario_piggyback(j, ships[static_cast<std::size_t>(selected)].name, index,
                                                    gtk_combo_box_get_active(GTK_COMBO_BOX(leader_port)),
                                                    gtk_combo_box_get_active(GTK_COMBO_BOX(own_port)), error))
                        {
                            alert(window, error);
                            gtk_widget_destroy(d);
                            return;
                        }
                        set(j);
                        gtk_widget_destroy(d);
                        return;
                    }
                    j["ships"].push_back(s);
                    set(j);
                }
            }
            gtk_widget_destroy(d);
        }
    };

    ScenarioPage *create_scenario_page(GtkWindow *window, const fs::path &root) { return new ScenarioPage(window, root); }
    GtkWidget *scenario_page_widget(ScenarioPage *page) { return page->box; }
    bool scenario_page_can_close(ScenarioPage *page) { return page->discard(); }
    void destroy_scenario_page(ScenarioPage *page) { delete page; }
}
