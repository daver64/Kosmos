#include "gui.h"

#ifdef PLANETBAKE_GTK

#include "../../gtk_schema.h"
#include <gtk/gtk.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "edit.h"
#include "fill.h"
#include "pack.h"
#include "settings.h"

namespace
{
    using namespace planetbake;

    constexpr int preview_w = 1024, preview_h = 512;

    struct Ui
    {
        Job job;
        int layer = layer_colour;

        GtkWidget *window, *area, *status, *progress, *estimate_label, *export_button, *cancel_button;
        GtkWidget *type_combo, *seed, *ocean, *ice, *relief, *tint, *bands, *contrast, *radius;
        GtkWidget *layer_radio[layer_count], *export_layer[layer_count];
        GtkWidget *width_combo, *format_combo, *dds_combo, *tiles_check, *folder, *name;

        cairo_surface_t *surface[layer_count] = {};

        Pack pack;
        bool pack_open = false, has_sel = false;
        TileRef sel;
        GtkWidget *pack_label, *level_scale, *editor_entry, *edit_button, *import_button, *restore_button;
        GtkWidget *lock_tile_button, *lock_layer_button, *lock_pack_button, *fill_button;
        std::string start_pack;
        guint watch = 0;
        std::string watch_file;
        time_t watch_mtime = 0, watch_imported_mtime = 0;
        guint debounce = 0;
        bool loading = false;

        std::mutex mutex;
        std::condition_variable wake;
        kosmos::PlanetStyle wanted;
        double wanted_radius = 6.371e6;
        float wanted_strength = 1.0f;
        std::atomic<unsigned> generation{0};
        bool has_request = false, quit = false;
        std::thread worker;

        std::atomic<bool> exporting{false}, cancel{false};
        std::thread exporter;
    };

    Ui *ui = nullptr;

    struct PreviewResult
    {
        unsigned generation;
        bool ok;
        std::string error;
        std::unique_ptr<Rendered> rendered;
        double lowest = 0, highest = 0;
        bool applicable[layer_count] = {};
    };

    cairo_surface_t *make_surface(int layer, const Rendered &r)
    {
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, r.w, r.h);
        unsigned char *dst = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        cairo_surface_flush(s);
        for (int y = 0; y < r.h; ++y)
        {
            unsigned char *d = dst + y * stride;
            for (int x = 0; x < r.w; ++x)
            {
                unsigned char red, green, blue;
                if (layer == layer_height)
                    red = green = blue = static_cast<unsigned char>(r.height[static_cast<std::size_t>(y) * r.w + x] >> 8);
                else
                {
                    const unsigned char *p = r.layer[layer].data() + (static_cast<std::size_t>(y) * r.w + x) * 4;
                    red = p[0];
                    green = layer == layer_specular ? p[0] : p[1];
                    blue = layer == layer_specular ? p[0] : p[2];
                    if (layer == layer_clouds)
                    {
                        // Cloud shown over a dark sea so the cover reads clearly.
                        const int a = p[3];
                        red = static_cast<unsigned char>((10 * (255 - a) + p[0] * a) / 255);
                        green = static_cast<unsigned char>((24 * (255 - a) + p[1] * a) / 255);
                        blue = static_cast<unsigned char>((48 * (255 - a) + p[2] * a) / 255);
                    }
                }
                d[x * 4 + 0] = blue;
                d[x * 4 + 1] = green;
                d[x * 4 + 2] = red;
                d[x * 4 + 3] = 255;
            }
        }
        cairo_surface_mark_dirty(s);
        return s;
    }

    gboolean show_preview(gpointer data)
    {
        std::unique_ptr<PreviewResult> result(static_cast<PreviewResult *>(data));
        if (result->generation != ui->generation.load() || ui->pack_open)
            return G_SOURCE_REMOVE; // out of date; a newer one is on its way
        if (!result->ok)
        {
            gtk_label_set_text(GTK_LABEL(ui->status), ("Preview failed: " + result->error).c_str());
            return G_SOURCE_REMOVE;
        }
        for (int i = 0; i < layer_count; ++i)
        {
            if (ui->surface[i])
                cairo_surface_destroy(ui->surface[i]);
            ui->surface[i] = result->applicable[i] ? make_surface(i, *result->rendered) : nullptr;
            gtk_widget_set_sensitive(ui->layer_radio[i], result->applicable[i]);
        }
        if (!result->applicable[ui->layer])
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->layer_radio[layer_colour]), TRUE);
        char text[160];
        if (result->highest > result->lowest)
            std::snprintf(text, sizeof(text), "Preview %dx%d. Heights %.0f m to %.0f m.", preview_w, preview_h, result->lowest, result->highest);
        else
            std::snprintf(text, sizeof(text), "Preview %dx%d.", preview_w, preview_h);
        if (!ui->exporting)
            gtk_label_set_text(GTK_LABEL(ui->status), text);
        gtk_widget_queue_draw(ui->area);
        return G_SOURCE_REMOVE;
    }

    void preview_worker()
    {
        std::unique_lock<std::mutex> lock(ui->mutex);
        for (;;)
        {
            ui->wake.wait(lock, [] { return ui->has_request || ui->quit; });
            if (ui->quit)
                return;
            const kosmos::PlanetStyle style = ui->wanted;
            const double radius = ui->wanted_radius;
            const float strength = ui->wanted_strength;
            const unsigned generation = ui->generation.load();
            ui->has_request = false;
            lock.unlock();

            auto result = std::make_unique<PreviewResult>();
            result->generation = generation;
            result->rendered = std::make_unique<Rendered>();
            try
            {
                World world(style, radius, strength);
                LayerSet all = LayerSet::all();
                for (int i = 0; i < layer_count; ++i)
                {
                    result->applicable[i] = world.applicable(i);
                    all.on[i] = result->applicable[i];
                }
                result->lowest = world.lowest();
                result->highest = world.highest();
                result->ok = render_region(world, Region{0, 0, 1, 1, preview_w, preview_h}, all, *result->rendered, max_threads, result->error);
            }
            catch (const std::exception &e)
            {
                result->ok = false;
                result->error = e.what();
            }
            g_idle_add(show_preview, result.release());
            lock.lock();
        }
    }

    void read_style(kosmos::PlanetStyle &style, double &radius)
    {
        style.type = static_cast<kosmos::Archetype>(1 + gtk_combo_box_get_active(GTK_COMBO_BOX(ui->type_combo)));
        style.seed = static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->seed)));
        style.ocean = static_cast<float>(gtk_range_get_value(GTK_RANGE(ui->ocean)));
        style.ice = static_cast<float>(gtk_range_get_value(GTK_RANGE(ui->ice)));
        style.relief = static_cast<float>(gtk_range_get_value(GTK_RANGE(ui->relief)));
        style.bands = static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->bands)));
        style.contrast = static_cast<float>(gtk_range_get_value(GTK_RANGE(ui->contrast)));
        GdkRGBA colour;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(ui->tint), &colour);
        style.tint[0] = static_cast<float>(colour.red);
        style.tint[1] = static_cast<float>(colour.green);
        style.tint[2] = static_cast<float>(colour.blue);
        radius = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->radius)) * 1000.0;
    }

    void update_estimate()
    {
        Job job = ui->job;
        double radius;
        read_style(job.style, radius);
        job.radius = radius;
        const char *widths[] = {"2048", "4096", "8192", "16384", "32768", "65536"};
        const int index = std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(ui->width_combo)));
        job.width = std::atoi(widths[index]);
        job.tiles = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->tiles_check));
        job.format = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->format_combo)) == 1 ? Format::dds : Format::png;
        job.dds = static_cast<DdsKind>(std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(ui->dds_combo))));
        for (int i = 0; i < layer_count; ++i)
            job.layers.on[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->export_layer[i]));
        const Estimate e = estimate(job);
        char text[256];
        std::snprintf(text, sizeof(text), "%d x %d, %d layer(s)%s: about %s on disk%s%s", e.width, e.height, e.layer_count, job.tiles ? ", tiles" : "",
                      format_bytes(e.disk_bytes).c_str(), e.fits ? "" : ". ", e.fits ? "" : e.problem.c_str());
        gtk_label_set_text(GTK_LABEL(ui->estimate_label), text);
        gtk_widget_set_sensitive(ui->export_button, e.fits && e.layer_count > 0 && !ui->exporting);
    }

    gboolean fire_preview(gpointer)
    {
        ui->debounce = 0;
        {
            std::lock_guard<std::mutex> lock(ui->mutex);
            read_style(ui->wanted, ui->wanted_radius);
            ui->wanted_strength = ui->job.normal_strength;
            ui->generation.fetch_add(1);
            ui->has_request = true;
        }
        ui->wake.notify_one();
        gtk_label_set_text(GTK_LABEL(ui->status), "Rendering preview...");
        return G_SOURCE_REMOVE;
    }

    void params_changed(GtkWidget *, gpointer)
    {
        if (ui->loading)
            return;
        if (ui->debounce)
            g_source_remove(ui->debounce);
        ui->debounce = g_timeout_add(250, fire_preview, nullptr);
        update_estimate();
    }

    void export_options_changed(GtkWidget *, gpointer)
    {
        if (ui->loading)
            return;
        gtk_widget_set_sensitive(ui->dds_combo, gtk_combo_box_get_active(GTK_COMBO_BOX(ui->format_combo)) == 1);
        update_estimate();
    }

    void layer_toggled(GtkWidget *button, gpointer data)
    {
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
            return;
        ui->layer = GPOINTER_TO_INT(data);
        gtk_widget_queue_draw(ui->area);
    }

    gboolean draw_preview(GtkWidget *widget, cairo_t *cr, gpointer)
    {
        const int w = gtk_widget_get_allocated_width(widget), h = gtk_widget_get_allocated_height(widget);
        cairo_set_source_rgb(cr, 0.08, 0.08, 0.1);
        cairo_paint(cr);
        cairo_surface_t *s = ui->surface[ui->layer];
        if (!s)
            return FALSE;
        const double scale = std::min(static_cast<double>(w) / preview_w, static_cast<double>(h) / preview_h);
        cairo_translate(cr, (w - preview_w * scale) / 2, (h - preview_h * scale) / 2);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_paint(cr);
        if (ui->pack_open && ui->pack.tiled && ui->has_sel)
        {
            double u0, v0, u1, v1;
            tile_bounds(ui->sel, u0, v0, u1, v1);
            cairo_set_source_rgb(cr, 1, 0.2, 0.2);
            cairo_set_line_width(cr, 2.0 / scale);
            for (double shift : {0.0, -1.0}) // a tile across the map's edge is drawn in two halves
                cairo_rectangle(cr, (u0 + shift) * preview_w, v0 * preview_h, (u1 - u0) * preview_w, (v1 - v0) * preview_h);
            cairo_stroke(cr);
        }
        return FALSE;
    }

    struct ProgressMessage
    {
        double fraction;
        std::string text;
        bool finished = false, ok = true;
    };

    gboolean show_progress(gpointer data)
    {
        std::unique_ptr<ProgressMessage> m(static_cast<ProgressMessage *>(data));
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), m->fraction);
        gtk_label_set_text(GTK_LABEL(ui->status), m->text.c_str());
        if (m->finished)
        {
            if (ui->exporter.joinable())
                ui->exporter.join();
            ui->exporting = false;
            gtk_widget_set_sensitive(ui->cancel_button, FALSE);
            update_estimate();
        }
        return G_SOURCE_REMOVE;
    }

    void start_export(GtkWidget *, gpointer)
    {
        if (ui->exporting)
            return;
        Job job = ui->job;
        read_style(job.style, job.radius);
        const char *widths[] = {"2048", "4096", "8192", "16384", "32768", "65536"};
        job.width = std::atoi(widths[std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(ui->width_combo)))]);
        job.tiles = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->tiles_check));
        job.format = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->format_combo)) == 1 ? Format::dds : Format::png;
        job.dds = static_cast<DdsKind>(std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(ui->dds_combo))));
        for (int i = 0; i < layer_count; ++i)
            job.layers.on[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->export_layer[i]));
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(ui->folder));
        job.out = folder ? folder : "";
        g_free(folder);
        job.name = gtk_entry_get_text(GTK_ENTRY(ui->name));
        if (job.out.empty())
        {
            gtk_label_set_text(GTK_LABEL(ui->status), "Choose an output folder first.");
            return;
        }
        ui->exporting = true;
        ui->cancel = false;
        gtk_widget_set_sensitive(ui->export_button, FALSE);
        gtk_widget_set_sensitive(ui->cancel_button, TRUE);
        ui->exporter = std::thread([job] {
            std::string error;
            bool ok;
            try
            {
                ok = run(job, [](double f, const std::string &text) {
                    g_idle_add(show_progress, new ProgressMessage{f, text});
                    return !ui->cancel.load();
                }, error);
            }
            catch (const std::exception &e)
            {
                ok = false;
                error = e.what();
            }
            auto *m = new ProgressMessage{ok ? 1.0 : 0.0, ok ? "Done. Files are in " + job.out : (error == "stopped" ? "Stopped. Tiles already written are kept; export again to resume." : "Export failed: " + error)};
            m->finished = true;
            g_idle_add(show_progress, m);
        });
    }

    void cancel_export(GtkWidget *, gpointer) { ui->cancel = true; }

    GtkWidget *row_label(GtkWidget *grid, int row, const char *text, GtkWidget *widget)
    {
        GtkWidget *label = gtk_label_new(text);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
        gtk_widget_set_hexpand(widget, TRUE);
        gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
        return widget;
    }

    GtkWidget *slider(double value)
    {
        GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
        gtk_range_set_value(GTK_RANGE(s), value);
        g_signal_connect(s, "value-changed", G_CALLBACK(params_changed), nullptr);
        return s;
    }

    GtkWidget *combo(std::initializer_list<const char *> items, int active)
    {
        GtkWidget *c = gtk_combo_box_text_new();
        for (const char *item : items)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), item);
        gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
        return c;
    }

    // ---- opened packs: click a tile, edit it outside, lock, fill ----

    void say(const std::string &text)
    {
        gtk_label_set_text(GTK_LABEL(ui->status), text.c_str());
    }

    void pump()
    {
        while (gtk_events_pending())
            gtk_main_iteration_do(FALSE);
    }

    void refresh_selection()
    {
        const bool tiled = ui->pack_open && ui->pack.tiled;
        const bool have = tiled && ui->has_sel;
        std::string text = ui->pack_open ? ui->pack.dir : "No pack open";
        if (have)
        {
            const TileRef &t = ui->sel;
            text += "\nTile " + std::to_string(t.column) + "," + std::to_string(t.row) + " at level " + std::to_string(t.level);
            if (is_edit_locked(ui->pack, ui->layer, t))
                text += " (edited, locked)";
            else if (is_locked(ui->pack, ui->layer, t))
                text += " (locked)";
        }
        else if (ui->pack_open && !tiled)
            text += "\nSingle images: whole layers only";
        gtk_label_set_text(GTK_LABEL(ui->pack_label), text.c_str());
        for (GtkWidget *w : {ui->edit_button, ui->import_button, ui->restore_button, ui->lock_tile_button})
            gtk_widget_set_sensitive(w, have);
        for (GtkWidget *w : {ui->lock_layer_button, ui->lock_pack_button, ui->fill_button})
            gtk_widget_set_sensitive(w, ui->pack_open);
        gtk_widget_queue_draw(ui->area);
    }

    void reload_pack_preview()
    {
        for (int i = 0; i < layer_count; ++i)
        {
            if (ui->surface[i])
                cairo_surface_destroy(ui->surface[i]);
            ui->surface[i] = nullptr;
            bool shown = false;
            if (ui->pack.layer[i].present)
            {
                Rendered r;
                std::string error;
                if (pack_preview(ui->pack, i, preview_w, preview_h, r, error))
                {
                    ui->surface[i] = make_surface(i, r);
                    shown = true;
                }
                else
                    say(error);
            }
            gtk_widget_set_sensitive(ui->layer_radio[i], shown);
        }
        if (!ui->surface[ui->layer])
            for (int i = 0; i < layer_count; ++i)
                if (ui->surface[i])
                {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->layer_radio[i]), TRUE);
                    break;
                }
    }

    void open_pack_dir(const std::string &dir)
    {
        Pack pack;
        std::string error;
        if (!open_pack(dir, pack, error))
        {
            say("Could not open the pack: " + error);
            return;
        }
        ui->pack = pack;
        ui->pack_open = true;
        ui->has_sel = false;
        ui->generation.fetch_add(1); // drop any generator preview still on its way
        say("Opened " + dir);
        reload_pack_preview();
        gtk_range_set_range(GTK_RANGE(ui->level_scale), 0, std::max(0, pack.max_level()));
        gtk_range_set_value(GTK_RANGE(ui->level_scale), std::max(0, pack.max_level()));
        refresh_selection();
    }

    void open_clicked(GtkWidget *, gpointer)
    {
        GtkWidget *dialog = gtk_file_chooser_dialog_new("Open a pack folder", GTK_WINDOW(ui->window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Cancel",
                                                       GTK_RESPONSE_CANCEL, "Open", GTK_RESPONSE_ACCEPT, nullptr);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        {
            char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (folder)
            {
                open_pack_dir(folder);
                g_free(folder);
            }
        }
        gtk_widget_destroy(dialog);
    }

    gboolean area_pressed(GtkWidget *widget, GdkEventButton *event, gpointer)
    {
        if (!ui->pack_open || !ui->pack.tiled || event->button != 1)
            return FALSE;
        const int w = gtk_widget_get_allocated_width(widget), h = gtk_widget_get_allocated_height(widget);
        const double scale = std::min(static_cast<double>(w) / preview_w, static_cast<double>(h) / preview_h);
        const double u = (event->x - (w - preview_w * scale) / 2) / (preview_w * scale);
        const double v = (event->y - (h - preview_h * scale) / 2) / (preview_h * scale);
        if (u < 0 || u >= 1 || v < 0 || v >= 1)
            return FALSE;
        ui->sel = tile_at(u, v, static_cast<int>(gtk_range_get_value(GTK_RANGE(ui->level_scale))));
        ui->has_sel = true;
        refresh_selection();
        return TRUE;
    }

    void level_changed(GtkWidget *, gpointer)
    {
        if (ui->has_sel)
        {
            ui->has_sel = false;
            refresh_selection();
        }
    }

    bool import_now()
    {
        std::string error;
        const std::string file = edit_work_path(ui->pack, ui->layer, ui->sel);
        if (!import_edit(ui->pack, ui->layer, ui->sel, file, error))
        {
            say("Import failed: " + error);
            return false;
        }
        say("Imported the edited tile; it is locked now");
        reload_pack_preview();
        refresh_selection();
        return true;
    }

    void edit_clicked(GtkWidget *, gpointer)
    {
        std::string file, error;
        if (!begin_edit(ui->pack, ui->layer, ui->sel, file, error))
        {
            say("Cannot edit: " + error);
            return;
        }
        Settings settings = load_settings();
        const char *typed = gtk_entry_get_text(GTK_ENTRY(ui->editor_entry));
        if (typed && *typed && settings.editor != typed)
        {
            settings.editor = typed;
            save_settings(settings, error);
        }
        if (!launch_editor(settings.editor, file, error))
        {
            say("Could not start the editor: " + error);
            return;
        }
        struct stat st;
        ui->watch_file = file;
        ui->watch_mtime = ui->watch_imported_mtime = stat(file.c_str(), &st) == 0 ? st.st_mtime : 0;
        if (!ui->watch)
            ui->watch = g_timeout_add(1500, [](gpointer) -> gboolean {
                struct stat s;
                if (!ui->has_sel || stat(ui->watch_file.c_str(), &s) != 0)
                    return G_SOURCE_CONTINUE;
                if (s.st_mtime != ui->watch_imported_mtime && s.st_mtime == ui->watch_mtime)
                {
                    ui->watch_imported_mtime = s.st_mtime;
                    import_now();
                }
                ui->watch_mtime = s.st_mtime;
                return G_SOURCE_CONTINUE;
            }, nullptr);
        say("Opened in " + settings.editor + ". Save there; the tile is taken back automatically, or press Import edit.");
    }

    void import_clicked(GtkWidget *, gpointer)
    {
        import_now();
    }

    void restore_clicked(GtkWidget *, gpointer)
    {
        std::string error;
        if (restore_original(ui->pack, ui->layer, ui->sel, error))
        {
            say("Restored the original tile");
            reload_pack_preview();
        }
        else
            say("Cannot restore: " + error);
        refresh_selection();
    }

    void toggle_lock(LockScope scope)
    {
        std::string error;
        const bool now = is_locked(ui->pack, scope, ui->layer, ui->sel);
        if (!set_locked(ui->pack, scope, ui->layer, ui->sel, !now, error))
            say("Cannot change the lock: " + error);
        else
            say(now ? "Unlocked" : "Locked");
        refresh_selection();
    }

    void lock_tile_clicked(GtkWidget *, gpointer) { toggle_lock(LockScope::tile); }
    void lock_layer_clicked(GtkWidget *, gpointer) { toggle_lock(LockScope::layer); }
    void lock_pack_clicked(GtkWidget *, gpointer) { toggle_lock(LockScope::pack); }

    void fill_clicked(GtkWidget *, gpointer)
    {
        GtkWidget *dialog = gtk_file_chooser_dialog_new("Take missing data from a picture or folder (Cancel to use the generator)", GTK_WINDOW(ui->window),
                                                       GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Generator only", GTK_RESPONSE_CANCEL, "Use folder", GTK_RESPONSE_ACCEPT, nullptr);
        FillOptions options;
        options.threads = max_threads;
        std::string error;
        const Progress progress = [](double fraction, const std::string &text) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), fraction);
            say(text);
            pump();
            return true;
        };
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        {
            char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (folder)
            {
                add_source(options.sources, folder, progress, error);
                g_free(folder);
            }
        }
        gtk_widget_destroy(dialog);
        for (int i = 0; i < layer_count; ++i)
            options.layers.on[i] = ui->pack.layer[i].present || !options.sources.layer[i].path.empty();
        FillReport report;
        if (!fill_pack(ui->pack, options, progress, report, error))
            say("Fill failed: " + error);
        else
            say("Filled " + std::to_string(report.written()) + " missing tiles or images");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), 0);
        open_pack_dir(ui->pack.dir);
    }

    GtkWidget *pack_button(const char *label, GCallback callback, GtkWidget *grid, int column, int row)
    {
        GtkWidget *b = gtk_button_new_with_label(label);
        g_signal_connect(b, "clicked", callback, nullptr);
        gtk_grid_attach(GTK_GRID(grid), b, column, row, 1, 1);
        return b;
    }

    void activate(GtkApplication *app, gpointer)
    {
        const kosmos::PlanetStyle &s = ui->job.style;
        ui->loading = true;
        ui->window = gtk_application_window_new(app);
        gtk_window_set_title(GTK_WINDOW(ui->window), "planetbake");
        gtk_window_set_default_size(GTK_WINDOW(ui->window), 1360, 640);

        GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(root), 8);
        gtk_container_add(GTK_CONTAINER(ui->window), root);

        // Left: the preview and the layer buttons.
        GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_pack_start(GTK_BOX(root), left, TRUE, TRUE, 0);
        ui->area = gtk_drawing_area_new();
        gtk_widget_set_size_request(ui->area, 640, 320);
        g_signal_connect(ui->area, "draw", G_CALLBACK(draw_preview), nullptr);
        gtk_widget_add_events(ui->area, GDK_BUTTON_PRESS_MASK);
        g_signal_connect(ui->area, "button-press-event", G_CALLBACK(area_pressed), nullptr);
        gtk_box_pack_start(GTK_BOX(left), ui->area, TRUE, TRUE, 0);
        GtkWidget *layers = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *group = nullptr;
        const char *titles[] = {"Colour", "Height", "Normal", "Specular", "Night", "Clouds"};
        for (int i = 0; i < layer_count; ++i)
        {
            ui->layer_radio[i] = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(group), titles[i]);
            gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(ui->layer_radio[i]), FALSE);
            group = ui->layer_radio[i];
            g_signal_connect(ui->layer_radio[i], "toggled", G_CALLBACK(layer_toggled), GINT_TO_POINTER(i));
            gtk_box_pack_start(GTK_BOX(layers), ui->layer_radio[i], TRUE, TRUE, 0);
        }
        gtk_box_pack_start(GTK_BOX(left), layers, FALSE, FALSE, 0);
        ui->status = gtk_label_new("");
        gtk_widget_set_halign(ui->status, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(ui->status), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(left), ui->status, FALSE, FALSE, 0);
        ui->progress = gtk_progress_bar_new();
        gtk_box_pack_start(GTK_BOX(left), ui->progress, FALSE, FALSE, 0);

        // Right: the world, then the export.
        GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_size_request(right, 340, -1);
        gtk_box_pack_start(GTK_BOX(root), right, FALSE, FALSE, 0);

        GtkWidget *pack_grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(pack_grid), 4);
        gtk_grid_set_column_spacing(GTK_GRID(pack_grid), 6);
        gtk_grid_set_column_homogeneous(GTK_GRID(pack_grid), TRUE);
        GtkWidget *pack_frame = gtk_frame_new("Open pack");
        gtk_container_add(GTK_CONTAINER(pack_frame), pack_grid);
        gtk_box_pack_start(GTK_BOX(right), pack_frame, FALSE, FALSE, 0);
        GtkWidget *open_button = pack_button("Open pack...", G_CALLBACK(open_clicked), pack_grid, 0, 0);
        (void)open_button;
        ui->fill_button = pack_button("Fill missing...", G_CALLBACK(fill_clicked), pack_grid, 1, 0);
        ui->pack_label = gtk_label_new("No pack open");
        gtk_widget_set_halign(ui->pack_label, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(ui->pack_label), PANGO_ELLIPSIZE_START);
        gtk_grid_attach(GTK_GRID(pack_grid), ui->pack_label, 0, 1, 2, 1);
        ui->level_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 8, 1);
        gtk_scale_set_digits(GTK_SCALE(ui->level_scale), 0);
        gtk_widget_set_tooltip_text(ui->level_scale, "Tile level to pick (click the map)");
        g_signal_connect(ui->level_scale, "value-changed", G_CALLBACK(level_changed), nullptr);
        gtk_grid_attach(GTK_GRID(pack_grid), ui->level_scale, 0, 2, 2, 1);
        ui->edit_button = pack_button("Edit tile", G_CALLBACK(edit_clicked), pack_grid, 0, 3);
        ui->import_button = pack_button("Import edit", G_CALLBACK(import_clicked), pack_grid, 1, 3);
        ui->restore_button = pack_button("Restore original", G_CALLBACK(restore_clicked), pack_grid, 0, 4);
        ui->lock_tile_button = pack_button("Lock/unlock tile", G_CALLBACK(lock_tile_clicked), pack_grid, 1, 4);
        ui->lock_layer_button = pack_button("Lock/unlock layer", G_CALLBACK(lock_layer_clicked), pack_grid, 0, 5);
        ui->lock_pack_button = pack_button("Lock/unlock pack", G_CALLBACK(lock_pack_clicked), pack_grid, 1, 5);
        ui->editor_entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(ui->editor_entry), load_settings().editor.c_str());
        gtk_entry_set_placeholder_text(GTK_ENTRY(ui->editor_entry), "graphics program, e.g. krita");
        gtk_grid_attach(GTK_GRID(pack_grid), ui->editor_entry, 0, 6, 2, 1);

        GtkWidget *world = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(world), 4);
        gtk_grid_set_column_spacing(GTK_GRID(world), 8);
        GtkWidget *world_frame = gtk_frame_new("World");
        gtk_container_add(GTK_CONTAINER(world_frame), world);
        gtk_box_pack_start(GTK_BOX(right), world_frame, FALSE, FALSE, 0);
        int r = 0;
        ui->type_combo = row_label(world, r++, "Type", combo({"Mercury-like", "Venus-like", "Earth-like", "Mars-like", "Gas giant"}, std::max(0, static_cast<int>(s.type) - 1)));
        g_signal_connect(ui->type_combo, "changed", G_CALLBACK(params_changed), nullptr);
        ui->seed = row_label(world, r++, "Seed", gtk_spin_button_new_with_range(0, 999999, 1));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->seed), s.seed);
        g_signal_connect(ui->seed, "value-changed", G_CALLBACK(params_changed), nullptr);
        ui->ocean = row_label(world, r++, "Ocean", slider(s.ocean));
        ui->ice = row_label(world, r++, "Ice", slider(s.ice));
        ui->relief = row_label(world, r++, "Relief", slider(s.relief));
        gtk_range_set_range(GTK_RANGE(ui->relief), 0.0, 3.0);
        gtk_range_set_value(GTK_RANGE(ui->relief), s.relief);
        GdkRGBA tint = {s.tint[0], s.tint[1], s.tint[2], 1.0};
        ui->tint = row_label(world, r++, "Tint", gtk_color_button_new_with_rgba(&tint));
        g_signal_connect(ui->tint, "color-set", G_CALLBACK(params_changed), nullptr);
        ui->bands = row_label(world, r++, "Bands (gas)", gtk_spin_button_new_with_range(1, 40, 1));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->bands), s.bands);
        g_signal_connect(ui->bands, "value-changed", G_CALLBACK(params_changed), nullptr);
        ui->contrast = row_label(world, r++, "Contrast (gas)", slider(s.contrast));
        ui->radius = row_label(world, r++, "Radius (km)", gtk_spin_button_new_with_range(100, 100000, 10));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->radius), ui->job.radius / 1000.0);
        g_signal_connect(ui->radius, "value-changed", G_CALLBACK(params_changed), nullptr);

        GtkWidget *out = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(out), 4);
        gtk_grid_set_column_spacing(GTK_GRID(out), 8);
        GtkWidget *out_frame = gtk_frame_new("Export");
        gtk_container_add(GTK_CONTAINER(out_frame), out);
        gtk_box_pack_start(GTK_BOX(right), out_frame, FALSE, FALSE, 0);
        r = 0;
        int width_index = 1;
        for (int w = 2048, i = 0; i < 6; w *= 2, ++i)
            if (w <= ui->job.width)
                width_index = i;
        ui->width_combo = row_label(out, r++, "Width", combo({"2048", "4096", "8192", "16384", "32768 (tiles)", "65536 (tiles)"}, width_index));
        g_signal_connect(ui->width_combo, "changed", G_CALLBACK(export_options_changed), nullptr);
        GtkWidget *checks = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        for (int i = 0; i < layer_count; ++i)
        {
            ui->export_layer[i] = gtk_check_button_new_with_label(titles[i]);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->export_layer[i]), ui->job.layers.on[i]);
            g_signal_connect(ui->export_layer[i], "toggled", G_CALLBACK(export_options_changed), nullptr);
            gtk_box_pack_start(GTK_BOX(i < 3 ? checks : checks), ui->export_layer[i], FALSE, FALSE, 0);
        }
        gtk_widget_set_halign(checks, GTK_ALIGN_START);
        GtkWidget *checks_frame = gtk_flow_box_new();
        gtk_container_add(GTK_CONTAINER(checks_frame), checks);
        gtk_grid_attach(GTK_GRID(out), checks_frame, 0, r++, 2, 1);
        ui->format_combo = row_label(out, r++, "Format", combo({"PNG", "DDS"}, ui->job.format == Format::dds ? 1 : 0));
        g_signal_connect(ui->format_combo, "changed", G_CALLBACK(export_options_changed), nullptr);
        ui->dds_combo = row_label(out, r++, "DDS encoding", combo({"BC1 (smallest)", "BC3", "RGBA (uncompressed)"}, static_cast<int>(ui->job.dds)));
        gtk_widget_set_sensitive(ui->dds_combo, ui->job.format == Format::dds);
        g_signal_connect(ui->dds_combo, "changed", G_CALLBACK(export_options_changed), nullptr);
        ui->tiles_check = gtk_check_button_new_with_label("Tile pyramid (512 px tiles, resumable)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->tiles_check), ui->job.tiles);
        g_signal_connect(ui->tiles_check, "toggled", G_CALLBACK(export_options_changed), nullptr);
        gtk_grid_attach(GTK_GRID(out), ui->tiles_check, 0, r++, 2, 1);
        ui->folder = gtk_file_chooser_button_new("Output folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
        if (!ui->job.out.empty())
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(ui->folder), ui->job.out.c_str());
        row_label(out, r++, "Folder", ui->folder);
        ui->name = row_label(out, r++, "Name", gtk_entry_new());
        gtk_entry_set_text(GTK_ENTRY(ui->name), ui->job.name.empty() ? "planet" : ui->job.name.c_str());
        ui->estimate_label = gtk_label_new("");
        gtk_label_set_line_wrap(GTK_LABEL(ui->estimate_label), TRUE);
        gtk_widget_set_halign(ui->estimate_label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(out), ui->estimate_label, 0, r++, 2, 1);
        GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        ui->export_button = gtk_button_new_with_label("Export");
        ui->cancel_button = gtk_button_new_with_label("Cancel");
        gtk_widget_set_sensitive(ui->cancel_button, FALSE);
        g_signal_connect(ui->export_button, "clicked", G_CALLBACK(start_export), nullptr);
        g_signal_connect(ui->cancel_button, "clicked", G_CALLBACK(cancel_export), nullptr);
        gtk_box_pack_start(GTK_BOX(buttons), ui->export_button, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(buttons), ui->cancel_button, TRUE, TRUE, 0);
        gtk_grid_attach(GTK_GRID(out), buttons, 0, r++, 2, 1);

        ui->loading = false;
        gtk_widget_show_all(ui->window);
        update_estimate();
        refresh_selection();
        if (!ui->start_pack.empty())
            open_pack_dir(ui->start_pack);
        else
            fire_preview(nullptr);
    }
}

int run_gui(const planetbake::Job &initial, const std::string &pack_dir)
{
    kosmos_gtk_schemas("planetbake");
    Ui state;
    ui = &state;
    state.job = initial;
    state.start_pack = pack_dir;
    state.worker = std::thread(preview_worker);
    GtkApplication *app = gtk_application_new("org.kosmos.planetbake", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    const int code = g_application_run(G_APPLICATION(app), 0, nullptr);
    state.cancel = true;
    if (state.exporter.joinable())
        state.exporter.join();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.quit = true;
    }
    state.wake.notify_one();
    state.worker.join();
    g_object_unref(app);
    for (auto *s : state.surface)
        if (s)
            cairo_surface_destroy(s);
    ui = nullptr;
    return code;
}

#else

#include <cstdio>

int run_gui(const planetbake::Job &, const std::string &)
{
    std::fprintf(stderr, "planetbake was built without the GTK preview window. Run it with options; see --help.\n");
    return 1;
}

#endif
