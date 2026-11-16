#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#define main shipedit_program_main
#include "../sdk/shipedit/src/main.cpp"
#undef main

namespace
{
    GtkWidget *find_button(GtkWidget *widget, const char *label)
    {
        if (GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0)
            return widget;
        if (!GTK_IS_CONTAINER(widget)) return nullptr;
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        GtkWidget *found = nullptr;
        for (GList *p = children; p && !found; p = p->next)
            found = find_button(GTK_WIDGET(p->data), label);
        g_list_free(children);
        return found;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    gtk_init(&argc, &argv);
    App state;
    app = &state;
    state.root = argv[1];
    state.addons_dir = state.root / "addons";
    state.addons = scan_addons(state.addons_dir);
    build_window();
    state.doc.make_rocket("Port UI test");
    state.doc.add_stage();
    gtk_widget_show_all(state.window);
    structure_changed({Selection::ship, -1, -1});
    GtkWidget *button = find_button(state.window, "+ Port");
    assert(button);
    gtk_button_clicked(GTK_BUTTON(button));
    assert(state.sel.kind == Selection::port);
    assert(state.doc.root()["ports"].size() == 1 && cur());
    commit_value("position", json::array({0, 4.5, 15}), false);
    commit_value("stage", 0, false);
    assert(state.doc.root()["ports"][0]["position"] == json::array({0, 4.5, 15}));
    assert(state.doc.root()["ports"][0]["stage"] == 0);
    assert(state.doc.undo());
    assert(state.doc.root()["ports"][0]["stage"] == -1);
    assert(state.doc.redo());
    assert(state.doc.root()["ports"][0]["stage"] == 0);
    g_signal_handlers_disconnect_by_func(state.window, reinterpret_cast<gpointer>(gtk_main_quit), nullptr);
    gtk_widget_destroy(state.window);
    destroy_scenario_page(state.scenario);
    return 0;
}
