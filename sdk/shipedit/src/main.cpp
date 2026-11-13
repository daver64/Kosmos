// ShipEdit: a GTK 3 editor for Kosmos ships and rocket stacks. It edits a ship.json (every key it does not show is kept as it was).
// Save writes it back into its addon after backing the addon up to shipedit-backups/; Save as new addon writes a separate copy.
//
// shipedit [--root DIR] [--addon ID] [--ship NAME] [--shot FILE [--yaw D] [--pitch D] [--size WxH]]
// --shot draws the preview to a PNG without opening a window.
#include "model.h"
#include "../../gtk_schema.h"
#include <epoxy/gl.h>
#include "gl_preview.h"
#include "preview.h"

#include <gtk/gtk.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>

using namespace shipedit;

namespace
{
#define CB(...) ((GCallback)(__VA_ARGS__))
    enum Col { COL_LABEL, COL_KIND, COL_STAGE, COL_ITEM, COL_COUNT };

    struct Field
    {
        enum Kind { Num, Int, Text, Bool, Vec3, Choice } kind;
        const char *key;
        const char *label;
        std::vector<std::string> choices = {};
    };

    struct App
    {
        fs::path root, addons_dir;
        std::vector<AddonInfo> addons;
        Document doc;
        Document library_doc;
        MeshCache meshes;
        Selection sel;
        PreviewView view;
        std::string last_saved_ship;

        GtkWidget *window = nullptr, *addon_combo = nullptr, *ship_combo = nullptr, *tree_view = nullptr, *area = nullptr, *form_box = nullptr, *gl_area = nullptr;
        GlPreview gl;
        bool use_gl = false; // the ship's surfaces are drawn by the GL widget under the overlay; otherwise cairo draws everything
        GtkWidget *stats_label = nullptr, *problems_label = nullptr, *undo_btn = nullptr, *redo_btn = nullptr, *with_contents = nullptr, *launch_btn = nullptr, *save_btn = nullptr;
        GtkWidget *library_addon = nullptr, *library_ship = nullptr, *library_list = nullptr, *arrange_toggle = nullptr, *overlap_toggle = nullptr;
        GtkTreeStore *store = nullptr;
        bool building = false;
        double drag_x = 0, drag_y = 0;
        int drag_button = 0;
        int drag_stage = -1;
        double stage_drag_start_y = 0, stage_drag_y = 0;
        bool stage_dragged = false;
    };

    App *app = nullptr;

    std::string fmt(double v)
    {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.9g", v);
        return buf;
    }

    template <class F>
    void on_signal(gpointer obj, const char *sig, F f)
    {
        auto *fn = new std::function<void()>(std::move(f));
        g_signal_connect_data(obj, sig, CB(+[](GtkWidget *, gpointer d) { (*static_cast<std::function<void()> *>(d))(); }), fn,
                              [](gpointer d, GClosure *) { delete static_cast<std::function<void()> *>(d); }, static_cast<GConnectFlags>(0));
    }

    void message(const std::string &text, GtkMessageType type = GTK_MESSAGE_INFO)
    {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", text.c_str());
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
    }

    // ---- the node the selection points at ----------------------------------------------------------------------------------------
    json *node_of(const Selection &s)
    {
        json &root = app->doc.root();
        if (s.kind == Selection::ship)
            return &root;
        if (s.stage < 0 || s.stage >= app->doc.stage_count())
            return nullptr;
        json &st = root["stages"][static_cast<std::size_t>(s.stage)];
        if (s.kind == Selection::stage_node)
            return &st;
        const char *list = s.kind == Selection::part ? "parts" : s.kind == Selection::engine ? "engines" : "gear";
        if (!st.contains(list) || !st[list].is_array() || s.item < 0 || s.item >= static_cast<int>(st[list].size()))
            return nullptr;
        return &st[list][static_cast<std::size_t>(s.item)];
    }
    json *cur() { return node_of(app->sel); }

    std::string part_label(const json &p)
    {
        if (p.contains("model") && p["model"].is_string())
            return "Model " + fs::path(p["model"].get<std::string>()).filename().string();
        if (p.contains("prop") && p["prop"].is_string())
            return "Prop " + p["prop"].get<std::string>();
        return "Part";
    }

    void refresh_status();
    void rebuild_form();
    void redraw_preview();

    void select_in_tree(const Selection &want)
    {
        GtkTreeModel *m = GTK_TREE_MODEL(app->store);
        GtkTreeIter it;
        std::function<bool(GtkTreeIter *)> walk = [&](GtkTreeIter *i) {
            do
            {
                int k, st, item;
                gtk_tree_model_get(m, i, COL_KIND, &k, COL_STAGE, &st, COL_ITEM, &item, -1);
                if (k == want.kind && st == want.stage && item == want.item)
                {
                    GtkTreePath *p = gtk_tree_model_get_path(m, i);
                    gtk_tree_view_expand_to_path(GTK_TREE_VIEW(app->tree_view), p);
                    gtk_tree_view_set_cursor(GTK_TREE_VIEW(app->tree_view), p, nullptr, FALSE);
                    gtk_tree_path_free(p);
                    return true;
                }
                GtkTreeIter child;
                if (gtk_tree_model_iter_children(m, &child, i) && walk(&child))
                    return true;
            } while (gtk_tree_model_iter_next(m, i));
            return false;
        };
        if (gtk_tree_model_get_iter_first(m, &it))
            walk(&it);
    }

    void rebuild_tree(Selection want)
    {
        app->building = true;
        gtk_tree_store_clear(app->store);
        auto add = [&](GtkTreeIter *parent, const std::string &label, int kind, int st, int item) {
            GtkTreeIter it;
            gtk_tree_store_append(app->store, &it, parent);
            gtk_tree_store_set(app->store, &it, COL_LABEL, label.c_str(), COL_KIND, kind, COL_STAGE, st, COL_ITEM, item, -1);
            return it;
        };
        GtkTreeIter ship = add(nullptr, "Ship: " + app->doc.name(), Selection::ship, -1, -1);
        const json &stages = app->doc.root().contains("stages") ? app->doc.root()["stages"] : json::array();
        for (std::size_t s = 0; s < stages.size(); ++s)
        {
            const json &st = stages[s];
            GtkTreeIter si = add(&ship, std::to_string(s + 1) + "  " + st.value("name", std::string("Stage")), Selection::stage_node, static_cast<int>(s), -1);
            const json parts = st.value("parts", json::array()), engines = st.value("engines", json::array()), gear = st.value("gear", json::array());
            for (std::size_t i = 0; i < parts.size(); ++i)
                add(&si, part_label(parts[i]), Selection::part, static_cast<int>(s), static_cast<int>(i));
            for (std::size_t i = 0; i < engines.size(); ++i)
                add(&si, "Engine " + engines[i].value("type", std::string("rocket")) + " " + fmt(engines[i].value("thrust", 0.0) / 1000.0) + " kN", Selection::engine, static_cast<int>(s), static_cast<int>(i));
            for (std::size_t i = 0; i < gear.size(); ++i)
                add(&si, "Gear " + std::to_string(i + 1), Selection::gear, static_cast<int>(s), static_cast<int>(i));
        }
        gtk_tree_view_expand_all(GTK_TREE_VIEW(app->tree_view));
        app->building = false;
        app->sel = {Selection::none, -1, -1};
        select_in_tree(want);
        if (app->sel.kind == Selection::none)
        {
            app->sel = {Selection::ship, -1, -1};
            select_in_tree(app->sel);
        }
        rebuild_form();
        refresh_status();
    }

    void structure_changed(Selection want)
    {
        rebuild_tree(want);
        redraw_preview();
    }

    void update_title()
    {
        const std::string t = "ShipEdit - " + app->doc.name() + (app->doc.dirty() ? " *" : "") + (app->doc.has_source() ? "  (" + app->doc.source_addon_id() + ")" : "");
        gtk_window_set_title(GTK_WINDOW(app->window), t.c_str());
    }

    void refresh_status()
    {
        std::string s = "Stage                  dry kg   prop kg  thrust kN   Isp s   dV m/s    TWR\n";
        const auto stats = app->doc.stats();
        const json &stages = app->doc.root().contains("stages") ? app->doc.root()["stages"] : json::array();
        for (std::size_t i = 0; i < stats.size() && i < stages.size(); ++i)
        {
            char line[200];
            std::snprintf(line, sizeof line, "%-18.18s %10.0f %10.0f %10.1f %7.0f %8.0f %7.2f%s\n", stages[i].value("name", std::string("Stage")).c_str(), stats[i].dry, stats[i].propellant,
                          stats[i].thrust / 1000.0, stats[i].isp, stats[i].delta_v, stats[i].twr, stats[i].air_breathing ? " (air)" : "");
            s += line;
        }
        gtk_label_set_text(GTK_LABEL(app->stats_label), s.c_str());
        std::string p;
        for (const std::string &v : app->doc.validate())
            p += "- " + v + "\n";
        gtk_label_set_text(GTK_LABEL(app->problems_label), p.empty() ? "No problems found." : p.c_str());
        gtk_widget_set_sensitive(app->undo_btn, app->doc.can_undo());
        gtk_widget_set_sensitive(app->redo_btn, app->doc.can_redo());
        gtk_widget_set_sensitive(app->launch_btn, !app->last_saved_ship.empty());
        gtk_widget_set_sensitive(app->save_btn, app->doc.has_source());
        update_title();
    }

    // A value edit that does not change the tree's shape: fix the label, the numbers and the picture.
    void value_changed()
    {
        GtkTreeSelection *ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree_view));
        GtkTreeModel *m;
        GtkTreeIter it;
        if (gtk_tree_selection_get_selected(ts, &m, &it))
        {
            std::string label;
            json *n = cur();
            if (n)
            {
                switch (app->sel.kind)
                {
                case Selection::ship: label = "Ship: " + app->doc.name(); break;
                case Selection::stage_node: label = std::to_string(app->sel.stage + 1) + "  " + n->value("name", std::string("Stage")); break;
                case Selection::part: label = part_label(*n); break;
                case Selection::engine: label = "Engine " + n->value("type", std::string("rocket")) + " " + fmt(n->value("thrust", 0.0) / 1000.0) + " kN"; break;
                default: label = "Gear " + std::to_string(app->sel.item + 1); break;
                }
                gtk_tree_store_set(app->store, &it, COL_LABEL, label.c_str(), -1);
            }
        }
        refresh_status();
        redraw_preview();
    }

    void commit_value(const char *key, const json &value, bool erase)
    {
        json *n = cur();
        if (!n)
            return;
        const char *member = std::strchr(key, '.');
        if (member && app->sel.kind == Selection::ship)
        {
            const std::string group(key, member);
            const char *child = member + 1;
            if (n->contains(group) && !(*n)[group].is_object())
            {
                message(group + " must be an object before editing its settings.", GTK_MESSAGE_ERROR);
                return;
            }
            if (erase && (!n->contains(group) || !(*n)[group].contains(child)))
                return;
            if (!erase && n->contains(group) && (*n)[group].contains(child) && (*n)[group][child] == value)
                return;
            app->doc.checkpoint();
            if (erase)
                (*n)[group].erase(child);
            else
                (*n)[group][child] = value;
            value_changed();
            return;
        }
        if (erase)
        {
            if (!n->contains(key))
                return;
            app->doc.checkpoint();
            n->erase(key);
        }
        else
        {
            if (n->contains(key) && (*n)[key] == value)
                return;
            if (app->sel.kind == Selection::stage_node && std::strcmp(key, "z") == 0 && value.is_number())
            {
                app->doc.set_stage_z(app->sel.stage, value.get<double>(), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->with_contents)));
                value_changed();
                return;
            }
            app->doc.checkpoint();
            (*n)[key] = value;
        }
        if (std::strcmp(key, "model") == 0)
            app->meshes.clear();
        value_changed();
    }

    bool parse_num(const char *text, double &out)
    {
        char *end = nullptr;
        out = std::strtod(text, &end);
        return end != text && *end == '\0';
    }

    // ---- property forms ------------------------------------------------------------------------------------------------------------
    void add_field(GtkWidget *grid, int row, const Field &f)
    {
        json *n = cur();
        if (!n)
            return;
        const std::string key = f.key;
        GtkWidget *lab = gtk_label_new(f.label);
        gtk_widget_set_halign(lab, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), lab, 0, row, 1, 1);
        const bool is_vec_node = n->is_array(); // gear: the node is the vector
        // A field commits only while the node it was built for is still selected: clicking another tree item moves the
        // selection first and then the old entry loses focus, which would otherwise write its text into the new node.
        const Selection at = app->sel;
        auto here = [at] { return app->sel.kind == at.kind && app->sel.stage == at.stage && app->sel.item == at.item; };
        const json *values = n;
        std::string field_key = key;
        if (const std::size_t dot = key.find('.'); dot != std::string::npos && app->sel.kind == Selection::ship)
        {
            const std::string group = key.substr(0, dot);
            values = n->contains(group) && (*n)[group].is_object() ? &(*n)[group] : nullptr;
            field_key = key.substr(dot + 1);
        }
        const json *cv = is_vec_node ? n : (values && values->contains(field_key) ? &(*values)[field_key] : nullptr);

        if (f.kind == Field::Bool)
        {
            GtkWidget *c = gtk_check_button_new();
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), cv && cv->is_boolean() ? cv->get<bool>() : key == "cockpit.mfds" && values);
            on_signal(c, "toggled", [c, key] { commit_value(key.c_str(), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c)) != 0, false); });
            gtk_grid_attach(GTK_GRID(grid), c, 1, row, 1, 1);
        }
        else if (f.kind == Field::Choice)
        {
            GtkWidget *c = gtk_combo_box_text_new_with_entry();
            for (const std::string &s : f.choices)
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), s.c_str());
            GtkWidget *e = gtk_bin_get_child(GTK_BIN(c));
            gtk_entry_set_text(GTK_ENTRY(e), cv && cv->is_string() ? cv->get<std::string>().c_str() : "");
            auto apply = [e, key, here] {
                if (!here())
                    return;
                const std::string t = gtk_entry_get_text(GTK_ENTRY(e));
                if (t.empty())
                    commit_value(key.c_str(), json(), true);
                else
                    commit_value(key.c_str(), t, false);
            };
            on_signal(c, "changed", apply);
            gtk_grid_attach(GTK_GRID(grid), c, 1, row, 1, 1);
        }
        else if (f.kind == Field::Vec3)
        {
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
            auto entries = std::make_shared<std::vector<GtkWidget *>>();
            for (int i = 0; i < 3; ++i)
            {
                GtkWidget *e = gtk_entry_new();
                gtk_entry_set_width_chars(GTK_ENTRY(e), 8);
                const char *axis[] = {"x", "y", "z"};
                gtk_widget_set_tooltip_text(e, axis[i]);
                gtk_entry_set_text(GTK_ENTRY(e), cv && cv->is_array() && cv->size() > static_cast<std::size_t>(i) && (*cv)[static_cast<std::size_t>(i)].is_number() ? fmt((*cv)[static_cast<std::size_t>(i)].get<double>()).c_str() : "");
                entries->push_back(e);
                gtk_box_pack_start(GTK_BOX(box), e, TRUE, TRUE, 0);
            }
            auto apply = [entries, key, is_vec_node, here] {
                if (!here())
                    return;
                double v[3];
                bool any = false, all = true;
                for (int i = 0; i < 3; ++i)
                {
                    const char *t = gtk_entry_get_text(GTK_ENTRY((*entries)[static_cast<std::size_t>(i)]));
                    if (*t)
                        any = true;
                    if (!parse_num(t, v[i]))
                        all = false;
                }
                if (!any && !is_vec_node)
                    commit_value(key.c_str(), json(), true);
                else if (all)
                {
                    if (is_vec_node)
                    {
                        json *n = cur();
                        if (n && *n != json::array({v[0], v[1], v[2]}))
                        {
                            app->doc.checkpoint();
                            *n = json::array({v[0], v[1], v[2]});
                            value_changed();
                        }
                    }
                    else
                        commit_value(key.c_str(), json::array({v[0], v[1], v[2]}), false);
                }
            };
            for (GtkWidget *e : *entries)
            {
                on_signal(e, "activate", apply);
                g_signal_connect_data(e, "focus-out-event", CB(+[](GtkWidget *, GdkEvent *, gpointer d) -> gboolean {
                                          (*static_cast<std::function<void()> *>(d))();
                                          return FALSE;
                                      }),
                                      new std::function<void()>(apply), [](gpointer d, GClosure *) { delete static_cast<std::function<void()> *>(d); }, static_cast<GConnectFlags>(0));
            }
            GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            gtk_box_pack_start(GTK_BOX(cell), box, FALSE, FALSE, 0);
            // Nudge sliders: one drag is one undo step. The range covers the whole stack so a part can be carried anywhere along it.
            double zlo = 0, zhi = 0, rmax = 1;
            for (const json &st : app->doc.root().value("stages", json::array()))
            {
                const double z = st.value("z", 0.0), h = st.value("length", 1.0) * 0.5;
                zlo = std::min(zlo, z - h);
                zhi = std::max(zhi, z + h);
                rmax = std::max(rmax, st.value("radius", 1.0));
            }
            for (int i = 0; i < 3; ++i)
            {
                const double cur_v = cv && cv->is_array() && cv->size() > static_cast<std::size_t>(i) && (*cv)[static_cast<std::size_t>(i)].is_number() ? (*cv)[static_cast<std::size_t>(i)].get<double>() : 0.0;
                double lim = i == 2 ? std::max(std::abs(zlo), std::abs(zhi)) + 2.0 : rmax * 3.0 + 2.0;
                if (key == "rotate")
                    lim = 180.0;
                lim = std::max(lim, std::ceil(std::abs(cur_v)) + 1.0);
                GtkWidget *sc = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -lim, lim, 0.01);
                gtk_scale_set_digits(GTK_SCALE(sc), key == "rotate" ? 1 : 2);
                gtk_scale_set_value_pos(GTK_SCALE(sc), GTK_POS_RIGHT);
                gtk_range_set_value(GTK_RANGE(sc), cur_v);
                const char *axis[] = {"X", "Y", "Z"};
                gtk_widget_set_tooltip_text(sc, axis[i]);
                g_signal_connect_data(sc, "button-press-event", CB(+[](GtkWidget *, GdkEvent *, gpointer) -> gboolean {
                                          app->doc.checkpoint();
                                          return FALSE;
                                      }),
                                      nullptr, nullptr, static_cast<GConnectFlags>(0));
                g_signal_connect_data(sc, "key-press-event", CB(+[](GtkWidget *, GdkEvent *, gpointer) -> gboolean {
                                          app->doc.checkpoint();
                                          return FALSE;
                                      }),
                                      nullptr, nullptr, static_cast<GConnectFlags>(0));
                on_signal(sc, "value-changed", [sc, i, key, entries, is_vec_node, here] {
                    json *n = cur();
                    if (!n || !here())
                        return;
                    json *target = is_vec_node ? n : &(*n)[key];
                    if (!target->is_array() || target->size() < 3)
                        *target = json::array({0.0, 0.0, 0.0});
                    const double v = std::round(gtk_range_get_value(GTK_RANGE(sc)) * 100.0) / 100.0;
                    if ((*target)[static_cast<std::size_t>(i)] == v)
                        return;
                    (*target)[static_cast<std::size_t>(i)] = v;
                    gtk_entry_set_text(GTK_ENTRY((*entries)[static_cast<std::size_t>(i)]), fmt(v).c_str());
                    value_changed();
                });
                gtk_box_pack_start(GTK_BOX(cell), sc, FALSE, FALSE, 0);
            }
            gtk_grid_attach(GTK_GRID(grid), cell, 1, row, 1, 1);
        }
        else
        {
            GtkWidget *e = gtk_entry_new();
            gtk_widget_set_hexpand(e, TRUE);
            if (cv)
                gtk_entry_set_text(GTK_ENTRY(e), cv->is_string() ? cv->get<std::string>().c_str() : cv->is_number() ? fmt(cv->get<double>()).c_str() : "");
            const Field::Kind kind = f.kind;
            auto apply = [e, key, kind, here] {
                if (!here())
                    return;
                const std::string t = gtk_entry_get_text(GTK_ENTRY(e));
                if (t.empty())
                {
                    commit_value(key.c_str(), json(), true);
                    return;
                }
                if (kind == Field::Text)
                {
                    commit_value(key.c_str(), t, false);
                    return;
                }
                double v;
                if (!parse_num(t.c_str(), v))
                    return;
                if (kind == Field::Int)
                    commit_value(key.c_str(), static_cast<long long>(v), false);
                else
                    commit_value(key.c_str(), v, false);
            };
            on_signal(e, "activate", apply);
            g_signal_connect_data(e, "focus-out-event", CB(+[](GtkWidget *, GdkEvent *, gpointer d) -> gboolean {
                                      (*static_cast<std::function<void()> *>(d))();
                                      return FALSE;
                                  }),
                                  new std::function<void()>(apply), [](gpointer d, GClosure *) { delete static_cast<std::function<void()> *>(d); }, static_cast<GConnectFlags>(0));
            if (key == "fuel_start" || key == "propellant")
            {
                // A fuel slider: starting fill is 0 to 1 of the tank, propellant runs to twice its current mass.
                const bool fill = key == "fuel_start";
                const double now = cv && cv->is_number() ? cv->get<double>() : (fill ? 1.0 : 0.0);
                const double hi = fill ? 1.0 : std::max(1000.0, std::ceil(now * 2.0));
                GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
                gtk_box_pack_start(GTK_BOX(cell), e, FALSE, FALSE, 0);
                GtkWidget *sc = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, hi, fill ? 0.01 : 1.0);
                gtk_scale_set_digits(GTK_SCALE(sc), fill ? 2 : 0);
                gtk_scale_set_value_pos(GTK_SCALE(sc), GTK_POS_RIGHT);
                gtk_range_set_value(GTK_RANGE(sc), now);
                for (const char *sig : {"button-press-event", "key-press-event"})
                    g_signal_connect_data(sc, sig, CB(+[](GtkWidget *, GdkEvent *, gpointer) -> gboolean {
                                              app->doc.checkpoint();
                                              return FALSE;
                                          }),
                                          nullptr, nullptr, static_cast<GConnectFlags>(0));
                on_signal(sc, "value-changed", [sc, e, key, fill, here] {
                    json *n = cur();
                    if (!n || !here())
                        return;
                    const double v = fill ? std::round(gtk_range_get_value(GTK_RANGE(sc)) * 100.0) / 100.0 : std::round(gtk_range_get_value(GTK_RANGE(sc)));
                    if (n->contains(key) && (*n)[key] == v)
                        return;
                    (*n)[key] = v;
                    gtk_entry_set_text(GTK_ENTRY(e), fmt(v).c_str());
                    value_changed();
                });
                gtk_box_pack_start(GTK_BOX(cell), sc, FALSE, FALSE, 0);
                gtk_grid_attach(GTK_GRID(grid), cell, 1, row, 1, 1);
            }
            else
                gtk_grid_attach(GTK_GRID(grid), e, 1, row, 1, 1);
            if (f.kind == Field::Text && key == "model")
            {
                GtkWidget *b = gtk_button_new_with_label("...");
                on_signal(b, "clicked", [e, apply] {
                    GtkWidget *d = gtk_file_chooser_dialog_new("Model file", GTK_WINDOW(app->window), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
                    if (app->doc.has_source())
                        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), (app->doc.source_dir() / "models").string().c_str());
                    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
                    {
                        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
                        // Reuse the model part machinery: swap the path through a temporary part so a copy into models/ is queued.
                        const int stage = app->sel.stage;
                        const int before = static_cast<int>(app->doc.root()["stages"][static_cast<std::size_t>(stage)]["parts"].size());
                        app->doc.add_model_part(stage, f);
                        json &parts = app->doc.root()["stages"][static_cast<std::size_t>(stage)]["parts"];
                        if (static_cast<int>(parts.size()) > before)
                        {
                            const json np = parts.back();
                            parts.erase(parts.size() - 1);
                            json *n = cur();
                            if (n && np.contains("model"))
                            {
                                (*n)["model"] = np["model"];
                                gtk_entry_set_text(GTK_ENTRY(e), np["model"].get<std::string>().c_str());
                                app->meshes.clear();
                                value_changed();
                            }
                        }
                        g_free(f);
                    }
                    gtk_widget_destroy(d);
                });
                gtk_grid_attach(GTK_GRID(grid), b, 2, row, 1, 1);
            }
        }
    }

    void rebuild_form()
    {
        GList *kids = gtk_container_get_children(GTK_CONTAINER(app->form_box));
        for (GList *k = kids; k; k = k->next)
            gtk_widget_destroy(GTK_WIDGET(k->data));
        g_list_free(kids);
        json *n = cur();
        if (!n)
            return;

        std::vector<Field> fields;
        std::string title;
        const std::vector<std::string> classes = {"building", "launcher", "aircraft", "tug", "station", "transfer", "missile", "probe", "payload", "lander", "ground", "capsule"};
        switch (app->sel.kind)
        {
        case Selection::ship:
            title = "Ship";
            fields = {{Field::Text, "name", "Name"},
                      {Field::Choice, "class", "Class", classes},
                      {Field::Num, "drag_area", "Drag area m2"},
                      {Field::Num, "camera_distance", "Camera distance m"},
                      {Field::Num, "torque_accel", "Torque accel"},
                      {Field::Num, "max_rate", "Max rate"},
                      {Field::Num, "rcs_thrust", "RCS thrust N"},
                      {Field::Bool, "retractable_gear", "Retractable gear"},
                      {Field::Num, "gear_time", "Gear time s"}};
            fields.push_back({Field::Choice, "script", "Autopilot script", {"@launcher.lua"}});
            fields.push_back({Field::Bool, "cockpit.mfds", "Cockpit MFDs"});
            fields.push_back({Field::Num, "launcher_ascent.kick_speed", "Turn start speed m/s"});
            fields.push_back({Field::Num, "launcher_ascent.kick_pitch", "Initial pitch deg"});
            fields.push_back({Field::Num, "launcher_ascent.turn_end_km", "Turn end altitude km"});
            fields.push_back({Field::Num, "launcher_ascent.turn_end_pitch", "Pitch at turn end deg"});
            fields.push_back({Field::Num, "launcher_ascent.climb_speed", "Climb speed m/s"});
            break;
        case Selection::stage_node:
            title = "Stage";
            fields = {{Field::Text, "name", "Name"},       {Field::Num, "dry_mass", "Dry mass kg"},   {Field::Num, "propellant", "Propellant kg"}, {Field::Num, "fuel_start", "Starting fuel (0-1)"}, {Field::Num, "z", "Z position m"},
                      {Field::Num, "length", "Length m"},  {Field::Num, "radius", "Radius m"},        {Field::Num, "separation_speed", "Separation speed"},
                      {Field::Vec3, "separation_dir", "Separation dir"},
                      {Field::Bool, "with_next", "Separate with next stage"},
                      {Field::Bool, "ignite_with_previous", "Ignite with previous stage"}};
            break;
        case Selection::part:
            if (n->contains("model"))
            {
                title = "Model part";
                fields = {{Field::Text, "model", "Model file"}, {Field::Vec3, "offset", "Offset"}, {Field::Vec3, "rotate", "Rotate deg"}, {Field::Num, "scale", "Scale"}};
            }
            else
            {
                title = "Prop part";
                fields = {{Field::Choice, "prop", "Prop", {"stage", "adapter", "engines", "fins", "fairing", "fatman", "csm", "aeroplane", "lunar_rover"}},
                          {Field::Num, "radius", "Radius"}, {Field::Num, "top_radius", "Top radius"}, {Field::Num, "length", "Length"}, {Field::Num, "span", "Span"},
                          {Field::Int, "count", "Count"},   {Field::Num, "z", "Z position"},         {Field::Vec3, "offset", "Offset"}};
            }
            break;
        case Selection::engine:
            title = "Engine";
            fields = {{Field::Vec3, "position", "Position"},
                      {Field::Vec3, "direction", "Direction"},
                      {Field::Num, "thrust", "Thrust N"},
                      {Field::Num, "isp", "Isp vacuum s"},
                      {Field::Num, "isp_sl", "Isp sea level s"},
                      {Field::Choice, "type", "Type", {"rocket", "solid", "nuclear", "jet", "scramjet", "hyperjump"}},
                      {Field::Choice, "group", "Group", {"main", "hover", "pod", "rcs"}},
                      {Field::Text, "fuel", "Fuel tank"},
                      {Field::Num, "plume", "Plume"},
                      {Field::Num, "plume_width", "Plume width"},
                      {Field::Bool, "air_breathing", "Air breathing"},
                      {Field::Bool, "afterburner", "Afterburner"}};
            break;
        default:
            title = "Gear";
            fields = {{Field::Vec3, "", "Position"}};
            break;
        }

        GtkWidget *head = gtk_label_new(nullptr);
        gtk_label_set_markup(GTK_LABEL(head), ("<b>" + title + "</b>").c_str());
        gtk_widget_set_halign(head, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(app->form_box), head, FALSE, FALSE, 2);
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
        int row = 0;
        for (const Field &f : fields)
            add_field(grid, row++, f);
        gtk_box_pack_start(GTK_BOX(app->form_box), grid, FALSE, FALSE, 0);

        // Raw JSON: every key, including the ones the form does not show.
        GtkWidget *exp = gtk_expander_new("Raw JSON (all keys)");
        GtkWidget *sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(sw, -1, 200);
        GtkWidget *tv = gtk_text_view_new();
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
        std::string dump = n->dump(2);
        if (app->sel.kind == Selection::ship && n->contains("stages")) // stages have their own rows
        {
            json copy = *n;
            copy["stages"] = "(edited in the tree)";
            dump = copy.dump(2);
        }
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), dump.c_str(), -1);
        gtk_container_add(GTK_CONTAINER(sw), tv);
        GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_box_pack_start(GTK_BOX(vb), sw, TRUE, TRUE, 0);
        GtkWidget *apply = gtk_button_new_with_label("Apply JSON");
        const bool ship_node = app->sel.kind == Selection::ship;
        on_signal(apply, "clicked", [tv, ship_node] {
            GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
            GtkTextIter s, e;
            gtk_text_buffer_get_bounds(b, &s, &e);
            char *t = gtk_text_buffer_get_text(b, &s, &e, FALSE);
            json parsed = json::parse(t, nullptr, false);
            g_free(t);
            if (parsed.is_discarded())
            {
                message("That is not valid JSON.", GTK_MESSAGE_ERROR);
                return;
            }
            json *n = cur();
            if (!n)
                return;
            const Selection keep = app->sel;
            app->doc.checkpoint();
            if (ship_node)
            {
                if (!parsed.is_object())
                    return;
                const json stages = (*n)["stages"];
                *n = parsed;
                (*n)["stages"] = stages;
            }
            else
                *n = parsed;
            app->meshes.clear();
            structure_changed(keep);
        });
        gtk_box_pack_start(GTK_BOX(vb), apply, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(exp), vb);
        gtk_box_pack_start(GTK_BOX(app->form_box), exp, FALSE, FALSE, 6);
        gtk_widget_show_all(app->form_box);
    }

    // ---- actions -------------------------------------------------------------------------------------------------------------------
    int target_stage()
    {
        if (app->sel.stage >= 0)
            return app->sel.stage;
        return app->doc.stage_count() - 1;
    }

    void do_add_stage()
    {
        const int i = app->doc.add_stage();
        structure_changed({Selection::stage_node, i, -1});
    }

    void do_delete()
    {
        const Selection s = app->sel;
        json *n = cur();
        if (!n || s.kind == Selection::ship)
            return;
        if (s.kind == Selection::stage_node)
        {
            app->doc.remove_stage(s.stage);
            structure_changed({Selection::ship, -1, -1});
            return;
        }
        const char *list = s.kind == Selection::part ? "parts" : s.kind == Selection::engine ? "engines" : "gear";
        json &arr = app->doc.root()["stages"][static_cast<std::size_t>(s.stage)][list];
        app->doc.checkpoint();
        arr.erase(static_cast<std::size_t>(s.item));
        structure_changed({Selection::stage_node, s.stage, -1});
    }

    void do_duplicate()
    {
        const Selection s = app->sel;
        if (s.kind == Selection::stage_node)
        {
            const int i = app->doc.duplicate_stage(s.stage);
            structure_changed({Selection::stage_node, i, -1});
        }
        else if (s.kind == Selection::part || s.kind == Selection::engine || s.kind == Selection::gear)
        {
            const char *list = s.kind == Selection::part ? "parts" : s.kind == Selection::engine ? "engines" : "gear";
            json &arr = app->doc.root()["stages"][static_cast<std::size_t>(s.stage)][list];
            app->doc.checkpoint();
            const json copy = arr[static_cast<std::size_t>(s.item)];
            arr.insert(arr.begin() + s.item + 1, copy);
            structure_changed({s.kind, s.stage, s.item + 1});
        }
    }

    void do_move(int delta)
    {
        const Selection s = app->sel;
        if (s.kind == Selection::stage_node)
        {
            if (app->doc.move_stage(s.stage, delta))
                structure_changed({Selection::stage_node, s.stage + delta, -1});
        }
        else if (s.kind == Selection::part || s.kind == Selection::engine || s.kind == Selection::gear)
        {
            const char *list = s.kind == Selection::part ? "parts" : s.kind == Selection::engine ? "engines" : "gear";
            json &arr = app->doc.root()["stages"][static_cast<std::size_t>(s.stage)][list];
            const int to = s.item + delta;
            if (to < 0 || to >= static_cast<int>(arr.size()))
                return;
            app->doc.checkpoint();
            std::swap(arr[static_cast<std::size_t>(s.item)], arr[static_cast<std::size_t>(to)]);
            structure_changed({s.kind, s.stage, to});
        }
    }

    void do_add_part_model()
    {
        const int st = target_stage();
        if (st < 0)
            return;
        GtkWidget *d = gtk_file_chooser_dialog_new("Model file (OBJ or GLB)", GTK_WINDOW(app->window), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        if (app->doc.has_source())
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), (app->doc.source_dir() / "models").string().c_str());
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            app->doc.add_model_part(st, f);
            g_free(f);
            const int n = static_cast<int>(app->doc.root()["stages"][static_cast<std::size_t>(st)]["parts"].size());
            gtk_widget_destroy(d);
            structure_changed({Selection::part, st, n - 1});
            return;
        }
        gtk_widget_destroy(d);
    }

    void do_add_prop(const std::string &prop)
    {
        const int st = target_stage();
        if (st < 0)
            return;
        app->doc.add_prop_part(st, prop);
        const int n = static_cast<int>(app->doc.root()["stages"][static_cast<std::size_t>(st)]["parts"].size());
        structure_changed({Selection::part, st, n - 1});
    }

    void load_selected()
    {
        const int a = gtk_combo_box_get_active(GTK_COMBO_BOX(app->addon_combo));
        gchar *ship = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->ship_combo));
        if (a < 0 || !ship)
        {
            g_free(ship);
            return;
        }
        std::string error;
        if (!app->doc.load(app->addons[static_cast<std::size_t>(a)], ship, error))
            message("Could not load the ship: " + error, GTK_MESSAGE_ERROR);
        g_free(ship);
        app->meshes.clear();
        app->last_saved_ship.clear();
        structure_changed({Selection::ship, -1, -1});
    }

    void fill_ships()
    {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->ship_combo));
        const int a = gtk_combo_box_get_active(GTK_COMBO_BOX(app->addon_combo));
        if (a < 0)
            return;
        for (const std::string &s : app->addons[static_cast<std::size_t>(a)].ships)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->ship_combo), s.c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->ship_combo), 0);
    }

    void do_save()
    {
        fs::path backup;
        std::string error;
        if (!app->doc.save_in_place(app->root / "shipedit-backups", backup, error))
        {
            message("Could not save: " + error, GTK_MESSAGE_ERROR);
            return;
        }
        app->doc.mark_clean();
        app->last_saved_ship = app->doc.name();
        refresh_status();
        message("Saved into " + app->doc.source_dir().string() + "\n\nThe previous version is backed up in " + backup.string() + "\n\nRestart Kosmos (or use Launch) to see the change.");
    }

    void do_save_as()
    {
        GtkWidget *d = gtk_dialog_new_with_buttons("Save as new addon", GTK_WINDOW(app->window), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
        gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
        GtkWidget *id = gtk_entry_new(), *name = gtk_entry_new(), *ship_name = gtk_entry_new();
        const std::string base = app->doc.name();
        gtk_entry_set_text(GTK_ENTRY(id), (slug(base) + "-mod").c_str());
        gtk_entry_set_text(GTK_ENTRY(name), (base + " (modified)").c_str());
        gtk_entry_set_text(GTK_ENTRY(ship_name), base.c_str());
        GtkWidget *dest = gtk_file_chooser_button_new("Folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dest), app->addons_dir.string().c_str());
        GtkWidget *over = gtk_check_button_new_with_label("Replace the folder if it already exists");
        const char *labels[] = {"Addon id (folder)", "Addon name", "Ship name", "Save into"};
        GtkWidget *widgets[] = {id, name, ship_name, dest};
        for (int i = 0; i < 4; ++i)
        {
            GtkWidget *l = gtk_label_new(labels[i]);
            gtk_widget_set_halign(l, GTK_ALIGN_START);
            gtk_grid_attach(GTK_GRID(grid), l, 0, i, 1, 1);
            gtk_widget_set_hexpand(widgets[i], TRUE);
            gtk_grid_attach(GTK_GRID(grid), widgets[i], 1, i, 1, 1);
        }
        gtk_grid_attach(GTK_GRID(grid), over, 1, 4, 1, 1);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), grid);
        gtk_widget_show_all(d);
        while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            const std::string new_name = gtk_entry_get_text(GTK_ENTRY(ship_name));
            if (!new_name.empty() && new_name != app->doc.name())
            {
                app->doc.checkpoint();
                app->doc.root()["name"] = new_name;
            }
            char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dest));
            fs::path written;
            std::string error;
            const bool ok = app->doc.save_as(folder ? folder : "", gtk_entry_get_text(GTK_ENTRY(id)), gtk_entry_get_text(GTK_ENTRY(name)), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(over)) != 0, written, error);
            g_free(folder);
            if (ok)
            {
                app->doc.mark_clean();
                app->last_saved_ship = app->doc.name();
                refresh_status();
                gtk_widget_destroy(d);
                message("Saved to " + written.string() + "\n\nRestart Kosmos (or use Launch) to see the new ship.");
                return;
            }
            message("Could not save: " + error, GTK_MESSAGE_ERROR);
        }
        gtk_widget_destroy(d);
        structure_changed(app->sel);
    }

    void do_launch()
    {
        for (const fs::path &exe : {app->root / "build" / "kosmos", app->root / "build-win" / "kosmos.exe", app->root / "kosmos", app->root / "kosmos.exe"})
            if (fs::exists(exe))
            {
                const std::string exe_s = exe.string(), dir_s = exe.parent_path().string();
                gchar *argv[] = {const_cast<gchar *>(exe_s.c_str()), const_cast<gchar *>("--ship"), const_cast<gchar *>(app->last_saved_ship.c_str()), nullptr};
                GError *err = nullptr;
                if (!g_spawn_async(dir_s.c_str(), argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &err))
                {
                    message(std::string("Could not start Kosmos: ") + err->message, GTK_MESSAGE_ERROR);
                    g_error_free(err);
                }
                return;
            }
        message("The kosmos program was not found next to the addons folder.", GTK_MESSAGE_ERROR);
    }

    void do_cad()
    {
        const fs::path ship = app->doc.source_dir() / "ship.json";
        if (app->doc.source_dir().empty() || !fs::exists(ship))
        {
            message("Save the ship as an addon first; KosmosCAD edits its ship.json.", GTK_MESSAGE_ERROR);
            return;
        }
        for (const fs::path &exe : {app->root / "addons" / "kosmoscad" / "bin" / "kosmoscad", app->root / "addons" / "kosmoscad" / "bin" / "kosmoscad.exe",
                                    app->root / "kosmoscad", app->root / "kosmoscad.exe"})
            if (!exe.empty() && fs::exists(exe))
            {
                const std::string exe_s = exe.string(), ship_s = ship.string();
                gchar *argv[] = {const_cast<gchar *>(exe_s.c_str()), const_cast<gchar *>(ship_s.c_str()), nullptr};
                GError *err = nullptr;
                if (!g_spawn_async(nullptr, argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &err))
                {
                    message(std::string("Could not start KosmosCAD: ") + err->message, GTK_MESSAGE_ERROR);
                    g_error_free(err);
                }
                return;
            }
        message("KosmosCAD was not found in addons/kosmoscad/bin.", GTK_MESSAGE_ERROR);
    }

    gboolean draw_cb(GtkWidget *w, cairo_t *cr, gpointer)
    {
        draw_preview(cr, gtk_widget_get_allocated_width(w), gtk_widget_get_allocated_height(w), app->doc, app->sel, app->view, app->meshes, !app->use_gl, true,
                     app->arrange_toggle && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->arrange_toggle)));
        if (app->drag_stage >= 0 && app->stage_dragged)
        {
            cairo_set_source_rgba(cr, 1.0, 0.8, 0.25, 0.9);
            cairo_set_line_width(cr, 2.0);
            cairo_move_to(cr, 0, app->stage_drag_y);
            cairo_line_to(cr, gtk_widget_get_allocated_width(w), app->stage_drag_y);
            cairo_stroke(cr);
        }
        return FALSE;
    }

    gboolean gl_render_cb(GtkGLArea *a, GdkGLContext *, gpointer)
    {
        const int w = gtk_widget_get_allocated_width(GTK_WIDGET(a)), h = gtk_widget_get_allocated_height(GTK_WIDGET(a));
        if (const std::shared_ptr<const Geometry> geo = prepare_geometry(app->doc, app->view, app->meshes))
            app->gl.draw(*geo, projection(*geo, w, h, app->view), app->sel, w, h, gtk_widget_get_scale_factor(GTK_WIDGET(a)));
        else
        {
            glClearColor(0.07f, 0.08f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        return TRUE;
    }

    void gl_realize_cb(GtkWidget *w, gpointer)
    {
        gtk_gl_area_make_current(GTK_GL_AREA(w));
        if (gtk_gl_area_get_error(GTK_GL_AREA(w)) || !app->gl.init())
        {
            std::fprintf(stderr, "shipedit: OpenGL is not available (%s); drawing the preview in software\n", gtk_gl_area_get_error(GTK_GL_AREA(w)) ? "no context" : "shader");
            gtk_widget_hide(w);
            return;
        }
        app->use_gl = true;
        app->view.thin_when_dragging = false;
        app->view.triangle_budget = 4000000;
        gtk_widget_queue_draw(app->area);
    }

    void gl_unrealize_cb(GtkWidget *w, gpointer)
    {
        gtk_gl_area_make_current(GTK_GL_AREA(w));
        if (!gtk_gl_area_get_error(GTK_GL_AREA(w)))
            app->gl.release();
        app->use_gl = false;
    }

    // Both layers change together: the overlay is cairo, the surfaces are GL.
    void redraw_preview()
    {
        gtk_widget_queue_draw(app->area);
        if (app->use_gl)
            gtk_gl_area_queue_render(GTK_GL_AREA(app->gl_area));
    }

    GtkWidget *button(const char *label, std::function<void()> f)
    {
        GtkWidget *b = gtk_button_new_with_label(label);
        on_signal(b, "clicked", std::move(f));
        return b;
    }

    struct StageTile
    {
        Document preview;
        MeshCache meshes;
        PreviewView view;
        int index = 0;
    };

    bool add_library_stage(int index, int position = -1)
    {
        int inserted = -1;
        std::string error, warning;
        if (!app->doc.import_stage(app->library_doc, index, position, inserted, error, warning,
                                   gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->overlap_toggle))))
        {
            message("Could not add the stage: " + error, GTK_MESSAGE_ERROR);
            return false;
        }
        app->meshes.clear();
        structure_changed({Selection::stage_node, inserted, -1});
        if (!warning.empty())
            message("The stage was added, but some source assets are missing:" + warning, GTK_MESSAGE_WARNING);
        return true;
    }

    int stage_drop_position(GtkWidget *widget, double y)
    {
        const auto geo = prepare_geometry(app->doc, app->view, app->meshes);
        if (!geo)
            return 0;
        const Projection p = projection(*geo, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget), app->view);
        const double vertical = p.cy * p.sp;
        if (std::abs(vertical) < 0.15)
            return -1;
        int position = 0;
        for (const json &stage : app->doc.root()["stages"])
        {
            const double centre = p.cyc + (stage.value("z", 0.0) - p.centre_z) * vertical * p.scale;
            if ((y - centre) * vertical < 0)
                ++position;
            else
                break;
        }
        return position;
    }

    int stage_at(GtkWidget *widget, double x, double y)
    {
        const auto geo = prepare_geometry(app->doc, app->view, app->meshes);
        if (!geo)
            return -1;
        const Projection p = projection(*geo, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget), app->view);
        const double vertical = p.cy * p.sp;
        if (std::abs(vertical) < 0.15)
            return -1;
        int i = 0;
        for (const json &stage : app->doc.root()["stages"])
        {
            const double centre = p.cyc + (stage.value("z", 0.0) - p.centre_z) * vertical * p.scale;
            const double half = std::max(5.0, stage.value("length", 1.0) * std::abs(vertical) * p.scale * 0.5);
            const double width = std::max(10.0, stage.value("radius", 1.0) * p.scale);
            if (std::abs(y - centre) <= half && std::abs(x - p.cx) <= width)
                return i;
            ++i;
        }
        return -1;
    }

    void populate_stage_library()
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(app->library_list));
        for (GList *p = children; p; p = p->next)
            gtk_widget_destroy(GTK_WIDGET(p->data));
        g_list_free(children);
        const int addon_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->library_addon));
        gchar *ship = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->library_ship));
        if (addon_index < 0 || !ship)
        {
            g_free(ship);
            return;
        }
        std::string error;
        const bool ok = app->library_doc.load(app->addons[static_cast<std::size_t>(addon_index)], ship, error);
        g_free(ship);
        if (!ok)
        {
            message("Could not open the stage library: " + error, GTK_MESSAGE_ERROR);
            return;
        }
        json expanded;
        if (!app->library_doc.expanded_root(expanded, error))
        {
            message("Could not expand the source ship's stages: " + error, GTK_MESSAGE_ERROR);
            return;
        }
        const json stages = expanded.value("stages", json::array());
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            auto *tile = new StageTile;
            tile->index = static_cast<int>(i);
            tile->preview = app->library_doc;
            tile->preview.root()["stages"] = json::array({stages[i]});
            tile->view.yaw = 0;
            tile->view.pitch = 1.57079632679;
            tile->view.triangle_budget = 15000;
            GtkWidget *b = gtk_button_new();
            g_object_set_data_full(G_OBJECT(b), "stage-tile", tile, +[](gpointer data) { delete static_cast<StageTile *>(data); });
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            GtkWidget *image = gtk_drawing_area_new();
            gtk_widget_set_size_request(image, 150, 100);
            g_signal_connect(image, "draw", CB(+[](GtkWidget *w, cairo_t *cr, gpointer data) -> gboolean {
                                 auto *t = static_cast<StageTile *>(data);
                                 draw_preview(cr, gtk_widget_get_allocated_width(w), gtk_widget_get_allocated_height(w),
                                              t->preview, {}, t->view, t->meshes, true, false);
                                 return FALSE;
                             }), tile);
            gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
            const std::string name = std::to_string(i + 1) + ". " + stages[i].value("name", std::string("Stage"));
            GtkWidget *label = gtk_label_new(name.c_str());
            gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_max_width_chars(GTK_LABEL(label), 24);
            gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
            gtk_container_add(GTK_CONTAINER(b), box);
            gtk_widget_set_tooltip_text(b, "Drag to a position on the rocket, or click to add at the nose end.");
            g_signal_connect(b, "clicked", CB(+[](GtkWidget *, gpointer data) {
                                 add_library_stage(static_cast<StageTile *>(data)->index);
                             }), tile);
            GtkTargetEntry target[] = {{const_cast<gchar *>("application/x-kosmos-stage"), GTK_TARGET_SAME_APP, 0}};
            gtk_drag_source_set(b, GDK_BUTTON1_MASK, target, 1, GDK_ACTION_COPY);
            g_signal_connect(b, "drag-data-get", CB(+[](GtkWidget *, GdkDragContext *, GtkSelectionData *selection, guint, guint, gpointer data) {
                                 const std::string value = std::to_string(static_cast<StageTile *>(data)->index);
                                 gtk_selection_data_set(selection, gdk_atom_intern_static_string("application/x-kosmos-stage"), 8,
                                                        reinterpret_cast<const guchar *>(value.data()), static_cast<gint>(value.size()));
                             }), tile);
            gtk_box_pack_start(GTK_BOX(app->library_list), b, FALSE, FALSE, 0);
        }
        gtk_widget_show_all(app->library_list);
    }

    void fill_library_ships()
    {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->library_ship));
        const int index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->library_addon));
        if (index < 0)
            return;
        for (const std::string &ship : app->addons[static_cast<std::size_t>(index)].ships)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->library_ship), ship.c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->library_ship), 0);
    }

    void build_window()
    {
        app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_default_size(GTK_WINDOW(app->window), 1360, 820);
        g_signal_connect(app->window, "delete-event", CB(+[](GtkWidget *, GdkEvent *, gpointer) -> gboolean {
                             if (!app->doc.dirty())
                                 return FALSE;
                             GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Discard the changes that are not saved?");
                             const int r = gtk_dialog_run(GTK_DIALOG(d));
                             gtk_widget_destroy(d);
                             return r == GTK_RESPONSE_YES ? FALSE : TRUE;
                         }),
                         nullptr);
        g_signal_connect(app->window, "destroy", CB(gtk_main_quit), nullptr);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_container_add(GTK_CONTAINER(app->window), vbox);

        // Toolbar
        GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(bar), 4);
        app->addon_combo = gtk_combo_box_text_new();
        for (const AddonInfo &a : app->addons)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->addon_combo), (a.name.empty() ? a.id : a.name).c_str());
        app->ship_combo = gtk_combo_box_text_new();
        on_signal(app->addon_combo, "changed", [] { fill_ships(); });
        gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("Addon"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), app->addon_combo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("Ship"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), app->ship_combo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), button("Load", [] { load_selected(); }), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), button("New blank", [] {
                               app->doc.make_blank("New Ship");
                               app->last_saved_ship.clear();
                               app->meshes.clear();
                               structure_changed({Selection::ship, -1, -1});
                           }), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), button("New rocket", [] {
                               app->doc.make_rocket("New Rocket");
                               app->last_saved_ship.clear();
                               app->meshes.clear();
                               app->view.yaw = 0;
                               app->view.pitch = 1.57079632679;
                               gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->arrange_toggle), TRUE);
                               structure_changed({Selection::ship, -1, -1});
                           }), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), bar, FALSE, FALSE, 0);
        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(actions), 4);
        app->save_btn = button("Save", [] { do_save(); });
        gtk_widget_set_tooltip_text(app->save_btn, "Write the changes into the addon this ship was loaded from. The whole addon is backed up first.");
        gtk_box_pack_start(GTK_BOX(actions), app->save_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(actions), button("Save as new addon...", [] { do_save_as(); }), FALSE, FALSE, 0);
        app->undo_btn = button("Undo", [] {
            if (app->doc.undo())
                structure_changed(app->sel);
        });
        app->redo_btn = button("Redo", [] {
            if (app->doc.redo())
                structure_changed(app->sel);
        });
        app->launch_btn = button("Launch in Kosmos", [] { do_launch(); });
        gtk_widget_set_tooltip_text(app->launch_btn, "Available after saving: starts the saved ship on the Chippenham pad");
        gtk_box_pack_start(GTK_BOX(actions), app->undo_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(actions), app->redo_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(actions), app->launch_btn, FALSE, FALSE, 0);
        GtkWidget *cad_btn = button("Model in KosmosCAD", [] { do_cad(); });
        gtk_widget_set_tooltip_text(cad_btn, "Open this ship's ship.json in KosmosCAD, the 3D modeller. Reload the ship here afterwards.");
        gtk_box_pack_start(GTK_BOX(actions), cad_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), actions, FALSE, FALSE, 0);

        GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

        // Left: tree and its buttons
        GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        app->store = gtk_tree_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
        app->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->store));
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(app->tree_view), FALSE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree_view), gtk_tree_view_column_new_with_attributes("", gtk_cell_renderer_text_new(), "text", COL_LABEL, nullptr));
        g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree_view)), "changed", CB(+[](GtkTreeSelection *ts, gpointer) {
                             if (app->building)
                                 return;
                             GtkTreeModel *m;
                             GtkTreeIter it;
                             if (!gtk_tree_selection_get_selected(ts, &m, &it))
                                 return;
                             int k, st, item;
                             gtk_tree_model_get(m, &it, COL_KIND, &k, COL_STAGE, &st, COL_ITEM, &item, -1);
                             app->sel = {static_cast<Selection::Kind>(k), st, item};
                             rebuild_form();
                             redraw_preview();
                         }),
                         nullptr);
        GtkWidget *tsw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(tsw), app->tree_view);
        gtk_box_pack_start(GTK_BOX(left), tsw, TRUE, TRUE, 0);

        GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3), *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_box_pack_start(GTK_BOX(row1), button("+ Stage", [] { do_add_stage(); }), TRUE, TRUE, 0);
        GtkWidget *part_btn = gtk_menu_button_new();
        gtk_button_set_label(GTK_BUTTON(part_btn), "+ Part");
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *mi = gtk_menu_item_new_with_label("Model file...");
        on_signal(mi, "activate", [] { do_add_part_model(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
        for (const char *p : {"stage", "adapter", "engines", "fins", "fairing", "fatman", "csm", "aeroplane", "lunar_rover"})
        {
            GtkWidget *pi = gtk_menu_item_new_with_label((std::string("Prop: ") + p).c_str());
            const std::string name = p;
            on_signal(pi, "activate", [name] { do_add_prop(name); });
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), pi);
        }
        gtk_widget_show_all(menu);
        gtk_menu_button_set_popup(GTK_MENU_BUTTON(part_btn), menu);
        gtk_box_pack_start(GTK_BOX(row1), part_btn, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row1), button("+ Engine", [] {
                               const int st = target_stage();
                               if (st < 0)
                                   return;
                               app->doc.add_engine(st);
                               structure_changed({Selection::engine, st, static_cast<int>(app->doc.root()["stages"][static_cast<std::size_t>(st)]["engines"].size()) - 1});
                           }), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row1), button("+ Gear", [] {
                               const int st = target_stage();
                               if (st < 0)
                                   return;
                               app->doc.add_gear(st);
                               structure_changed({Selection::gear, st, static_cast<int>(app->doc.root()["stages"][static_cast<std::size_t>(st)]["gear"].size()) - 1});
                           }), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row2), button("Delete", [] { do_delete(); }), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row2), button("Duplicate", [] { do_duplicate(); }), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row2), button("Up", [] { do_move(-1); }), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row2), button("Down", [] { do_move(1); }), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row2), button("Auto-pack", [] {
                               app->doc.auto_pack();
                               structure_changed(app->sel);
                           }), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(left), row1, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(left), row2, FALSE, FALSE, 0);
        app->with_contents = gtk_check_button_new_with_label("Moving a stage's Z moves its parts, engines and gear");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->with_contents), TRUE);
        gtk_box_pack_start(GTK_BOX(left), app->with_contents, FALSE, FALSE, 0);
        GtkWidget *library = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        app->library_addon = gtk_combo_box_text_new();
        app->library_ship = gtk_combo_box_text_new();
        for (const AddonInfo &a : app->addons)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->library_addon), (a.name.empty() ? a.id : a.name).c_str());
        gtk_box_pack_start(GTK_BOX(library), gtk_label_new("Source addon"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(library), app->library_addon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(library), gtk_label_new("Source ship"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(library), app->library_ship, FALSE, FALSE, 0);
        GtkWidget *hint = gtk_label_new("Drag a stage onto the rocket, or click to add at the nose.\nShip-wide scripts and systems are not imported.");
        gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
        gtk_box_pack_start(GTK_BOX(library), hint, FALSE, FALSE, 4);
        app->overlap_toggle = gtk_check_button_new_with_label("Overlap previous stage");
        gtk_widget_set_tooltip_text(app->overlap_toggle, "Align the tails of the new and previous stages, for example to place a core alongside strap-on boosters. Check staging flags in Structure.");
        gtk_box_pack_start(GTK_BOX(library), app->overlap_toggle, FALSE, FALSE, 0);
        app->library_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        GtkWidget *library_scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(library_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(library_scroll), app->library_list);
        gtk_box_pack_start(GTK_BOX(library), library_scroll, TRUE, TRUE, 0);
        on_signal(app->library_addon, "changed", [] { fill_library_ships(); });
        on_signal(app->library_ship, "changed", [] { populate_stage_library(); });
        GtkWidget *tabs = gtk_notebook_new();
        gtk_notebook_append_page(GTK_NOTEBOOK(tabs), library, gtk_label_new("Stage library"));
        gtk_notebook_append_page(GTK_NOTEBOOK(tabs), left, gtk_label_new("Structure"));
        gtk_widget_set_size_request(tabs, 300, -1);
        gtk_paned_pack1(GTK_PANED(paned), tabs, FALSE, FALSE);

        // Centre and right
        GtkWidget *inner = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_pack2(GTK_PANED(paned), inner, TRUE, FALSE);
        GtkWidget *centre = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        app->area = gtk_drawing_area_new();
        gtk_widget_set_size_request(app->area, 400, 300);
        gtk_widget_add_events(app->area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK | GDK_SCROLL_MASK);
        g_signal_connect(app->area, "draw", CB(draw_cb), nullptr);
        GtkTargetEntry stage_target[] = {{const_cast<gchar *>("application/x-kosmos-stage"), GTK_TARGET_SAME_APP, 0}};
        gtk_drag_dest_set(app->area, GTK_DEST_DEFAULT_ALL, stage_target, 1, GDK_ACTION_COPY);
        g_signal_connect(app->area, "drag-data-received", CB(+[](GtkWidget *widget, GdkDragContext *context, gint, gint y, GtkSelectionData *selection, guint, guint time, gpointer) {
                             const guchar *data = gtk_selection_data_get_data(selection);
                             const int length = gtk_selection_data_get_length(selection);
                             if (!data || length <= 0)
                             {
                                 gtk_drag_finish(context, FALSE, FALSE, time);
                                 return;
                             }
                             const std::string text(reinterpret_cast<const char *>(data), static_cast<std::size_t>(length));
                             char *end = nullptr;
                             const long index = std::strtol(text.c_str(), &end, 10);
                             if (!end || *end != '\0' || index < 0 || index >= app->library_doc.stage_count())
                             {
                                 gtk_drag_finish(context, FALSE, FALSE, time);
                                 return;
                             }
                             const int position = stage_drop_position(widget, y);
                             if (position < 0)
                             {
                                 message("Choose Top or Bottom view to place a stage by height.", GTK_MESSAGE_WARNING);
                                 gtk_drag_finish(context, FALSE, FALSE, time);
                                 return;
                             }
                             const bool ok = add_library_stage(static_cast<int>(index), position);
                             gtk_drag_finish(context, ok, FALSE, time);
                         }), nullptr);
        // Left drag orbits, middle or right drag (or Shift+left) pans, Ctrl+left drag or the wheel zooms toward the pointer.
        g_signal_connect(app->area, "button-press-event", CB(+[](GtkWidget *, GdkEventButton *e, gpointer) -> gboolean {
                             if (e->button == 1 && !(e->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) &&
                                 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->arrange_toggle)))
                             {
                                 const int stage = stage_at(app->area, e->x, e->y);
                                 if (stage >= 0)
                                 {
                                     app->drag_stage = stage;
                                     app->stage_drag_start_y = app->stage_drag_y = e->y;
                                     app->stage_dragged = false;
                                     select_in_tree({Selection::stage_node, stage, -1});
                                     return TRUE;
                                 }
                             }
                             app->drag_x = e->x;
                             app->drag_y = e->y;
                             app->drag_button = static_cast<int>(e->button);
                             app->view.interactive = true;
                             return TRUE;
                         }),
                         nullptr);
        g_signal_connect(app->area, "motion-notify-event", CB(+[](GtkWidget *w, GdkEventMotion *e, gpointer) -> gboolean {
                             if (app->drag_stage >= 0)
                             {
                                 app->stage_drag_y = e->y;
                                 app->stage_dragged = app->stage_dragged || std::abs(e->y - app->stage_drag_start_y) > 5.0;
                                 redraw_preview();
                                 return TRUE;
                             }
                             if (app->drag_button == 0)
                                 return TRUE;
                             const double dx = e->x - app->drag_x, dy = e->y - app->drag_y;
                             app->drag_x = e->x;
                             app->drag_y = e->y;
                             const bool pan = app->drag_button == 2 || app->drag_button == 3 || (e->state & GDK_SHIFT_MASK);
                             if (pan)
                             {
                                 app->view.pan_x += dx;
                                 app->view.pan_y += dy;
                             }
                             else if (e->state & GDK_CONTROL_MASK)
                                 app->view.zoom = std::max(0.05, std::min(50.0, app->view.zoom * std::exp(-dy * 0.01)));
                             else
                             {
                                 app->view.yaw += dx * 0.008;
                                 app->view.pitch = std::max(-1.5707, std::min(1.5707, app->view.pitch + dy * 0.008));
                             }
                             redraw_preview();
                             return TRUE;
                         }),
                         nullptr);
        g_signal_connect(app->area, "button-release-event", CB(+[](GtkWidget *w, GdkEventButton *e, gpointer) -> gboolean {
                             if (app->drag_stage >= 0)
                             {
                                 const int from = app->drag_stage;
                                 const bool dragged = app->stage_dragged;
                                 app->drag_stage = -1;
                                 app->stage_dragged = false;
                                 if (dragged)
                                 {
                                     int to = stage_drop_position(w, e->y);
                                     if (to > from)
                                         --to;
                                     std::string error;
                                     if (to < 0 || !app->doc.arrange_stage(from, to, error))
                                         message(error.empty() ? "Choose Top or Bottom view to arrange stages." : error, GTK_MESSAGE_WARNING);
                                     else
                                         structure_changed({Selection::stage_node, to, -1});
                                 }
                                 redraw_preview();
                                 return TRUE;
                             }
                             app->drag_button = 0;
                             app->view.interactive = false;
                             redraw_preview();
                             return TRUE;
                         }),
                         nullptr);
        g_signal_connect(app->area, "scroll-event", CB(+[](GtkWidget *w, GdkEventScroll *e, gpointer) -> gboolean {
                             const double k = e->direction == GDK_SCROLL_UP ? 1.15 : e->direction == GDK_SCROLL_DOWN ? 1.0 / 1.15 : 1.0;
                             const double nz = std::max(0.05, std::min(50.0, app->view.zoom * k));
                             const double f = nz / app->view.zoom;
                             const double mx = e->x - gtk_widget_get_allocated_width(w) * 0.5, my = e->y - gtk_widget_get_allocated_height(w) * 0.5;
                             app->view.pan_x = mx + (app->view.pan_x - mx) * f;
                             app->view.pan_y = my + (app->view.pan_y - my) * f;
                             app->view.zoom = nz;
                             redraw_preview();
                             return TRUE;
                         }),
                         nullptr);
        // View buttons: fixed orthographic views along each axis (the nose is -Z), an isometric one, and Fit to reset pan and zoom.
        GtkWidget *views = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_container_set_border_width(GTK_CONTAINER(views), 3);
        struct Preset
        {
            const char *label, *tip;
            double yaw, pitch;
        };
        const double hp = 1.5707963;
        const Preset presets[] = {{"Front", "Looking at the nose (down +Z)", 3.14159265, 0.0}, {"Back", "Looking at the tail", 0.0, 0.0},
                                  {"Left", "From the left side (-X)", hp, 0.0},                  {"Right", "From the right side (+X)", -hp, 0.0},
                                  {"Top", "From above, nose up the screen", 0.0, hp},            {"Bottom", "From below", 0.0, -hp},
                                  {"Iso", "Three-quarter view", 1.25, 0.45}};
        gtk_box_pack_start(GTK_BOX(views), gtk_label_new("View"), FALSE, FALSE, 4);
        app->arrange_toggle = gtk_toggle_button_new_with_label("Move stages");
        gtk_widget_set_tooltip_text(app->arrange_toggle, "Drag a stage in the preview to reorder and repack the stack. Turn off to orbit with the left mouse button.");
        on_signal(app->arrange_toggle, "toggled", [] {
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->arrange_toggle)))
            {
                app->view.yaw = 0;
                app->view.pitch = 1.57079632679;
                redraw_preview();
            }
        });
        gtk_box_pack_start(GTK_BOX(actions), app->arrange_toggle, FALSE, FALSE, 0);
        for (const Preset &p : presets)
        {
            GtkWidget *b = button(p.label, [p] {
                app->view.yaw = p.yaw;
                app->view.pitch = p.pitch;
                redraw_preview();
            });
            gtk_widget_set_tooltip_text(b, p.tip);
            gtk_box_pack_start(GTK_BOX(views), b, FALSE, FALSE, 0);
        }
        GtkWidget *fit = button("Fit", [] {
            app->view.zoom = 1.0;
            app->view.pan_x = app->view.pan_y = 0.0;
            redraw_preview();
        });
        gtk_widget_set_tooltip_text(fit, "Centre the ship and reset the zoom");
        gtk_box_pack_start(GTK_BOX(views), fit, FALSE, FALSE, 8);
        gtk_box_pack_start(GTK_BOX(centre), views, FALSE, FALSE, 0);
        // The GL widget draws the ship; the transparent cairo area over it draws the outlines and labels and takes the mouse.
        GtkWidget *overlay = gtk_overlay_new();
        if (!std::getenv("SHIPEDIT_SOFTWARE"))
        {
            app->gl_area = gtk_gl_area_new();
            gtk_gl_area_set_required_version(GTK_GL_AREA(app->gl_area), 3, 2);
            gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(app->gl_area), FALSE);
            gtk_widget_set_size_request(app->gl_area, 400, 300);
            g_signal_connect(app->gl_area, "render", CB(gl_render_cb), nullptr);
            g_signal_connect(app->gl_area, "realize", CB(gl_realize_cb), nullptr);
            g_signal_connect(app->gl_area, "unrealize", CB(gl_unrealize_cb), nullptr);
            gtk_container_add(GTK_CONTAINER(overlay), app->gl_area);
            gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app->area);
        }
        else
            gtk_container_add(GTK_CONTAINER(overlay), app->area);
        gtk_box_pack_start(GTK_BOX(centre), overlay, TRUE, TRUE, 0);
        app->stats_label = gtk_label_new("");
        gtk_widget_set_halign(app->stats_label, GTK_ALIGN_START);
        gtk_label_set_selectable(GTK_LABEL(app->stats_label), TRUE);
        PangoAttrList *mono = pango_attr_list_new();
        pango_attr_list_insert(mono, pango_attr_family_new("monospace"));
        gtk_label_set_attributes(GTK_LABEL(app->stats_label), mono);
        pango_attr_list_unref(mono);
        gtk_box_pack_start(GTK_BOX(centre), app->stats_label, FALSE, FALSE, 0);
        app->problems_label = gtk_label_new("");
        gtk_widget_set_halign(app->problems_label, GTK_ALIGN_START);
        gtk_label_set_line_wrap(GTK_LABEL(app->problems_label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(app->problems_label), 60);
        GtkWidget *psw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(psw, -1, 90);
        gtk_container_add(GTK_CONTAINER(psw), app->problems_label);
        gtk_box_pack_start(GTK_BOX(centre), psw, FALSE, FALSE, 0);
        gtk_paned_pack1(GTK_PANED(inner), centre, TRUE, TRUE);

        app->form_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(app->form_box), 6);
        GtkWidget *fsw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(fsw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(fsw), app->form_box);
        gtk_widget_set_size_request(fsw, 380, -1);
        gtk_paned_pack2(GTK_PANED(inner), fsw, FALSE, FALSE);
    }

    fs::path detect_root(const char *argv0, const std::string &given)
    {
        if (!given.empty())
            return given;
        if (const char *e = std::getenv("KOSMOS_ROOT"))
            return e;
        std::error_code ec;
        fs::path program = argv0;
        if (!program.has_parent_path())
        {
            gchar *found = g_find_program_in_path(argv0);
            if (found)
            {
                program = found;
                g_free(found);
            }
        }
        fs::path exe = fs::weakly_canonical(program, ec).parent_path();
        for (int i = 0; !ec && i < 6 && !exe.empty(); ++i, exe = exe.parent_path())
        {
            if (fs::is_directory(exe / "addons") && fs::is_directory(exe / "data"))
                return exe;
        }
        return fs::current_path();
    }

    int shot(const std::string &file, const std::string &addon, const std::string &ship, double yaw, double pitch, int w, int h)
    {
        const AddonInfo *found = nullptr;
        for (const AddonInfo &a : app->addons)
            if (a.id == addon)
                found = &a;
        if (!found)
        {
            std::fprintf(stderr, "no addon '%s'\n", addon.c_str());
            return 1;
        }
        std::string file_name = ship, error;
        if (file_name.empty() && !found->ships.empty())
            file_name = found->ships[0];
        if (!app->doc.load(*found, file_name, error))
        {
            std::fprintf(stderr, "load failed: %s\n", error.c_str());
            return 1;
        }
        app->view.yaw = yaw;
        app->view.pitch = pitch;
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(s);
        draw_preview(cr, w, h, app->doc, app->sel, app->view, app->meshes);
        cairo_destroy(cr);
        const bool ok = cairo_surface_write_to_png(s, file.c_str()) == CAIRO_STATUS_SUCCESS;
        cairo_surface_destroy(s);
        return ok ? 0 : 1;
    }
} // namespace

int main(int argc, char **argv)
{
    App state;
    app = &state;
    std::string root_arg, addon_arg, ship_arg, shot_file;
    double yaw = state.view.yaw, pitch = state.view.pitch;
    int w = 1000, h = 700;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--root" && more)
            root_arg = argv[++i];
        else if (a == "--addon" && more)
            addon_arg = argv[++i];
        else if (a == "--ship" && more)
            ship_arg = argv[++i];
        else if (a == "--shot" && more)
            shot_file = argv[++i];
        else if (a == "--yaw" && more)
            yaw = std::atof(argv[++i]) * 3.14159265 / 180.0;
        else if (a == "--pitch" && more)
            pitch = std::atof(argv[++i]) * 3.14159265 / 180.0;
        else if (a == "--size" && more)
            std::sscanf(argv[++i], "%dx%d", &w, &h);
    }
    state.root = detect_root(argv[0], root_arg);
    state.addons_dir = state.root / "addons";
    state.addons = scan_addons(state.addons_dir);

    if (!shot_file.empty())
        return shot(shot_file, addon_arg, ship_arg, yaw, pitch, w, h);

    kosmos_gtk_schemas("shipedit");
    gtk_init(&argc, &argv);
    build_window();
    gtk_widget_show_all(state.window);
    int pick = 0;
    for (std::size_t i = 0; i < state.addons.size(); ++i)
        if (state.addons[i].id == addon_arg)
            pick = static_cast<int>(i);
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.addon_combo), pick);
    if (!ship_arg.empty())
    {
        const auto &ships = state.addons[static_cast<std::size_t>(pick)].ships;
        for (std::size_t i = 0; i < ships.size(); ++i)
            if (ships[i] == ship_arg)
                gtk_combo_box_set_active(GTK_COMBO_BOX(state.ship_combo), static_cast<int>(i));
    }
    state.doc.make_rocket("New Rocket");
    state.view.yaw = 0;
    state.view.pitch = 1.57079632679;
    structure_changed({Selection::ship, -1, -1});
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.arrange_toggle), TRUE);
    if (!state.addons.empty())
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.library_addon), pick);
    if (!addon_arg.empty())
        load_selected();
    gtk_main();
    return 0;
}
