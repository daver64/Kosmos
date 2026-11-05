// KosmosMission: a mission planner. Pick a start (a base or an orbit), a ship and a payload, then add legs (ascent, Hohmann, transfers to other bodies,
// plane changes) and read off the delta-v ledger against the ship's stage budget. Missions save as Kosmos scenarios; a leg can be pushed to the running game
// as a manoeuvre node over KosmosLink. The planning maths is in core/ and has no GTK in it (tests/missioncore_test.cpp).
#include "bodies.h"
#include "budget.h"
#include "gl_view.h"
#include "mission.h"
#include "transfer.h"

#include "json.hpp"
#include "kosmos_link.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace km;
namespace fs = std::filesystem;

namespace
{
    struct Polyline
    {
        std::vector<Vec> pts;
        float r, g, b;
    };

    // What the GL area shows: the last planned transfer's geometry, or its porkchop grid.
    struct Viz
    {
        std::vector<Polyline> lines;
        Porkchop grid;
        bool has_grid = false;
        double best_x = 0, best_y = 0;
        std::string caption;
    };

    struct BaseInfo
    {
        std::string id, name, body;
        double latitude = 0;
    };

    struct Place
    {
        std::string body = "Earth";
        double altitude = 400000;
        double clock = 0; // seconds since the epoch, when the next burn can start
    };

    struct App
    {
        std::string root;
        Bodies bodies;
        std::vector<ShipSpec> ships;
        std::vector<BaseInfo> bases;
        AscentTable table;
        Mission mission;
        Place place;
        Viz viz;
        bool show_porkchop = false;
        float zoom = 1.0f;
        std::unique_ptr<kosmos::Link> link;
        kmview::Renderer renderer;
        kmview::Scene scene;
        bool gl_ready = false;

        GtkWidget *window, *gl, *status, *info, *tree, *budget_label;
        GtkWidget *start_body, *start_base, *start_alt, *start_inc, *ship_combo, *payload, *margin;
        GtkWidget *kind, *target_combo, *alt_entry, *angle_entry, *dv_entry, *name_entry;
        GtkWidget *view_toggle;
        GtkListStore *store;
    };

    App app;

    double entry_number(GtkWidget *e, double fallback)
    {
        const char *t = gtk_entry_get_text(GTK_ENTRY(e));
        char *end = nullptr;
        const double v = std::strtod(t, &end);
        return end == t ? fallback : v;
    }

    void say(const std::string &s) { gtk_label_set_text(GTK_LABEL(app.status), s.c_str()); }

    const ShipSpec *current_ship()
    {
        const int i = gtk_combo_box_get_active(GTK_COMBO_BOX(app.ship_combo));
        return i >= 0 && i < static_cast<int>(app.ships.size()) ? &app.ships[i] : nullptr;
    }

    double available_dv()
    {
        const ShipSpec *s = current_ship();
        return s ? budget(*s, app.mission.payload).total : 0.0;
    }

    std::string find_root()
    {
        if (const char *e = std::getenv("KOSMOS_ROOT"))
            return e;
        char buf[4096];
#ifdef _WIN32
        const long n = static_cast<long>(GetModuleFileNameA(nullptr, buf, sizeof buf - 1));
#else
        const long n = static_cast<long>(readlink("/proc/self/exe", buf, sizeof buf - 1));
#endif
        fs::path p = n > 0 ? fs::path(std::string(buf, static_cast<size_t>(n))).parent_path() : fs::current_path();
        for (int i = 0; i < 6; ++i, p = p.parent_path())
            if (fs::exists(p / "data" / "sol.json"))
                return p.string();
        return fs::current_path().string();
    }

    void load_bases()
    {
        std::error_code ec;
        for (const auto &f : fs::directory_iterator(fs::path(app.root) / "data" / "bases", ec))
        {
            if (f.path().extension() != ".json")
                continue;
            std::ifstream in(f.path());
            nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_discarded() || !j.contains("bases"))
                continue;
            for (const nlohmann::json &b : j["bases"])
                app.bases.push_back({b.value("id", ""), b.value("name", ""), b.value("body", "Earth"), b.value("latitude", 0.0)});
        }
        std::sort(app.bases.begin(), app.bases.end(), [](const BaseInfo &a, const BaseInfo &b) { return a.name < b.name; });
    }

    // ---- Geometry for the views -------------------------------------------------------------------------------------------------------------

    void add_orbit_line(const Orbit &o, double from_nu, double to_nu, float r, float g, float b, std::vector<Polyline> &out, int n = 180)
    {
        Polyline pl{{}, r, g, b};
        for (int i = 0; i <= n; ++i)
        {
            Vec p, v;
            state_at(o, from_nu + (to_nu - from_nu) * i / n, p, v);
            pl.pts.push_back(p);
        }
        out.push_back(pl);
    }

    void add_circle(double radius, float r, float g, float b, std::vector<Polyline> &out)
    {
        Polyline pl{{}, r, g, b};
        for (int i = 0; i <= 120; ++i)
            pl.pts.push_back({radius * std::cos(TAU * i / 120), radius * std::sin(TAU * i / 120), 0});
        out.push_back(pl);
    }

    void add_marker(Vec p, double size, float r, float g, float b, std::vector<Polyline> &out)
    {
        out.push_back({{{p.x - size, p.y, 0}, {p.x + size, p.y, 0}}, r, g, b});
        out.push_back({{{p.x, p.y - size, 0}, {p.x, p.y + size, 0}}, r, g, b});
    }

    void add_transfer_arc(const Transfer &t, double mu, std::vector<Polyline> &out)
    {
        const auto o = make_orbit(t.r_dep, t.v_dep, mu);
        if (!o)
            return;
        Polyline pl{{}, 1.0f, 0.8f, 0.2f};
        for (int i = 0; i <= 120; ++i)
        {
            Vec r, v;
            state_at(*o, advance(*o, o->nu, t.tof * i / 120), r, v);
            pl.pts.push_back(r);
        }
        out.push_back(pl);
    }

    void build_scene()
    {
        kmview::Scene &s = app.scene;
        s.lines.clear();
        s.tris.clear();
        s.offset_x = s.offset_y = 0;
        s.scale_x = s.scale_y = 1;
        if (app.show_porkchop && app.viz.has_grid)
        {
            const Porkchop &g = app.viz.grid;
            double best = 1e30, worst = 0;
            for (double v : g.dv)
                if (v >= 0)
                {
                    best = std::min(best, v);
                    worst = std::max(worst, v);
                }
            const double hi = std::min(worst, best * 1.5);
            const auto colour = [&](double v, float &r, float &gg, float &b) {
                if (v < 0)
                {
                    r = gg = b = 0.08f;
                    return;
                }
                if (v > hi)
                {
                    r = 0.28f;
                    gg = 0.08f;
                    b = 0.1f;
                    return;
                }
                const float t = static_cast<float>(clamp((v - best) / std::max(hi - best, 1.0), 0.0, 1.0));
                r = t;
                gg = 1.0f - 0.7f * t;
                b = 0.25f * (1 - t);
            };
            const auto px = [&](double ix) { return static_cast<float>(-0.95 + 1.9 * ix / g.nx); };
            const auto py = [&](double iy) { return static_cast<float>(-0.95 + 1.9 * iy / g.ny); };
            for (int iy = 0; iy < g.ny; ++iy)
                for (int ix = 0; ix < g.nx; ++ix)
                {
                    float r, gg, b;
                    colour(g.at(ix, iy), r, gg, b);
                    const float x0 = px(ix), x1 = px(ix + 1), y0 = py(iy), y1 = py(iy + 1);
                    s.tris.insert(s.tris.end(), {{x0, y0, r, gg, b}, {x1, y0, r, gg, b}, {x1, y1, r, gg, b}, {x0, y0, r, gg, b}, {x1, y1, r, gg, b}, {x0, y1, r, gg, b}});
                }
            const float bx = px((app.viz.best_x - g.x0) / g.dx + 0.5), by = py((app.viz.best_y - g.y0) / g.dy + 0.5);
            s.lines.insert(s.lines.end(), {{bx - 0.03f, by, 1, 1, 1}, {bx + 0.03f, by, 1, 1, 1}, {bx, by - 0.03f, 1, 1, 1}, {bx, by + 0.03f, 1, 1, 1}});
            return;
        }
        double extent = 1;
        for (const Polyline &pl : app.viz.lines)
            for (const Vec &p : pl.pts)
                extent = std::max({extent, std::fabs(p.x), std::fabs(p.y)});
        const float k = static_cast<float>(0.92 / extent) * app.zoom;
        s.scale_x = s.scale_y = k; // the widget's aspect is applied in draw
        for (const Polyline &pl : app.viz.lines)
            for (size_t i = 1; i < pl.pts.size(); ++i)
            {
                s.lines.push_back({static_cast<float>(pl.pts[i - 1].x), static_cast<float>(pl.pts[i - 1].y), pl.r, pl.g, pl.b});
                s.lines.push_back({static_cast<float>(pl.pts[i].x), static_cast<float>(pl.pts[i].y), pl.r, pl.g, pl.b});
            }
    }

    void redraw()
    {
        build_scene();
        gtk_widget_queue_draw(app.gl);
    }

    // ---- The ledger ---------------------------------------------------------------------------------------------------------------------------

    void refresh_ledger()
    {
        gtk_list_store_clear(app.store);
        const double avail = available_dv();
        const std::vector<LedgerRow> rows = ledger(app.mission, avail);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            GtkTreeIter it;
            gtk_list_store_append(app.store, &it);
            char a[32], b[32], c[32];
            std::snprintf(a, sizeof a, "%.0f", rows[i].dv);
            std::snprintf(b, sizeof b, "%.0f", rows[i].cumulative);
            std::snprintf(c, sizeof c, "%.0f", rows[i].remaining);
            gtk_list_store_set(app.store, &it, 0, rows[i].name.c_str(), 1, leg_type_name(app.mission.legs[i].type), 2, a, 3, b, 4, c, 5,
                               rows[i].short_of ? "#ff8080" : nullptr, -1);
        }
        const ShipSpec *s = current_ship();
        std::string text = "No ship";
        if (s)
        {
            const Budget bud = budget(*s, app.mission.payload);
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s: %.0f m/s from %zu stage(s), %.0f kg payload. Mission needs %.0f m/s (%.0f%% reserve held).", s->name.c_str(),
                          bud.total, s->stages.size(), app.mission.payload, app.mission.total_dv(), app.mission.margin * 100);
            text = buf;
        }
        gtk_label_set_text(GTK_LABEL(app.budget_label), text.c_str());
    }

    void sync_mission_from_ui()
    {
        Mission &m = app.mission;
        m.start.body = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app.start_body)) ? gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app.start_body)) : "Earth";
        const int bi = gtk_combo_box_get_active(GTK_COMBO_BOX(app.start_base));
        m.start.base.clear();
        m.start.latitude = 0;
        if (bi > 0)
        {
            int n = 0;
            for (const BaseInfo &b : app.bases)
                if (b.body == m.start.body && ++n == bi)
                {
                    m.start.base = b.id;
                    m.start.latitude = b.latitude;
                }
        }
        m.start.altitude = entry_number(app.start_alt, 400) * 1000.0;
        m.start.inclination = entry_number(app.start_inc, 0);
        m.payload = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app.payload));
        m.margin = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app.margin)) / 100.0;
        if (const ShipSpec *s = current_ship())
        {
            m.ship = s->name;
            m.ship_file = s->file;
        }
    }

    void reset_place()
    {
        sync_mission_from_ui();
        app.place.body = app.mission.start.body;
        app.place.altitude = app.mission.start.altitude;
        app.place.clock = 0;
    }

    // ---- Adding legs --------------------------------------------------------------------------------------------------------------------------

    void push_leg(LegType type, const std::string &name, const std::string &body, const std::string &to, double dv, double duration, double depart,
                  double altitude, const std::string &note)
    {
        Leg l;
        l.type = type;
        l.name = name;
        l.body = body;
        l.to_body = to;
        l.dv = dv;
        l.duration = duration;
        l.depart_days = depart / DAY;
        l.altitude = altitude;
        l.note = note;
        app.mission.legs.push_back(l);
    }

    const Body *place_body()
    {
        const Body *b = app.bodies.find(app.place.body);
        if (!b)
            say("Unknown body " + app.place.body);
        return b;
    }

    void add_ascent()
    {
        sync_mission_from_ui();
        const Body *b = app.bodies.find(app.mission.start.body);
        if (!b)
            return;
        if (app.mission.start.in_orbit())
        {
            say("The mission starts in orbit; choose a base to add an ascent.");
            return;
        }
        const Ascent a = ascent(*b, app.mission.start.altitude, app.mission.start.latitude, app.mission.ship, app.table);
        push_leg(LegType::Ascent, "Ascent to " + std::to_string(int(app.mission.start.altitude / 1000)) + " km", b->name, "", a.dv, 600, 0,
                 app.mission.start.altitude, a.note);
        app.place = {b->name, app.mission.start.altitude, 600};
        app.viz = Viz{};
        add_circle(b->radius, 0.5f, 0.5f, 0.55f, app.viz.lines);
        add_circle(b->radius + app.mission.start.altitude, 0.3f, 0.9f, 0.4f, app.viz.lines);
        app.viz.caption = "Ascent: orbital " + std::to_string(int(a.v_orbit)) + " - ground " + std::to_string(int(a.v_rotation)) + " + loss " +
                          std::to_string(int(a.loss)) + " m/s" + (a.measured ? " (measured)" : " (estimate)");
        app.show_porkchop = false;
    }

    void add_hohmann_local(double target_alt)
    {
        const Body *b = place_body();
        if (!b)
            return;
        const double r1 = b->radius + app.place.altitude, r2 = b->radius + target_alt;
        const Hohmann h = hohmann(b->gm, r1, r2);
        const std::string where = std::to_string(int(target_alt / 1000)) + " km";
        push_leg(LegType::Transfer, "Hohmann burn 1 to " + where, b->name, "", std::fabs(h.dv1), h.tof, app.place.clock, app.place.altitude, "");
        push_leg(LegType::Arrival, "Circularise at " + where, b->name, "", std::fabs(h.dv2), 0, (app.place.clock + h.tof), target_alt, "");
        app.viz = Viz{};
        add_circle(b->radius, 0.5f, 0.5f, 0.55f, app.viz.lines);
        add_circle(r1, 0.3f, 0.9f, 0.4f, app.viz.lines);
        add_circle(r2, 0.4f, 0.6f, 1.0f, app.viz.lines);
        const double a = (r1 + r2) / 2, e = std::fabs(r2 - r1) / (r1 + r2);
        Orbit o = orbit_from_elements(b->gm, a, e, 0, 0, r2 > r1 ? 0 : PI, 0);
        add_orbit_line(o, 0, PI, 1.0f, 0.8f, 0.2f, app.viz.lines);
        app.viz.caption = "Hohmann: " + std::to_string(int(std::fabs(h.dv1))) + " + " + std::to_string(int(std::fabs(h.dv2))) + " m/s, " +
                          std::to_string(int(h.tof / 60)) + " min";
        app.place.altitude = target_alt;
        app.place.clock += h.tof;
        app.show_porkchop = false;
    }

    void add_plane_change(double degrees)
    {
        const Body *b = place_body();
        if (!b)
            return;
        const double v = std::sqrt(b->gm / (b->radius + app.place.altitude));
        push_leg(LegType::Plane, "Plane change " + std::to_string(int(degrees)) + " deg", b->name, "", plane_change_dv(v, degrees * PI / 180), 0, app.place.clock,
                 app.place.altitude, "Burn at a node of the two planes.");
    }

    void add_transfer(const std::string &to_name, double arr_alt)
    {
        const Body *from = place_body();
        const Body *to = app.bodies.find(to_name);
        if (!from || !to)
        {
            say("Choose a destination body.");
            return;
        }
        std::string err;
        const double r_dep = from->radius + app.place.altitude, r_arr = to->radius + arr_alt;
        app.viz = Viz{};
        Transfer t;
        double central_gm = 0;

        if (to->name == from->parent)
        {
            t = return_to_parent(app.bodies, from->name, r_dep, r_arr, err);
            if (!t.valid)
            {
                say(err);
                return;
            }
            t.depart = app.place.clock;
            push_leg(LegType::Transfer, "Escape " + from->name + " to " + to->name, from->name, to->name, t.dv_dep, t.tof, app.place.clock, app.place.altitude,
                     t.note);
            push_leg(LegType::Arrival, "Circularise at " + to->name, to->name, "", t.dv_arr, 0, app.place.clock + t.tof, arr_alt, "");
            add_circle(from->a, 0.5f, 0.6f, 1.0f, app.viz.lines);
            add_circle(r_arr, 0.3f, 0.9f, 0.4f, app.viz.lines);
            app.viz.caption = "Return: escape " + std::to_string(int(t.dv_dep)) + " + circularise " + std::to_string(int(t.dv_arr)) + " m/s";
        }
        else if (to->parent == from->name)
        {
            // The ship's circular orbit lies in the moon's orbital plane (see the leg note).
            const Orbit mo = app.bodies.orbit_of(*to);
            const double v = std::sqrt(from->gm / r_dep);
            LambertProblem p;
            if (!moon_problem(app.bodies, from->name, to->name, mo.P * r_dep, mo.Q * v, r_arr, p, err))
            {
                say(err);
                return;
            }
            const double ht = hohmann(from->gm, r_dep, to->a).tof;
            const Porkchop g = porkchop(p, app.place.clock, 3 * DAY, 0.4 * ht, 1.6 * ht, 120, 60);
            t = optimise(p, g);
            if (!t.valid)
            {
                say("No transfer found; try a different altitude.");
                return;
            }
            push_leg(LegType::Transfer, "Depart for " + to->name, from->name, to->name, t.dv_dep, t.tof, t.depart, app.place.altitude,
                     "Assumes the parking orbit lies in " + to->name + "'s orbital plane.");
            push_leg(LegType::Arrival, "Capture at " + to->name, to->name, "", t.dv_arr, 0, t.depart + t.tof, arr_alt, "");
            central_gm = from->gm;
            app.viz.grid = g;
            app.viz.has_grid = true;
            app.viz.best_x = t.depart;
            app.viz.best_y = t.tof;
            add_circle(to->a, 0.5f, 0.6f, 1.0f, app.viz.lines);
            add_circle(r_dep, 0.3f, 0.9f, 0.4f, app.viz.lines);
            add_transfer_arc(t, central_gm, app.viz.lines);
            Vec mr, mv;
            app.bodies.state(*to, t.depart + t.tof, mr, mv);
            add_marker(mr, to->a * 0.03, 0.5f, 0.7f, 1.0f, app.viz.lines);
            app.viz.caption = "Depart in " + std::to_string(t.depart / 3600 - app.place.clock / 3600) + " h";
        }
        else if (to->parent == from->parent && from->has_orbit && to->has_orbit)
        {
            LambertProblem p;
            if (!sibling_problem(app.bodies, from->name, to->name, r_dep, r_arr, p, err))
            {
                say(err);
                return;
            }
            const Body *sun = app.bodies.find(from->parent);
            const Hohmann h = hohmann(sun->gm, from->a, to->a);
            const double span = std::min(h.synodic > 0 ? h.synodic : 400 * DAY, 2500 * DAY);
            const Porkchop g = porkchop(p, app.place.clock, span, 0.5 * h.tof, 1.3 * h.tof, 100, 60);
            t = optimise(p, g);
            if (!t.valid)
            {
                say("No transfer found in the next synodic period.");
                return;
            }
            push_leg(LegType::Transfer, "Eject " + from->name + " to " + to->name, from->name, to->name, t.dv_dep, t.tof, t.depart, app.place.altitude,
                     "Ejection from a circular parking orbit; plane of the parking orbit is free.");
            push_leg(LegType::Arrival, "Capture at " + to->name, to->name, "", t.dv_arr, 0, t.depart + t.tof, arr_alt, "Circular capture orbit.");
            app.viz.grid = g;
            app.viz.has_grid = true;
            app.viz.best_x = t.depart;
            app.viz.best_y = t.tof;
            add_orbit_line(app.bodies.orbit_of(*from), 0, TAU, 0.3f, 0.9f, 0.4f, app.viz.lines);
            add_orbit_line(app.bodies.orbit_of(*to), 0, TAU, 0.8f, 0.4f, 0.3f, app.viz.lines);
            add_transfer_arc(t, sun->gm, app.viz.lines);
            Vec r, v;
            app.bodies.state(*from, t.depart, r, v);
            add_marker(r, sun->gm > 0 ? to->a * 0.03 : 1, 0.3f, 0.9f, 0.4f, app.viz.lines);
            app.bodies.state(*to, t.depart + t.tof, r, v);
            add_marker(r, to->a * 0.03, 1.0f, 0.5f, 0.3f, app.viz.lines);
            char buf[160];
            std::snprintf(buf, sizeof buf, "Depart day %.0f, flight %.0f d, v-inf %.2f / %.2f km/s", t.depart / DAY, t.tof / DAY, t.vinf_dep / 1000,
                          t.vinf_arr / 1000);
            app.viz.caption = buf;
        }
        else
        {
            say(from->name + " to " + to->name + " is not supported directly: route the mission through the common parent body.");
            return;
        }
        app.place = {to->name, arr_alt, t.depart + t.tof};
        app.show_porkchop = false;
    }

    void add_custom(const std::string &name, double dv)
    {
        push_leg(LegType::Custom, name.empty() ? "Burn" : name, app.place.body, "", dv, 0, app.place.clock, app.place.altitude, "");
    }

    // ---- Callbacks ----------------------------------------------------------------------------------------------------------------------------

    void refresh_all()
    {
        refresh_ledger();
        char buf[512];
        std::snprintf(buf, sizeof buf, "%s   |   now at %s, %.0f km, day %.1f", app.viz.caption.c_str(), app.place.body.c_str(), app.place.altitude / 1000,
                      app.place.clock / DAY);
        gtk_label_set_text(GTK_LABEL(app.info), buf);
        redraw();
    }

    void on_add(GtkWidget *, gpointer)
    {
        sync_mission_from_ui();
        if (app.mission.legs.empty() && !app.mission.start.base.empty())
        {
            // nothing yet: the planner starts from the chosen orbit
        }
        if (app.mission.legs.empty())
            reset_place();
        const int kind = gtk_combo_box_get_active(GTK_COMBO_BOX(app.kind));
        const std::string target = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app.target_combo))
                                       ? gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app.target_combo))
                                       : "";
        const double alt = entry_number(app.alt_entry, 200) * 1000.0;
        switch (kind)
        {
        case 0:
            add_ascent();
            break;
        case 1:
            add_hohmann_local(alt);
            break;
        case 2:
            add_plane_change(entry_number(app.angle_entry, 0));
            break;
        case 3:
            add_transfer(target, alt);
            break;
        default:
            add_custom(gtk_entry_get_text(GTK_ENTRY(app.name_entry)), entry_number(app.dv_entry, 0));
            break;
        }
        say("");
        refresh_all();
    }

    void on_remove(GtkWidget *, gpointer)
    {
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app.tree));
        GtkTreeModel *model;
        GtkTreeIter it;
        if (!gtk_tree_selection_get_selected(sel, &model, &it))
        {
            if (!app.mission.legs.empty())
                app.mission.legs.pop_back();
        }
        else
        {
            GtkTreePath *p = gtk_tree_model_get_path(model, &it);
            const int i = gtk_tree_path_get_indices(p)[0];
            gtk_tree_path_free(p);
            if (i >= 0 && i < static_cast<int>(app.mission.legs.size()))
                app.mission.legs.erase(app.mission.legs.begin() + i);
        }
        refresh_all();
    }

    void on_clear(GtkWidget *, gpointer)
    {
        app.mission.legs.clear();
        app.viz = Viz{};
        reset_place();
        refresh_all();
    }

    void on_param_changed(GtkWidget *, gpointer)
    {
        sync_mission_from_ui();
        refresh_ledger();
    }

    void on_body_changed(GtkWidget *, gpointer)
    {
        const char *body = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app.start_body));
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app.start_base));
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.start_base), "(in orbit)");
        for (const BaseInfo &b : app.bases)
            if (body && b.body == body)
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.start_base), (b.name + " [" + b.id + "]").c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.start_base), 0);
    }

    void on_view_toggle(GtkWidget *w, gpointer)
    {
        app.show_porkchop = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)) && app.viz.has_grid;
        redraw();
    }

    std::string choose_file(const char *title, GtkFileChooserAction action, const char *name)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(title, GTK_WINDOW(app.window), action, "_Cancel", GTK_RESPONSE_CANCEL,
                                                   action == GTK_FILE_CHOOSER_ACTION_SAVE ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        const fs::path dir = fs::path(app.root) / "scenarios";
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), dir.string().c_str());
        if (action == GTK_FILE_CHOOSER_ACTION_SAVE)
        {
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), name);
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        }
        std::string out;
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            out = f ? f : "";
            g_free(f);
        }
        gtk_widget_destroy(d);
        return out;
    }

    void on_save(GtkWidget *, gpointer)
    {
        sync_mission_from_ui();
        app.mission.name = gtk_entry_get_text(GTK_ENTRY(app.name_entry))[0] ? gtk_entry_get_text(GTK_ENTRY(app.name_entry)) : app.mission.name;
        std::string base = app.mission.name;
        std::replace(base.begin(), base.end(), ' ', '_');
        const std::string path = choose_file("Save mission as a scenario", GTK_FILE_CHOOSER_ACTION_SAVE, (base + ".json").c_str());
        if (path.empty())
            return;
        std::string err;
        say(save_mission(path, app.mission, err) ? "Saved " + path : err);
    }

    void on_load(GtkWidget *, gpointer)
    {
        const std::string path = choose_file("Open a mission", GTK_FILE_CHOOSER_ACTION_OPEN, "");
        if (path.empty())
            return;
        Mission m;
        std::string err;
        if (!load_mission(path, m, err))
        {
            say(err);
            return;
        }
        app.mission = m;
        gtk_entry_set_text(GTK_ENTRY(app.name_entry), m.name.c_str());
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.payload), m.payload);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.margin), m.margin * 100);
        for (size_t i = 0; i < app.ships.size(); ++i)
            if (app.ships[i].name == m.ship)
                gtk_combo_box_set_active(GTK_COMBO_BOX(app.ship_combo), static_cast<int>(i));
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", m.start.altitude / 1000);
        gtk_entry_set_text(GTK_ENTRY(app.start_alt), buf);
        app.mission = m;
        app.viz = Viz{};
        app.place = {m.start.body, m.start.altitude, 0};
        if (!m.legs.empty())
            app.place = {m.legs.back().to_body.empty() ? m.legs.back().body : m.legs.back().to_body,
                         m.legs.back().altitude, m.legs.back().depart_days * DAY + m.legs.back().duration};
        say("Loaded " + path);
        refresh_all();
    }

    void on_export(GtkWidget *, gpointer)
    {
        sync_mission_from_ui();
        const std::string path = choose_file("Export the leg list (.txt or .csv)", GTK_FILE_CHOOSER_ACTION_SAVE, "mission.txt");
        if (path.empty())
            return;
        const bool csv = path.size() > 4 && path.substr(path.size() - 4) == ".csv";
        std::ofstream f(path);
        f << (csv ? summary_csv(app.mission, available_dv()) : summary_text(app.mission, available_dv()));
        say(f ? "Exported " + path : "Could not write " + path);
    }

    void on_push(GtkWidget *, gpointer)
    {
        if (app.mission.legs.empty())
        {
            say("There are no legs to push.");
            return;
        }
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app.tree));
        GtkTreeModel *model;
        GtkTreeIter it;
        size_t index = 0;
        if (gtk_tree_selection_get_selected(sel, &model, &it))
        {
            GtkTreePath *p = gtk_tree_model_get_path(model, &it);
            index = static_cast<size_t>(gtk_tree_path_get_indices(p)[0]);
            gtk_tree_path_free(p);
        }
        const Leg &leg = app.mission.legs[std::min(index, app.mission.legs.size() - 1)];
        try
        {
            if (!app.link)
                app.link = std::make_unique<kosmos::Link>("kosmosmission", "KosmosMission", KL_CAP_READ | KL_CAP_WRITE, "0.1");
            // OrbitComp's manoeuvre node: T+ mode, a prograde burn. The game must have the OrbitComp MFD to show it.
            app.link->set("/orbitcomp/page", 1.0);
            app.link->set("/orbitcomp/node-mode", 1.0);
            app.link->set("/orbitcomp/time", 600.0);
            app.link->set("/orbitcomp/prograde", leg.dv);
            app.link->set("/orbitcomp/radial", 0.0);
            app.link->set("/orbitcomp/normal", 0.0);
            char buf[160];
            std::snprintf(buf, sizeof buf, "Sent '%s' (%.0f m/s prograde at T+600 s) to the game's OrbitComp node.", leg.name.c_str(), leg.dv);
            say(buf);
        }
        catch (const std::exception &e)
        {
            app.link.reset();
            say(std::string("Not connected to Kosmos: ") + e.what() + " (start the game and launch this from Add-ons)");
        }
    }

    // ---- GL area ------------------------------------------------------------------------------------------------------------------------------

    void on_realize(GtkWidget *w, gpointer)
    {
        gtk_gl_area_make_current(GTK_GL_AREA(w));
        if (gtk_gl_area_get_error(GTK_GL_AREA(w)))
            return;
        app.gl_ready = app.renderer.init();
    }

    void on_unrealize(GtkWidget *w, gpointer)
    {
        gtk_gl_area_make_current(GTK_GL_AREA(w));
        app.renderer.release();
        app.gl_ready = false;
    }

    gboolean on_render(GtkGLArea *a, GdkGLContext *, gpointer)
    {
        const int scale = gtk_widget_get_scale_factor(GTK_WIDGET(a));
        const int w = gtk_widget_get_allocated_width(GTK_WIDGET(a)) * scale, h = gtk_widget_get_allocated_height(GTK_WIDGET(a)) * scale;
        kmview::Scene s = app.scene;
        if (!app.show_porkchop && h > 0 && w > 0)
        {
            s.scale_x *= std::min(1.0f, static_cast<float>(h) / w);
            s.scale_y *= std::min(1.0f, static_cast<float>(w) / h);
        }
        app.renderer.draw(s, w, h);
        return TRUE;
    }

    gboolean on_scroll(GtkWidget *, GdkEventScroll *e, gpointer)
    {
        app.zoom = clamp(app.zoom * (e->direction == GDK_SCROLL_UP ? 1.15f : 1 / 1.15f), 0.2f, 20.0f);
        redraw();
        return TRUE;
    }

    GtkWidget *labelled(GtkWidget *box, const char *text, GtkWidget *w)
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *l = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_widget_set_size_request(l, 110, -1);
        gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
        return w;
    }

    GtkWidget *entry(const char *text)
    {
        GtkWidget *e = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(e), text);
        gtk_entry_set_width_chars(GTK_ENTRY(e), 8);
        return e;
    }

    GtkWidget *button(const char *text, GCallback cb)
    {
        GtkWidget *b = gtk_button_new_with_label(text);
        g_signal_connect(b, "clicked", cb, nullptr);
        return b;
    }

    bool g_demo = false;
    std::string g_shot;

    // --shot FILE draws the window to a PNG a few seconds after start and quits.
    gboolean take_shot(gpointer)
    {
        const int w = gtk_widget_get_allocated_width(app.window), h = gtk_widget_get_allocated_height(app.window);
        cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(surf);
        gtk_widget_draw(app.window, cr);
        cairo_surface_write_to_png(surf, g_shot.c_str());
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
        g_application_quit(g_application_get_default());
        return G_SOURCE_REMOVE;
    }

    // --demo plans a launch from the first Earth base to Mars, for screenshots and smoke tests.
    void run_demo()
    {
        if (const char *want = std::getenv("KM_SHIP"))
            for (size_t i = 0; i < app.ships.size(); ++i)
                if (app.ships[i].name.find(want) != std::string::npos)
                    gtk_combo_box_set_active(GTK_COMBO_BOX(app.ship_combo), int(i));
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.start_base), 1);
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.kind), 0);
        on_add(nullptr, nullptr);
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.kind), 3);
        int idx = 0;
        for (const Body &b : app.bodies.all())
        {
            if (b.kind == "star")
                continue;
            if (b.name == "Mars")
                gtk_combo_box_set_active(GTK_COMBO_BOX(app.target_combo), idx);
            ++idx;
        }
        gtk_entry_set_text(GTK_ENTRY(app.alt_entry), "300");
        on_add(nullptr, nullptr);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.view_toggle), std::getenv("KM_PORKCHOP") != nullptr);
    }

    void activate(GtkApplication *gapp, gpointer)
    {
        app.root = find_root();
        std::string err;
        if (!app.bodies.load(app.root + "/data/sol.json", err))
            std::fprintf(stderr, "kosmosmission: %s\n", err.c_str());
        app.ships = list_ships(app.root);
        app.table.load(app.root + "/data/mission/ascent.json");
        load_bases();

        app.window = gtk_application_window_new(gapp);
        gtk_window_set_title(GTK_WINDOW(app.window), "KosmosMission");
        gtk_window_set_default_size(GTK_WINDOW(app.window), 1280, 800);
        GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(main_box), 8);
        gtk_container_add(GTK_CONTAINER(app.window), main_box);

        // Left: start, ship and legs.
        GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_size_request(left, 360, -1);
        gtk_box_pack_start(GTK_BOX(main_box), left, FALSE, FALSE, 0);
        app.name_entry = labelled(left, "Mission", entry("New mission"));
        gtk_box_pack_start(GTK_BOX(left), gtk_label_new("Start"), FALSE, FALSE, 0);
        app.start_body = labelled(left, "Body", gtk_combo_box_text_new());
        for (const Body &b : app.bodies.all())
            if (b.kind != "star")
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.start_body), b.name.c_str());
        app.start_base = labelled(left, "Base", gtk_combo_box_text_new());
        app.start_alt = labelled(left, "Orbit alt (km)", entry("200"));
        app.start_inc = labelled(left, "Inclination", entry("0"));
        gtk_box_pack_start(GTK_BOX(left), gtk_label_new("Ship"), FALSE, FALSE, 0);
        app.ship_combo = labelled(left, "Ship", gtk_combo_box_text_new());
        for (const ShipSpec &s : app.ships)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.ship_combo), s.name.c_str());
        app.payload = labelled(left, "Payload (kg)", gtk_spin_button_new_with_range(0, 1e7, 50));
        app.margin = labelled(left, "Reserve (%)", gtk_spin_button_new_with_range(0, 50, 1));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.margin), 5);
        app.budget_label = gtk_label_new("");
        gtk_label_set_line_wrap(GTK_LABEL(app.budget_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(app.budget_label), 0);
        gtk_box_pack_start(GTK_BOX(left), app.budget_label, FALSE, FALSE, 4);

        gtk_box_pack_start(GTK_BOX(left), gtk_label_new("Add a leg"), FALSE, FALSE, 0);
        app.kind = labelled(left, "Kind", gtk_combo_box_text_new());
        for (const char *k : {"Ascent (from the base)", "Hohmann to altitude", "Plane change", "Transfer to body", "Custom burn"})
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.kind), k);
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.kind), 0);
        app.target_combo = labelled(left, "Destination", gtk_combo_box_text_new());
        for (const Body &b : app.bodies.all())
            if (b.kind != "star")
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.target_combo), b.name.c_str());
        app.alt_entry = labelled(left, "Altitude (km)", entry("200"));
        app.angle_entry = labelled(left, "Angle (deg)", entry("0"));
        app.dv_entry = labelled(left, "Burn dV (m/s)", entry("0"));
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_start(GTK_BOX(row), button("Add leg", G_CALLBACK(on_add)), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), button("Remove", G_CALLBACK(on_remove)), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), button("Clear", G_CALLBACK(on_clear)), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(left), row, FALSE, FALSE, 0);
        GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_start(GTK_BOX(row2), button("Save", G_CALLBACK(on_save)), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row2), button("Open", G_CALLBACK(on_load)), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row2), button("Export", G_CALLBACK(on_export)), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row2), button("To game", G_CALLBACK(on_push)), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(left), row2, FALSE, FALSE, 0);
        app.status = gtk_label_new("");
        gtk_label_set_line_wrap(GTK_LABEL(app.status), TRUE);
        gtk_label_set_xalign(GTK_LABEL(app.status), 0);
        gtk_box_pack_start(GTK_BOX(left), app.status, FALSE, FALSE, 4);

        // Right: the view over the legs.
        GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_box_pack_start(GTK_BOX(main_box), right, TRUE, TRUE, 0);
        app.view_toggle = gtk_check_button_new_with_label("Porkchop (departure vs flight time, colour = total dV)");
        gtk_box_pack_start(GTK_BOX(right), app.view_toggle, FALSE, FALSE, 0);
        app.gl = gtk_gl_area_new();
        gtk_gl_area_set_required_version(GTK_GL_AREA(app.gl), 3, 2);
        gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(app.gl), FALSE);
        gtk_widget_add_events(app.gl, GDK_SCROLL_MASK);
        gtk_widget_set_size_request(app.gl, 600, 420);
        g_signal_connect(app.gl, "realize", G_CALLBACK(on_realize), nullptr);
        g_signal_connect(app.gl, "unrealize", G_CALLBACK(on_unrealize), nullptr);
        g_signal_connect(app.gl, "render", G_CALLBACK(on_render), nullptr);
        g_signal_connect(app.gl, "scroll-event", G_CALLBACK(on_scroll), nullptr);
        gtk_box_pack_start(GTK_BOX(right), app.gl, TRUE, TRUE, 0);
        app.info = gtk_label_new("Add a leg to begin.");
        gtk_label_set_xalign(GTK_LABEL(app.info), 0);
        gtk_box_pack_start(GTK_BOX(right), app.info, FALSE, FALSE, 0);

        app.store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
        app.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.store));
        const char *cols[] = {"Leg", "Type", "dV m/s", "Total", "Left"};
        for (int i = 0; i < 5; ++i)
        {
            GtkCellRenderer *r = gtk_cell_renderer_text_new();
            GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(cols[i], r, "text", i, "background", 5, nullptr);
            gtk_tree_view_column_set_expand(c, i == 0);
            gtk_tree_view_append_column(GTK_TREE_VIEW(app.tree), c);
        }
        GtkWidget *sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(sw, -1, 200);
        gtk_container_add(GTK_CONTAINER(sw), app.tree);
        gtk_box_pack_start(GTK_BOX(right), sw, FALSE, FALSE, 0);

        g_signal_connect(app.view_toggle, "toggled", G_CALLBACK(on_view_toggle), nullptr);
        g_signal_connect(app.start_body, "changed", G_CALLBACK(on_body_changed), nullptr);
        g_signal_connect(app.ship_combo, "changed", G_CALLBACK(on_param_changed), nullptr);
        g_signal_connect(app.payload, "value-changed", G_CALLBACK(on_param_changed), nullptr);
        g_signal_connect(app.margin, "value-changed", G_CALLBACK(on_param_changed), nullptr);

        for (size_t i = 0; i < app.bodies.all().size(); ++i)
            if (app.bodies.all()[i].name == "Earth")
            {
                int idx = 0;
                for (const Body &b : app.bodies.all())
                {
                    if (b.kind == "star")
                        continue;
                    if (b.name == "Earth")
                        gtk_combo_box_set_active(GTK_COMBO_BOX(app.start_body), idx);
                    ++idx;
                }
                break;
            }
        if (!app.ships.empty())
            gtk_combo_box_set_active(GTK_COMBO_BOX(app.ship_combo), 0);
        sync_mission_from_ui();
        reset_place();
        refresh_all();
        gtk_widget_show_all(app.window);
        if (g_demo)
            run_demo();
        if (!g_shot.empty())
            g_timeout_add(2500, take_shot, nullptr);
    }
} // namespace

int main(int argc, char **argv)
{
    int kept = 1;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--demo")
            g_demo = true;
        else if (arg == "--shot" && i + 1 < argc)
            g_shot = argv[++i];
        else
            argv[kept++] = argv[i];
    }
    argc = kept;
    GtkApplication *a = gtk_application_new("org.kosmos.mission", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(a, "activate", G_CALLBACK(activate), nullptr);
    const int status = g_application_run(G_APPLICATION(a), argc, argv);
    g_object_unref(a);
    return status;
}
