// KosmosMap: a moving map for Kosmos, in the spirit of FlightGear's Atlas. A plain GTK 3 program that talks to the game
// over KosmosLink and reads the game's own terrain files, so it does not link simlib.
#include "render.h"
#include "../../gtk_schema.h"

#include <kosmos_link.hpp>

#include <gtk/gtk.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using kmap::pi;
    constexpr double rad = pi / 180.0;

    struct Ship
    {
        std::string name, body;
        double lat = 0, lon = 0, alt = 0, heading = 0, speed = 0;
        bool flown = false, landed = false, crashed = false;
    };

    struct Rendered
    {
        cairo_surface_t *surface = nullptr;
        kmap::View view;
    };

    struct App
    {
        std::unique_ptr<kosmos::Link> link;
        std::shared_ptr<kmap::Ground> ground = std::make_shared<kmap::Ground>();
        std::string root;
        kmap::View view;
        Rendered cache;
        std::atomic<int> generation{0};
        guint render_timer = 0;
        GtkWidget *window = nullptr, *area = nullptr, *follow_button = nullptr, *mode_box = nullptr;

        Ship flown;
        bool have_flown = false;
        std::vector<Ship> others;
        std::vector<kl_facility> facilities;
        std::vector<std::pair<double, double>> track;
        double facility_lat = 1e9, facility_lon = 1e9;
        gint64 facility_time = 0;

        bool follow = true, dragging = false, connected = false, busy = false;
        double drag_x = 0, drag_y = 0, cursor_x = -1, cursor_y = -1;
        std::string status;
    };

    constexpr double margin_factor = 1.4;

    double min_ppd(const App &a) { return std::max(0.2, a.view.h / 180.0); }
    double max_ppd(const App &a) { return a.ground->radius * rad / 2.0; } // two metres a pixel

    std::string hemisphere(double v, char positive, char negative, int width)
    {
        char text[48];
        std::snprintf(text, sizeof text, "%c %0*.4f\u00b0", v >= 0 ? positive : negative, width, std::fabs(v));
        return text;
    }

    // Renders the cached picture (a bit larger than the window, so small pans need no redraw).
    struct Job
    {
        App *app;
        std::shared_ptr<kmap::Ground> ground;
        kmap::View view;
        int generation;
        std::vector<std::uint32_t> pixels;
    };

    gboolean finish_job(gpointer data)
    {
        std::unique_ptr<Job> job(static_cast<Job *>(data));
        App &a = *job->app;
        if (job->generation != a.generation.load() || job->pixels.empty())
            return G_SOURCE_REMOVE;
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, job->view.w, job->view.h);
        const int stride = cairo_image_surface_get_stride(s);
        unsigned char *dst = cairo_image_surface_get_data(s);
        cairo_surface_flush(s);
        for (int y = 0; y < job->view.h; ++y)
            std::memcpy(dst + static_cast<std::size_t>(y) * stride, job->pixels.data() + static_cast<std::size_t>(y) * job->view.w, static_cast<std::size_t>(job->view.w) * 4);
        cairo_surface_mark_dirty(s);
        if (a.cache.surface)
            cairo_surface_destroy(a.cache.surface);
        a.cache = {s, job->view};
        a.busy = false;
        if (a.area)
            gtk_widget_queue_draw(a.area);
        return G_SOURCE_REMOVE;
    }

    kmap::View render_view(const App &a)
    {
        kmap::View v = a.view;
        v.w = static_cast<int>(a.view.w * margin_factor);
        v.h = static_cast<int>(a.view.h * margin_factor);
        return v;
    }

    void start_render(App &a)
    {
        a.render_timer = 0;
        if (a.view.w < 16 || a.view.h < 16)
            return;
        const int gen = ++a.generation;
        a.busy = true;
        Job *job = new Job{&a, a.ground, render_view(a), gen, {}};
        std::thread([job] {
            job->pixels = kmap::render(*job->ground, job->view, job->app->generation, job->generation);
            g_idle_add(finish_job, job);
        }).detach();
    }

    gboolean render_now(gpointer data)
    {
        start_render(*static_cast<App *>(data));
        return G_SOURCE_REMOVE;
    }

    void request_render(App &a, unsigned delay_ms = 90)
    {
        if (a.render_timer)
            g_source_remove(a.render_timer);
        a.render_timer = g_timeout_add(delay_ms, render_now, &a);
    }

    // True when the cached picture no longer covers the window or is at the wrong scale.
    bool cache_stale(const App &a)
    {
        if (!a.cache.surface)
            return true;
        const kmap::View &c = a.cache.view;
        const double ratio = a.view.ppd / c.ppd;
        if (ratio < 0.7 || ratio > 1.4 || a.view.imagery != c.imagery)
            return true;
        for (const double cx : {0.0, static_cast<double>(a.view.w)})
            for (const double cy : {0.0, static_cast<double>(a.view.h)})
            {
                double lat, lon, px, py;
                a.view.to_geo(cx, cy, lat, lon);
                c.to_pixel(lat, lon, px, py);
                if (px < 0 || py < 0 || px > c.w || py > c.h)
                    return true;
            }
        return false;
    }

    void set_ground(App &a, const std::string &body, double radius)
    {
        auto g = std::make_shared<kmap::Ground>();
        g->load(a.root, body, radius);
        a.ground = g;
        a.track.clear();
        a.facilities.clear();
        a.facility_lat = a.facility_lon = 1e9;
        a.view.imagery = a.view.imagery && g->picture.ok();
        a.view.ppd = std::clamp(a.view.ppd, min_ppd(a), max_ppd(a));
        if (a.cache.surface)
        {
            cairo_surface_destroy(a.cache.surface);
            a.cache = {};
        }
        if (a.mode_box)
        {
            gtk_widget_set_sensitive(a.mode_box, g->picture.ok());
            gtk_combo_box_set_active(GTK_COMBO_BOX(a.mode_box), a.view.imagery ? 1 : 0);
        }
        a.status = g->source;
    }

    double body_radius(App &a, const std::string &name)
    {
        const int count = static_cast<int>(a.link->get("/universe/bodies/count", 0.0));
        for (int i = 0; i < count; ++i)
        {
            const std::string base = "/universe/bodies/body[" + std::to_string(i) + "]/";
            if (a.link->get_string(base + "name") == name)
                return a.link->get(base + "radius", 0.0);
        }
        return 0.0;
    }

    // ---- Drawing

    void text_at(cairo_t *cr, double x, double y, const std::string &text, double size, double r, double g, double b, double alpha = 1.0, bool shadow = true)
    {
        cairo_set_font_size(cr, size);
        if (shadow)
        {
            cairo_set_source_rgba(cr, 0, 0, 0, 0.75 * alpha);
            for (const double d : {-1.0, 1.0})
            {
                cairo_move_to(cr, x + d, y);
                cairo_show_text(cr, text.c_str());
                cairo_move_to(cr, x, y + d);
                cairo_show_text(cr, text.c_str());
            }
        }
        cairo_set_source_rgba(cr, r, g, b, alpha);
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, text.c_str());
    }

    void draw_graticule(cairo_t *cr, const App &a)
    {
        static const double steps[] = {0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 45, 90};
        const kmap::View &v = a.view;
        double step = 90;
        for (const double s : steps)
            if (s * v.ppd * v.kx() >= 90.0)
            {
                step = s;
                break;
            }
        double lat_top, lat_bottom, lon_left, lon_right;
        v.to_geo(0, 0, lat_top, lon_left);
        v.to_geo(v.w, v.h, lat_bottom, lon_right);
        cairo_set_line_width(cr, 1.0);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        const int decimals = step >= 1 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3;
        char fmt[16];
        std::snprintf(fmt, sizeof fmt, "%%.%df\u00b0", decimals);
        for (double lat = std::ceil(lat_bottom / step) * step; lat <= lat_top; lat += step)
        {
            double px, py;
            v.to_pixel(lat, v.lon, px, py);
            cairo_set_source_rgba(cr, 1, 1, 1, std::fabs(lat) < 1e-9 ? 0.45 : 0.22);
            cairo_move_to(cr, 0, std::round(py) + 0.5);
            cairo_line_to(cr, v.w, std::round(py) + 0.5);
            cairo_stroke(cr);
            char label[32];
            std::snprintf(label, sizeof label, fmt, std::fabs(lat));
            text_at(cr, 4, py - 3, std::string(label) + (lat < 0 ? "S" : "N"), 10, 1, 1, 1, 0.8);
        }
        const double span = (v.w / (v.ppd * v.kx()));
        const double first = std::floor((v.lon - span / 2) / step) * step;
        for (double lon = first; lon <= v.lon + span / 2 + step; lon += step)
        {
            double px, py;
            v.to_pixel(v.lat, lon, px, py);
            if (px < -2 || px > v.w + 2)
                continue;
            double wrapped = std::fmod(lon + 180.0, 360.0);
            if (wrapped < 0)
                wrapped += 360.0;
            wrapped -= 180.0;
            cairo_set_source_rgba(cr, 1, 1, 1, std::fabs(wrapped) < 1e-9 ? 0.45 : 0.22);
            cairo_move_to(cr, std::round(px) + 0.5, 0);
            cairo_line_to(cr, std::round(px) + 0.5, v.h);
            cairo_stroke(cr);
            char label[32];
            std::snprintf(label, sizeof label, fmt, std::fabs(wrapped));
            text_at(cr, px + 3, v.h - 6, std::string(label) + (wrapped < 0 ? "W" : "E"), 10, 1, 1, 1, 0.8);
        }
    }

    void draw_airplane(cairo_t *cr, double x, double y, double heading_deg, double size, double r, double g, double b)
    {
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_rotate(cr, heading_deg * rad);
        cairo_scale(cr, size / 16.0, size / 16.0);
        cairo_move_to(cr, 0, -14);
        cairo_curve_to(cr, 2, -12, 2, -6, 2, -4);
        cairo_line_to(cr, 13, 2);
        cairo_line_to(cr, 13, 5);
        cairo_line_to(cr, 2, 2);
        cairo_line_to(cr, 1.5, 9);
        cairo_line_to(cr, 5, 12);
        cairo_line_to(cr, 5, 14);
        cairo_line_to(cr, 0, 12.5);
        cairo_line_to(cr, -5, 14);
        cairo_line_to(cr, -5, 12);
        cairo_line_to(cr, -1.5, 9);
        cairo_line_to(cr, -2, 2);
        cairo_line_to(cr, -13, 5);
        cairo_line_to(cr, -13, 2);
        cairo_line_to(cr, -2, -4);
        cairo_curve_to(cr, -2, -6, -2, -12, 0, -14);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, r, g, b, 1.0);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.9);
        cairo_set_line_width(cr, 1.4);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    void draw_facilities(cairo_t *cr, const App &a)
    {
        const kmap::View &v = a.view;
        const double mpp = v.metres_per_pixel(a.ground->radius);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        for (const kl_facility &f : a.facilities)
        {
            double px, py;
            v.to_pixel(f.latitude, f.longitude, px, py);
            if (px < -60 || py < -60 || px > v.w + 60 || py > v.h + 60)
                continue;
            for (int i = 0; i < f.runway_count; ++i)
            {
                const kl_runway &r = f.runways[i];
                double rx, ry;
                v.to_pixel(r.latitude, r.longitude, rx, ry);
                const double length = r.length / mpp, width = std::max(2.5, r.width / mpp);
                if (length < 3)
                    continue;
                cairo_save(cr);
                cairo_translate(cr, rx, ry);
                cairo_rotate(cr, r.heading * rad);
                cairo_rectangle(cr, -width / 2, -length / 2, width, length);
                cairo_set_source_rgba(cr, 0.12, 0.12, 0.14, 0.95);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);
                cairo_restore(cr);
            }
            const bool airport = f.runway_count > 0;
            const double dot = airport ? 4.5 : 3.5;
            cairo_new_path(cr);
            cairo_arc(cr, px, py, dot, 0, 2 * pi);
            cairo_set_source_rgba(cr, airport ? 0.25 : 0.95, airport ? 0.62 : 0.8, airport ? 1.0 : 0.3, 1.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
            if (v.ppd > 8.0)
                text_at(cr, px + 8, py + 4, f.icao[0] ? f.icao : f.name, 11, 0.85, 0.95, 1.0);
            if (v.ppd > 400.0)
            {
                text_at(cr, px + 8, py + 17, f.name, 10, 1, 1, 1, 0.9);
                for (int n = 0; n < f.navaid_count; ++n)
                {
                    const kl_navaid &nav = f.navaids[n];
                    double nx, ny;
                    v.to_pixel(nav.latitude, nav.longitude, nx, ny);
                    cairo_rectangle(cr, nx - 3, ny - 3, 6, 6);
                    cairo_set_source_rgba(cr, 1.0, 0.5, 0.9, 0.95);
                    cairo_fill(cr);
                    text_at(cr, nx + 6, ny + 3, nav.id, 9, 1.0, 0.75, 0.95);
                }
            }
        }
    }

    void draw_scale_bar(cairo_t *cr, const App &a)
    {
        const double mpp = a.view.metres_per_pixel(a.ground->radius);
        const double target = 140.0 * mpp;
        const double magnitude = std::pow(10.0, std::floor(std::log10(target)));
        double length = magnitude;
        for (const double m : {1.0, 2.0, 5.0, 10.0})
            if (m * magnitude <= target)
                length = m * magnitude;
        const double pixels = length / mpp;
        const double x = 16, y = a.view.h - 34;
        cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
        cairo_rectangle(cr, x - 6, y - 16, pixels + 12, 34);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, x, y - 4);
        cairo_line_to(cr, x, y + 4);
        cairo_line_to(cr, x + pixels, y + 4);
        cairo_line_to(cr, x + pixels, y - 4);
        cairo_stroke(cr);
        char label[32];
        if (length >= 1000.0)
            std::snprintf(label, sizeof label, "%g km", length / 1000.0);
        else
            std::snprintf(label, sizeof label, "%g m", length);
        text_at(cr, x + 4, y - 7, label, 11, 1, 1, 1, 1.0, false);
    }

    void draw_info(cairo_t *cr, const App &a)
    {
        std::vector<std::string> lines;
        lines.push_back(a.ground->name + (a.view.imagery ? "  (imagery)" : "  (relief)"));
        if (a.have_flown)
        {
            const Ship &s = a.flown;
            lines.push_back(s.name);
            lines.push_back(hemisphere(s.lat, 'N', 'S', 7) + "  " + hemisphere(s.lon, 'E', 'W', 8));
            char row[96];
            std::snprintf(row, sizeof row, "Alt %.0f m   Hdg %03.0f\u00b0   %.0f kt", s.alt, std::fmod(s.heading + 360.0, 360.0), s.speed * 1.943844);
            lines.push_back(row);
        }
        else
            lines.push_back(a.connected ? "Waiting for a ship" : "Not connected to Kosmos: browsing only");
        if (a.cursor_x >= 0)
        {
            double lat, lon;
            a.view.to_geo(a.cursor_x, a.cursor_y, lat, lon);
            std::string row = hemisphere(lat, 'N', 'S', 7) + "  " + hemisphere(lon, 'E', 'W', 8);
            const double h = kmap::height_at(*a.ground, a.view, lat, lon);
            if (!std::isnan(h))
            {
                char buf[32];
                std::snprintf(buf, sizeof buf, "   %.0f m", h);
                row += buf;
            }
            lines.push_back("Cursor  " + row);
        }
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);
        double width = 0;
        for (const std::string &l : lines)
        {
            cairo_text_extents_t e;
            cairo_text_extents(cr, l.c_str(), &e);
            width = std::max(width, e.x_advance);
        }
        cairo_set_source_rgba(cr, 0.04, 0.05, 0.08, 0.72);
        cairo_rectangle(cr, 8, 8, width + 20, lines.size() * 17 + 10);
        cairo_fill(cr);
        double y = 27;
        for (std::size_t i = 0; i < lines.size(); ++i, y += 17)
            text_at(cr, 18, y, lines[i], 12, i == 0 ? 1.0 : 0.88, i == 0 ? 0.85 : 0.92, i == 0 ? 0.45 : 1.0, 1.0, false);
        if (a.busy)
            text_at(cr, 18, y + 4, "Drawing...", 11, 1, 0.8, 0.4, 1.0, false);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_text_extents_t e;
        cairo_set_font_size(cr, 10);
        cairo_text_extents(cr, a.status.c_str(), &e);
        text_at(cr, a.view.w - e.x_advance - 10, a.view.h - 10, a.status, 10, 1, 1, 1, 0.75);
    }

    void paint(cairo_t *cr, const App &a)
    {
        const kmap::View &v = a.view;
        cairo_set_source_rgb(cr, 0.06, 0.07, 0.1);
        cairo_paint(cr);
        if (a.cache.surface)
        {
            const kmap::View &c = a.cache.view;
            double cx, cy;
            v.to_pixel(c.lat, c.lon, cx, cy);
            cairo_save(cr);
            cairo_translate(cr, cx, cy);
            cairo_scale(cr, v.ppd * v.kx() / (c.ppd * c.kx()), v.ppd / c.ppd);
            cairo_set_source_surface(cr, a.cache.surface, -c.w * 0.5, -c.h * 0.5);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        draw_graticule(cr, a);
        draw_facilities(cr, a);

        if (a.track.size() > 1)
        {
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_set_line_width(cr, 2.0);
            cairo_set_source_rgba(cr, 1.0, 0.35, 0.2, 0.9);
            bool pen_down = false;
            double last_x = 0;
            for (const auto &p : a.track)
            {
                double px, py;
                v.to_pixel(p.first, p.second, px, py);
                if (pen_down && std::fabs(px - last_x) > v.w)
                    pen_down = false; // across the date line
                if (pen_down)
                    cairo_line_to(cr, px, py);
                else
                    cairo_move_to(cr, px, py);
                pen_down = true;
                last_x = px;
            }
            cairo_stroke(cr);
        }
        for (const Ship &s : a.others)
        {
            double px, py;
            v.to_pixel(s.lat, s.lon, px, py);
            if (px < -20 || py < -20 || px > v.w + 20 || py > v.h + 20)
                continue;
            draw_airplane(cr, px, py, s.heading, 11, 0.4, 0.9, 1.0);
            if (v.ppd > 50.0)
                text_at(cr, px + 10, py + 4, s.name, 10, 0.7, 0.95, 1.0);
        }
        if (a.have_flown)
        {
            double px, py;
            v.to_pixel(a.flown.lat, a.flown.lon, px, py);
            draw_airplane(cr, px, py, a.flown.heading, 22, 1.0, 0.85, 0.15);
        }
        draw_scale_bar(cr, a);
        draw_info(cr, a);
    }

    gboolean on_draw(GtkWidget *, cairo_t *cr, gpointer data)
    {
        paint(cr, *static_cast<App *>(data));
        return FALSE;
    }

    // ---- Interaction

    void set_follow(App &a, bool on, bool update_button = true)
    {
        a.follow = on;
        if (update_button && a.follow_button)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a.follow_button), on);
    }

    void zoom_to(App &a, double ppd, double px, double py)
    {
        ppd = std::clamp(ppd, min_ppd(a), max_ppd(a));
        double lat, lon;
        a.view.to_geo(px, py, lat, lon);
        a.view.ppd = ppd;
        for (int i = 0; i < 3; ++i)
        {
            a.view.lat = std::clamp(lat + (py - a.view.h * 0.5) / ppd, -90.0, 90.0);
            a.view.lon = lon - (px - a.view.w * 0.5) / (ppd * a.view.kx());
        }
        if (a.view.lon > 180.0)
            a.view.lon -= 360.0;
        if (a.view.lon < -180.0)
            a.view.lon += 360.0;
        gtk_widget_queue_draw(a.area);
        request_render(a, 140);
    }

    gboolean on_scroll(GtkWidget *, GdkEventScroll *e, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        const double factor = e->direction == GDK_SCROLL_UP ? 1.25 : e->direction == GDK_SCROLL_DOWN ? 0.8 : 1.0;
        if (a.follow && a.have_flown)
            zoom_to(a, a.view.ppd * factor, a.view.w * 0.5, a.view.h * 0.5);
        else
            zoom_to(a, a.view.ppd * factor, e->x, e->y);
        return TRUE;
    }

    gboolean on_press(GtkWidget *, GdkEventButton *e, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        if (e->button == 1)
        {
            a.dragging = true;
            a.drag_x = e->x;
            a.drag_y = e->y;
        }
        return TRUE;
    }

    gboolean on_release(GtkWidget *, GdkEventButton *, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        a.dragging = false;
        if (cache_stale(a))
            request_render(a, 60);
        return TRUE;
    }

    gboolean on_motion(GtkWidget *, GdkEventMotion *e, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        a.cursor_x = e->x;
        a.cursor_y = e->y;
        if (a.dragging)
        {
            const double dx = e->x - a.drag_x, dy = e->y - a.drag_y;
            a.drag_x = e->x;
            a.drag_y = e->y;
            set_follow(a, false);
            a.view.lat = std::clamp(a.view.lat + dy / a.view.ppd, -90.0, 90.0);
            a.view.lon -= dx / (a.view.ppd * a.view.kx());
            if (a.view.lon > 180.0)
                a.view.lon -= 360.0;
            if (a.view.lon < -180.0)
                a.view.lon += 360.0;
            if (cache_stale(a))
                request_render(a, 60);
        }
        gtk_widget_queue_draw(a.area);
        return TRUE;
    }

    gboolean on_leave(GtkWidget *, GdkEventCrossing *, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        a.cursor_x = -1;
        gtk_widget_queue_draw(a.area);
        return FALSE;
    }

    gboolean on_configure(GtkWidget *, GdkEventConfigure *e, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        a.view.w = e->width;
        a.view.h = e->height;
        a.view.ppd = std::clamp(a.view.ppd, min_ppd(a), max_ppd(a));
        request_render(a, 150);
        return FALSE;
    }

    void centre_on_ship(App &a)
    {
        if (!a.have_flown)
            return;
        a.view.lat = a.flown.lat;
        a.view.lon = a.flown.lon;
    }

    gboolean on_key(GtkWidget *, GdkEventKey *e, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        switch (e->keyval)
        {
        case GDK_KEY_plus:
        case GDK_KEY_equal:
        case GDK_KEY_KP_Add:
            zoom_to(a, a.view.ppd * 1.5, a.view.w * 0.5, a.view.h * 0.5);
            return TRUE;
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
            zoom_to(a, a.view.ppd / 1.5, a.view.w * 0.5, a.view.h * 0.5);
            return TRUE;
        case GDK_KEY_f:
        case GDK_KEY_F:
            set_follow(a, !a.follow);
            return TRUE;
        case GDK_KEY_c:
        case GDK_KEY_C:
            a.track.clear();
            gtk_widget_queue_draw(a.area);
            return TRUE;
        }
        return FALSE;
    }

    void on_follow(GtkToggleButton *button, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        a.follow = gtk_toggle_button_get_active(button);
        if (a.follow)
        {
            centre_on_ship(a);
            if (cache_stale(a))
                request_render(a, 30);
            gtk_widget_queue_draw(a.area);
        }
    }

    void on_mode(GtkComboBox *box, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        a.view.imagery = gtk_combo_box_get_active(box) == 1 && a.ground->picture.ok();
        request_render(a, 10);
    }

    void on_zoom_in(GtkButton *, gpointer data) { auto &a = *static_cast<App *>(data); zoom_to(a, a.view.ppd * 1.5, a.view.w * 0.5, a.view.h * 0.5); }
    void on_zoom_out(GtkButton *, gpointer data) { auto &a = *static_cast<App *>(data); zoom_to(a, a.view.ppd / 1.5, a.view.w * 0.5, a.view.h * 0.5); }
    void on_clear(GtkButton *, gpointer data) { auto &a = *static_cast<App *>(data); a.track.clear(); gtk_widget_queue_draw(a.area); }

    // ---- The link to Kosmos

    void update_facilities(App &a)
    {
        const gint64 now = g_get_monotonic_time();
        if (!a.connected || now - a.facility_time < 2000000)
            return;
        const double moved = std::fabs(a.view.lat - a.facility_lat) + std::fabs(a.view.lon - a.facility_lon);
        if (moved < 0.05 / std::max(1.0, a.view.ppd / 100.0) && !a.facilities.empty())
            return;
        a.facility_time = now;
        a.facility_lat = a.view.lat;
        a.facility_lon = a.view.lon;
        a.facilities = a.link->facilities(a.view.lat, a.view.lon, 0.0, "", 200, "");
    }

    gboolean on_tick(gpointer data)
    {
        App &a = *static_cast<App *>(data);
        if (!a.link)
            return G_SOURCE_CONTINUE;
        if (!a.link->poll(0))
        {
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }
        std::vector<kl_ship_info> ships = a.link->ships(0.0, false);
        a.others.clear();
        a.have_flown = false;
        for (const kl_ship_info &info : ships)
        {
            Ship s;
            s.name = info.name;
            s.body = info.body;
            s.lat = info.latitude;
            s.lon = info.longitude;
            s.alt = info.altitude;
            s.heading = info.heading;
            s.speed = info.speed;
            s.flown = info.flags & KL_SHIP_FLOWN;
            s.landed = info.flags & KL_SHIP_LANDED;
            s.crashed = info.flags & KL_SHIP_CRASHED;
            if (s.flown)
            {
                a.flown = s;
                a.have_flown = true;
            }
            else if (s.body == a.flown.body || !a.have_flown)
                a.others.push_back(s);
        }
        a.others.erase(std::remove_if(a.others.begin(), a.others.end(), [&](const Ship &s) { return a.have_flown && s.body != a.flown.body; }), a.others.end());
        if (a.have_flown && a.flown.body != a.ground->name)
        {
            set_ground(a, a.flown.body, body_radius(a, a.flown.body));
            centre_on_ship(a);
            a.view.ppd = std::clamp(std::max(a.view.ppd, 40.0), min_ppd(a), max_ppd(a));
            set_follow(a, true);
            request_render(a, 10);
        }
        if (a.have_flown)
        {
            if (!a.track.empty() && std::fabs(a.track.back().first - a.flown.lat) + std::fabs(a.track.back().second - a.flown.lon) > 5.0)
                a.track.clear();
            if (a.track.empty() || std::fabs(a.track.back().first - a.flown.lat) + std::fabs(a.track.back().second - a.flown.lon) > 0.0003 / std::max(1.0, a.view.ppd / 50.0))
            {
                a.track.emplace_back(a.flown.lat, a.flown.lon);
                if (a.track.size() > 6000)
                    a.track.erase(a.track.begin(), a.track.begin() + 1000);
            }
            if (a.follow)
            {
                centre_on_ship(a);
                if (cache_stale(a) && !a.render_timer)
                    request_render(a, 40);
            }
        }
        update_facilities(a);
        gtk_widget_queue_draw(a.area);
        return G_SOURCE_CONTINUE;
    }

    bool connect_link(App &a)
    {
        try
        {
            a.link = std::make_unique<kosmos::Link>("kosmosmap", "KosmosMap", KL_CAP_READ, "0.1");
            a.connected = true;
            return true;
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "kosmosmap: not connected (%s); browsing only\n", e.what());
            return false;
        }
    }

    // ---- Start up

    GtkWidget *tool_button(const char *label, GCallback cb, App *a, const char *tip)
    {
        GtkWidget *b = gtk_button_new_with_label(label);
        gtk_widget_set_tooltip_text(b, tip);
        g_signal_connect(b, "clicked", cb, a);
        return b;
    }

    void build_window(App &a, int width, int height)
    {
        a.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_default_size(GTK_WINDOW(a.window), width, height);
        GtkWidget *header = gtk_header_bar_new();
        gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
        gtk_header_bar_set_title(GTK_HEADER_BAR(header), "KosmosMap");
        gtk_window_set_titlebar(GTK_WINDOW(a.window), header);

        a.follow_button = gtk_toggle_button_new_with_label("Follow");
        gtk_widget_set_tooltip_text(a.follow_button, "Keep the map centred on the flown ship (F)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a.follow_button), a.follow);
        g_signal_connect(a.follow_button, "toggled", G_CALLBACK(on_follow), &a);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), a.follow_button);

        a.mode_box = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a.mode_box), "Relief");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a.mode_box), "Imagery");
        gtk_combo_box_set_active(GTK_COMBO_BOX(a.mode_box), a.view.imagery ? 1 : 0);
        gtk_widget_set_tooltip_text(a.mode_box, "Shaded height map, or the body's surface picture with relief shading");
        g_signal_connect(a.mode_box, "changed", G_CALLBACK(on_mode), &a);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), a.mode_box);
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), tool_button("Clear track", G_CALLBACK(on_clear), &a, "Forget the path flown so far (C)"));
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), tool_button("\u2212", G_CALLBACK(on_zoom_out), &a, "Zoom out (-)"));
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), tool_button("+", G_CALLBACK(on_zoom_in), &a, "Zoom in (+)"));

        a.area = gtk_drawing_area_new();
        gtk_widget_add_events(a.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_KEY_PRESS_MASK);
        gtk_widget_set_can_focus(a.area, TRUE);
        g_signal_connect(a.area, "draw", G_CALLBACK(on_draw), &a);
        g_signal_connect(a.area, "scroll-event", G_CALLBACK(on_scroll), &a);
        g_signal_connect(a.area, "button-press-event", G_CALLBACK(on_press), &a);
        g_signal_connect(a.area, "button-release-event", G_CALLBACK(on_release), &a);
        g_signal_connect(a.area, "motion-notify-event", G_CALLBACK(on_motion), &a);
        g_signal_connect(a.area, "leave-notify-event", G_CALLBACK(on_leave), &a);
        g_signal_connect(a.area, "configure-event", G_CALLBACK(on_configure), &a);
        g_signal_connect(a.window, "key-press-event", G_CALLBACK(on_key), &a);
        g_signal_connect(a.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
        gtk_container_add(GTK_CONTAINER(a.window), a.area);
        gtk_widget_show_all(a.window);
        gtk_widget_grab_focus(a.area);
    }

    // Draws one frame to a PNG without a window, for testing.
    int write_shot(App &a, const std::string &file, int width, int height)
    {
        a.view.w = width;
        a.view.h = height;
        a.view.ppd = std::clamp(a.view.ppd, min_ppd(a), max_ppd(a));
        const int gen = ++a.generation;
        Job job{&a, a.ground, render_view(a), gen, {}};
        job.pixels = kmap::render(*job.ground, job.view, a.generation, gen);
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, job.view.w, job.view.h);
        const int stride = cairo_image_surface_get_stride(s);
        cairo_surface_flush(s);
        for (int y = 0; y < job.view.h; ++y)
            std::memcpy(cairo_image_surface_get_data(s) + static_cast<std::size_t>(y) * stride, job.pixels.data() + static_cast<std::size_t>(y) * job.view.w, static_cast<std::size_t>(job.view.w) * 4);
        cairo_surface_mark_dirty(s);
        a.cache = {s, job.view};
        cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
        cairo_t *cr = cairo_create(out);
        paint(cr, a);
        cairo_destroy(cr);
        const bool ok = cairo_surface_write_to_png(out, file.c_str()) == CAIRO_STATUS_SUCCESS;
        cairo_surface_destroy(out);
        std::printf("%s %s (%s)\n", ok ? "wrote" : "could not write", file.c_str(), a.ground->source.c_str());
        return ok ? 0 : 1;
    }
}

int main(int argc, char **argv)
{
    App app;
    std::string body = "", shot, root_hint;
    bool want_link = true, imagery = false;
    int width = 1100, height = 720;
    double lat = 0, lon = 0;
    bool have_position = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--body")
            body = next();
        else if (arg == "--lat")
            lat = std::atof(next()), have_position = true;
        else if (arg == "--lon")
            lon = std::atof(next()), have_position = true;
        else if (arg == "--ppd")
            app.view.ppd = std::atof(next());
        else if (arg == "--root")
            root_hint = next();
        else if (arg == "--shot")
            shot = next();
        else if (arg == "--size" && std::sscanf(next(), "%dx%d", &width, &height) != 2)
            width = 1100, height = 720;
        else if (arg == "--imagery")
            imagery = true;
        else if (arg == "--no-link")
            want_link = false;
        else if (arg == "--help" || arg == "-h")
        {
            std::puts("KosmosMap: a moving map for Kosmos.\n"
                      "  --body NAME --lat DEG --lon DEG --ppd N   browse without a game (pixels per degree)\n"
                      "  --imagery        start with the surface picture\n"
                      "  --root DIR       the Kosmos folder (default: found from the program or KOSMOS_DATA)\n"
                      "  --shot FILE.png [--size WxH]   write one picture and exit\n"
                      "  --no-link        do not connect to Kosmos\n"
                      "Keys: F follow, + and - zoom, C clear the track. Drag to pan, wheel to zoom.");
            return 0;
        }
    }
    app.root = kmap::find_root(root_hint, argv[0]);
    if (app.root.empty())
    {
        std::fprintf(stderr, "kosmosmap: cannot find the Kosmos data folder; use --root DIR\n");
        return 1;
    }
    app.view.imagery = imagery;
    if (want_link && shot.empty())
        connect_link(app);
    if (!app.connected && body.empty())
        body = "Earth";
    if (!body.empty())
    {
        set_ground(app, body, 0.0);
        app.view.lat = lat;
        app.view.lon = lon;
        if (!have_position && !app.connected)
            app.view.lat = 30.0;
        if (!app.view.ppd || app.view.ppd == 4.0)
            app.view.ppd = app.connected ? 4.0 : 3.0;
        app.follow = app.connected && !have_position;
    }
    app.view.imagery = imagery && app.ground->picture.ok();
    if (!shot.empty())
    {
        if (have_position)
            app.follow = false;
        return write_shot(app, shot, width, height);
    }

    kosmos_gtk_schemas("kosmosmap");
    gtk_init(&argc, &argv);
    build_window(app, width, height);
    g_timeout_add(200, on_tick, &app);
    if (app.connected && body.empty())
        app.status = "Waiting for the flown ship";
    gtk_main();
    if (app.cache.surface)
        cairo_surface_destroy(app.cache.surface);
    return 0;
}
