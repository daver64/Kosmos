#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <string>

#include "scenario_page.h"

using namespace shipedit;

namespace
{
    GtkWidget *find_child(GtkWidget *widget, GType type, const char *label = nullptr)
    {
        if (g_type_is_a(G_OBJECT_TYPE(widget), type) &&
            (!label || (GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0)))
            return widget;
        if (!GTK_IS_CONTAINER(widget))
            return nullptr;
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        GtkWidget *found = nullptr;
        for (GList *p = children; p && !found; p = p->next)
            found = find_child(GTK_WIDGET(p->data), type, label);
        g_list_free(children);
        return found;
    }

    GtkWidget *dialog(const char *title)
    {
        GList *windows = gtk_window_list_toplevels();
        GtkWidget *found = nullptr;
        for (GList *p = windows; p; p = p->next)
            if (GTK_IS_DIALOG(p->data) &&
                g_strcmp0(gtk_window_get_title(GTK_WINDOW(p->data)), title) == 0)
                found = GTK_WIDGET(p->data);
        g_list_free(windows);
        return found;
    }

    bool select_ship(GtkWidget *combo, const char *prefix)
    {
        GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
        GtkTreeIter it;
        if (!gtk_tree_model_get_iter_first(model, &it)) return false;
        int index = 0;
        do
        {
            gchar *label = nullptr;
            gtk_tree_model_get(model, &it, 0, &label, -1);
            const bool found = label && g_str_has_prefix(label, prefix);
            g_free(label);
            if (found) { gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index); return true; }
            ++index;
        } while (gtk_tree_model_iter_next(model, &it));
        return false;
    }

    gboolean add_carrier(gpointer)
    {
        GtkWidget *d = dialog("Add ship / piggyback");
        assert(d);
        GtkWidget *grid = find_child(gtk_dialog_get_content_area(GTK_DIALOG(d)), GTK_TYPE_GRID);
        assert(grid && select_ship(gtk_grid_get_child_at(GTK_GRID(grid), 1, 0), "CVEL Energia ("));
        gtk_entry_set_text(GTK_ENTRY(gtk_grid_get_child_at(GTK_GRID(grid), 1, 3)), "baikonur");
        gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
        return G_SOURCE_REMOVE;
    }

    gboolean scenario_settings(gpointer)
    {
        GtkWidget *d = dialog("Scenario settings");
        assert(d);
        GtkWidget *grid = find_child(gtk_dialog_get_content_area(GTK_DIALOG(d)), GTK_TYPE_GRID);
        assert(grid);
        gtk_entry_set_text(GTK_ENTRY(gtk_grid_get_child_at(GTK_GRID(grid), 1, 0)), "ShipEdit Buran test");
        gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
        return G_SOURCE_REMOVE;
    }

    gboolean add_orbiter(gpointer)
    {
        GtkWidget *d = dialog("Add ship / piggyback");
        assert(d);
        GtkWidget *grid = find_child(gtk_dialog_get_content_area(GTK_DIALOG(d)), GTK_TYPE_GRID);
        assert(grid && select_ship(gtk_grid_get_child_at(GTK_GRID(grid), 1, 0), "CVEL Buran ("));
        gtk_combo_box_set_active(GTK_COMBO_BOX(gtk_grid_get_child_at(GTK_GRID(grid), 1, 1)), 3);
        gtk_combo_box_set_active(GTK_COMBO_BOX(gtk_grid_get_child_at(GTK_GRID(grid), 1, 6)), 0);
        gtk_combo_box_set_active(GTK_COMBO_BOX(gtk_grid_get_child_at(GTK_GRID(grid), 1, 7)), 1);
        gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
        return G_SOURCE_REMOVE;
    }

    gboolean save_scenario(gpointer data)
    {
        GtkWidget *d = dialog("Save scenario");
        assert(d && GTK_IS_FILE_CHOOSER(d));
        const fs::path path = static_cast<const char *>(data);
        assert(gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), path.parent_path().string().c_str()));
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), path.filename().string().c_str());
        gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
        return G_SOURCE_REMOVE;
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    assert(!fs::exists(argv[2]));
    gtk_init(&argc, &argv);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    ScenarioPage *page = create_scenario_page(GTK_WINDOW(window), argv[1]);
    GtkWidget *widget = scenario_page_widget(page);
    gtk_container_add(GTK_CONTAINER(window), widget);
    gtk_widget_show_all(window);
    GtkWidget *settings = find_child(widget, GTK_TYPE_BUTTON, "Scenario settings...");
    assert(settings);
    g_idle_add(scenario_settings, nullptr);
    gtk_button_clicked(GTK_BUTTON(settings));
    GtkWidget *add = find_child(widget, GTK_TYPE_BUTTON, "Add ship / piggyback...");
    assert(add);
    g_idle_add(add_carrier, nullptr);
    gtk_button_clicked(GTK_BUTTON(add));
    g_idle_add(add_orbiter, nullptr);
    gtk_button_clicked(GTK_BUTTON(add));
    GtkWidget *editor = find_child(widget, GTK_TYPE_TEXT_VIEW);
    assert(editor);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    json scenario = json::parse(content);
    g_free(content);
    assert(scenario["name"] == "ShipEdit Buran test");
    assert(scenario["ships"].size() == 2);
    assert(scenario["ships"][1]["dock"] == json({{"to", 0}, {"port", 0}, {"own_port", 1}}));
    assert(scenario["ships"][1]["ship"] == "CVEL Buran");
    GtkWidget *save = find_child(widget, GTK_TYPE_BUTTON, "Save as...");
    assert(save);
    g_idle_add(save_scenario, argv[2]);
    gtk_button_clicked(GTK_BUTTON(save));
    std::ifstream in(argv[2]);
    assert(in.good());
    json saved = json::parse(in);
    assert(saved == scenario);
    gtk_widget_destroy(window);
    destroy_scenario_page(page);
    return 0;
}
