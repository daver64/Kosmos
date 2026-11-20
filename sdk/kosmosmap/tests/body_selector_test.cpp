#define main kosmosmap_program_main
#include "../src/main.cpp"
#undef main

#include <sstream>

namespace
{
    int failures = 0;
    void check(bool result, const char *description)
    {
        std::printf("%s: %s\n", result ? "ok" : "FAIL", description);
        failures += !result;
    }

    bool contains(const std::vector<kmap::BodyInfo> &bodies, const std::string &name, const std::string &parent, double radius)
    {
        return std::any_of(bodies.begin(), bodies.end(), [&](const auto &body) {
            return body.name == name && body.parent == parent && body.radius == radius;
        });
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: body_selector_test KOSMOS_ROOT [--gtk] [--live]\n");
        return 1;
    }
    const std::string root = argv[1];
    bool use_gtk = false, live = false;
    for (int i = 2; i < argc; ++i)
    {
        use_gtk |= std::string(argv[i]) == "--gtk";
        live |= std::string(argv[i]) == "--live";
    }
    std::ifstream input(root + "/data/sol.json");
    std::vector<kmap::BodyInfo> bodies;
    std::string error;
    check(input.is_open() && kmap::read_body_catalog(input, bodies, error), "load shipped solar system");
    check(contains(bodies, "Earth", "Sun", 6371000.0) &&
          contains(bodies, "Moon", "Earth", 1737400.0) &&
          contains(bodies, "Titan", "Saturn", 2574700.0), "catalogue contains planets and moons with radii and parents");
    std::istringstream malformed(R"({"bodies":[{"name":"Earth","radius":0}]})");
    const auto previous = bodies.size();
    check(!kmap::read_body_catalog(malformed, bodies, error) && !error.empty() && bodies.size() == previous,
          "invalid body list reports an error without replacing the catalogue");

    if (use_gtk)
    {
        int gtk_argc = 1;
        char *gtk_argv[] = {argv[0], nullptr};
        char **gtk_args = gtk_argv;
        if (!gtk_init_check(&gtk_argc, &gtk_args))
        {
            std::fprintf(stderr, "Cannot open a headless GTK display\n");
            return 1;
        }
        App app;
        app.root = root;
        app.bodies = bodies;
        app.view.w = 640;
        app.view.h = 480;
        app.area = gtk_drawing_area_new();
        app.follow_button = gtk_toggle_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.follow_button), TRUE);
        g_signal_connect(app.follow_button, "toggled", G_CALLBACK(on_follow), &app);
        app.body_box = gtk_combo_box_text_new();
        g_signal_connect(app.body_box, "changed", G_CALLBACK(on_body), &app);
        if (live)
        {
            if (!connect_link(app) || on_tick(&app) != G_SOURCE_CONTINUE || !app.have_flown)
            {
                std::fprintf(stderr, "Cannot obtain a flown ship from Kosmos\n");
                ++failures;
            }
        }
        else
        {
            set_ground(app, "Earth", 6371000.0);
            app.have_flown = true;
            app.flown.body = "Earth";
            app.flown.lat = 12.0;
            app.flown.lon = 34.0;
        }
        if (app.have_flown)
        {
            const std::string flown_body = app.flown.body;
            const std::string target = flown_body == "Earth" ? "Moon" : "Earth";
            check(gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.body_box), target.c_str()) &&
                  app.ground->name == target && !app.follow, "selecting a body opens it and stops follow");
            if (live)
            {
                on_tick(&app);
                check(app.ground->name == target && !app.follow, "live updates preserve manual browsing");
                for (int i = 0; i < 100 && app.catalog_loading; ++i)
                    on_tick(&app);
                check(!app.catalog_loading && static_cast<int>(app.bodies.size()) == app.catalog_count &&
                      app.ground->name == target && !app.follow,
                      "full live system catalogue loads without leaving the selected body");
            }
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.follow_button), TRUE);
            check(app.follow && app.ground->name == flown_body &&
                  app.view.lat == app.flown.lat && app.view.lon == app.flown.lon,
                  "Follow returns to the flown body and ship");
            check(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.body_box)) &&
                  flown_body == gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.body_box)),
                  "selector follows the displayed body");
            if (!live)
            {
                check(gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.body_box), "Itokawa") &&
                      app.ground->name == "Itokawa" && min_ppd(app) <= max_ppd(app),
                      "small bodies have a valid zoom range");
            }
        }
        if (app.render_timer)
            g_source_remove(app.render_timer);
        gtk_widget_destroy(app.body_box);
        gtk_widget_destroy(app.follow_button);
        gtk_widget_destroy(app.area);
        if (app.cache.surface)
            cairo_surface_destroy(app.cache.surface);
    }
    return failures ? 1 : 0;
}
