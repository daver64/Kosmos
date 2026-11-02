// KosmosCAD: a small 3D modeller for Kosmos ships (four views, mesh editing, equipment markers). GTK 3 for the panels, OpenGL for every view.
//   kosmoscad [ship.json | scene.kmodel] [--shot FILE [--size WxH] [--select-face N] [--mode object|vertex|edge|face]]
// --shot draws the views to a PNG and exits.
#include "editor.h"
#include "gl_view.h"
#include "ship_io.h"

#include <epoxy/gl.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

using namespace cad;

namespace
{
    const char *kViewNames[4] = {"Top", "Front", "Side", "Perspective"};

    struct App
    {
        Editor ed;
        GlView gl;
        Camera cams[4];
        ViewState vs;
        bool gl_ok = false, updating = false, need_frame = true;

        GtkWidget *window = nullptr, *area = nullptr, *status = nullptr, *tree = nullptr, *labels[4] = {};
        GtkListStore *store = nullptr;
        GtkWidget *sp_pos[3] = {}, *sp_rot[3] = {}, *sp_scl[3] = {};
        GtkWidget *obj_name = nullptr, *obj_visible = nullptr, *obj_smooth = nullptr;
        GtkWidget *mk_box = nullptr, *mk_kind = nullptr, *mk_name = nullptr, *mk_props = nullptr, *mk_place = nullptr;
        GtkWidget *mat_combo = nullptr, *mat_name = nullptr, *mat_colour = nullptr, *mat_tex = nullptr;
        GtkWidget *sp_extrude = nullptr, *sp_inset = nullptr, *sp_weld = nullptr;
        GtkWidget *cb_uv = nullptr, *cb_uv_axis = nullptr;
        GtkWidget *mode_btn[4] = {}, *tool_btn[4] = {};

        int press_view = -1, press_button = 0, nav_view = -1;
        double press_x = 0, press_y = 0, last_x = 0, last_y = 0;
        bool pressed = false, moved = false, gizmo_drag = false, navigating = false;
        std::string backed_up;

        std::string shot_file;
        int shot_w = 1280, shot_h = 800, shot_frames = 0;
    };
    App *A = nullptr;

    // ---- small helpers --------------------------------------------------------------------------------------------

    void render() { if (A->area) gtk_widget_queue_draw(A->area); }

    void say(const std::string &text) { gtk_label_set_text(GTK_LABEL(A->status), text.c_str()); }

    GtkWidget *spin(double lo, double hi, double step, int digits, double value)
    {
        GtkWidget *s = gtk_spin_button_new_with_range(lo, hi, step);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s), digits);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), value);
        gtk_entry_set_width_chars(GTK_ENTRY(s), 7);
        return s;
    }
    double val(GtkWidget *s) { return gtk_spin_button_get_value(GTK_SPIN_BUTTON(s)); }

    struct Field
    {
        const char *label;
        double value, lo, hi;
        int digits;
    };

    // A small modal form of number fields; returns false on cancel.
    bool ask(const char *title, std::vector<Field> &fields)
    {
        GtkWidget *dlg = gtk_dialog_new_with_buttons(title, GTK_WINDOW(A->window), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
        gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
        std::vector<GtkWidget *> spins;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            gtk_grid_attach(GTK_GRID(grid), gtk_label_new(fields[i].label), 0, static_cast<int>(i), 1, 1);
            spins.push_back(spin(fields[i].lo, fields[i].hi, fields[i].digits ? 0.1 : 1, fields[i].digits, fields[i].value));
            gtk_grid_attach(GTK_GRID(grid), spins.back(), 1, static_cast<int>(i), 1, 1);
        }
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), grid);
        gtk_widget_show_all(dlg);
        const bool ok = gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK;
        for (size_t i = 0; i < fields.size(); ++i)
            fields[i].value = val(spins[i]);
        gtk_widget_destroy(dlg);
        return ok;
    }

    std::string choose(GtkFileChooserAction action, const char *title, const char *pattern, const char *name = "")
    {
        GtkWidget *dlg = gtk_file_chooser_dialog_new(title, GTK_WINDOW(A->window), action, "_Cancel", GTK_RESPONSE_CANCEL,
                                                     action == GTK_FILE_CHOOSER_ACTION_OPEN || action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER ? "_Open" : "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        if (pattern && *pattern)
        {
            GtkFileFilter *f = gtk_file_filter_new();
            gtk_file_filter_set_name(f, pattern);
            gtk_file_filter_add_pattern(f, pattern);
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);
        }
        if (action == GTK_FILE_CHOOSER_ACTION_SAVE)
        {
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), name);
        }
        std::string out;
        if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT)
        {
            char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
            if (f)
                out = f;
            g_free(f);
        }
        gtk_widget_destroy(dlg);
        return out;
    }

    bool confirm(const char *text)
    {
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(A->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", text);
        gtk_dialog_add_buttons(GTK_DIALOG(dlg), "_Cancel", GTK_RESPONSE_CANCEL, "_Discard", GTK_RESPONSE_OK, nullptr);
        const bool ok = gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK;
        gtk_widget_destroy(dlg);
        return ok;
    }

    bool discard_ok() { return !A->ed.doc.dirty || confirm("There are unsaved changes. Discard them?"); }

    // ---- UI refresh -----------------------------------------------------------------------------------------------

    void rebuild_tree()
    {
        A->updating = true;
        gtk_list_store_clear(A->store);
        GtkTreeIter it;
        const Scene &s = A->ed.scene();
        for (size_t i = 0; i < s.objects.size(); ++i)
        {
            gtk_list_store_append(A->store, &it);
            const std::string t = (s.objects[i].visible ? "" : "(hidden) ") + s.objects[i].name;
            gtk_list_store_set(A->store, &it, 0, t.c_str(), 1, 0, 2, static_cast<int>(i), -1);
        }
        for (size_t i = 0; i < s.markers.size(); ++i)
        {
            gtk_list_store_append(A->store, &it);
            const Marker &m = s.markers[i];
            const KindInfo *k = find_kind(m.kind);
            const std::string t = std::string("◆ ") + (k ? k->label : m.kind) + (m.name.empty() ? "" : " " + m.name) + (m.placed ? "" : " (not placed)");
            gtk_list_store_set(A->store, &it, 0, t.c_str(), 1, 1, 2, static_cast<int>(i), -1);
        }
        A->updating = false;
    }

    void rebuild_materials()
    {
        A->updating = true;
        const int keep = std::max(gtk_combo_box_get_active(GTK_COMBO_BOX(A->mat_combo)), 0);
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(A->mat_combo));
        for (const Material &m : A->ed.scene().materials)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(A->mat_combo), m.name.c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(A->mat_combo), std::min(keep, static_cast<int>(A->ed.scene().materials.size()) - 1));
        A->updating = false;
    }

    int current_material() { return std::clamp(gtk_combo_box_get_active(GTK_COMBO_BOX(A->mat_combo)), 0, static_cast<int>(A->ed.scene().materials.size()) - 1); }

    // Marker that the transform and property panels edit: the single selected marker when no object is selected.
    Marker *edit_marker()
    {
        Editor &ed = A->ed;
        if (ed.sel.markers.size() >= 1 && ed.sel.objects.empty())
        {
            const int i = *ed.sel.markers.begin();
            if (i < static_cast<int>(ed.scene().markers.size()))
                return &ed.scene().markers[i];
        }
        return nullptr;
    }

    void sync_transform()
    {
        A->updating = true;
        Editor &ed = A->ed;
        Xform x;
        bool have = false;
        if (Marker *m = edit_marker())
        {
            x = Xform{m->pos, m->rot, m->size};
            have = true;
        }
        else if (const Object *o = ed.active_object())
        {
            x = o->xf;
            have = true;
        }
        for (int i = 0; i < 3; ++i)
        {
            gtk_widget_set_sensitive(A->sp_pos[i], have);
            gtk_widget_set_sensitive(A->sp_rot[i], have);
            gtk_widget_set_sensitive(A->sp_scl[i], have);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(A->sp_pos[i]), x.pos[i]);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(A->sp_rot[i]), x.rot[i]);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(A->sp_scl[i]), x.scale[i]);
        }
        A->updating = false;
    }

    void sync_ui(bool tree_selection = true)
    {
        Editor &ed = A->ed;
        sync_transform();
        A->updating = true;
        const Object *o = ed.active_object();
        const bool obj = o && !edit_marker();
        gtk_widget_set_sensitive(A->obj_name, obj);
        gtk_widget_set_sensitive(A->obj_visible, obj);
        gtk_widget_set_sensitive(A->obj_smooth, obj);
        gtk_entry_set_text(GTK_ENTRY(A->obj_name), obj ? o->name.c_str() : "");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(A->obj_visible), obj ? o->visible : TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(A->obj_smooth), obj ? o->smooth : FALSE);

        Marker *m = edit_marker();
        gtk_widget_set_visible(A->mk_box, m != nullptr);
        if (m)
        {
            const KindInfo *k = find_kind(m->kind);
            gtk_label_set_text(GTK_LABEL(A->mk_kind), k ? k->label.c_str() : m->kind.c_str());
            gtk_entry_set_text(GTK_ENTRY(A->mk_name), m->name.c_str());
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(A->mk_props)), m->props.dump(2).c_str(), -1);
            gtk_widget_set_visible(A->mk_place, !m->placed);
        }
        // Tree selection follows the editor (not while the tree itself is what changed).
        GtkTreeSelection *ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(A->tree));
        GtkTreeIter it;
        if (tree_selection)
            gtk_tree_selection_unselect_all(ts);
        for (bool ok = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(A->store), &it); ok; ok = gtk_tree_model_iter_next(GTK_TREE_MODEL(A->store), &it))
        {
            int type, index;
            gtk_tree_model_get(GTK_TREE_MODEL(A->store), &it, 1, &type, 2, &index, -1);
            if (tree_selection && ((type == 0 && ed.sel.objects.count(index)) || (type == 1 && ed.sel.markers.count(index))))
                gtk_tree_selection_select_iter(ts, &it);
        }
        for (int i = 0; i < 4; ++i)
        {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(A->mode_btn[i]), static_cast<int>(ed.mode) == i);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(A->tool_btn[i]), static_cast<int>(ed.tool) == i);
        }
        A->updating = false;

        char text[256];
        std::snprintf(text, sizeof text, "%s%s   objects %zu  markers %zu   selected: %zu objects, %zu markers%s%s", ed.doc.file.empty() ? "untitled" : ed.doc.file.filename().string().c_str(),
                      ed.doc.dirty ? " *" : "", ed.scene().objects.size(), ed.scene().markers.size(), ed.sel.objects.size(), ed.sel.markers.size(),
                      ed.mode == Mode::Vertex ? "   verts " : (ed.mode == Mode::Edge ? "   edges " : (ed.mode == Mode::Face ? "   faces " : "")),
                      ed.mode == Mode::Vertex ? std::to_string(ed.sel.verts.size()).c_str() : (ed.mode == Mode::Edge ? std::to_string(ed.sel.edges.size()).c_str() : (ed.mode == Mode::Face ? std::to_string(ed.sel.faces.size()).c_str() : "")));
        say(text);
        gtk_window_set_title(GTK_WINDOW(A->window), (std::string("KosmosCAD - ") + (ed.doc.file.empty() ? (ed.scene().ship_file.empty() ? "untitled" : fs::path(ed.scene().ship_file).parent_path().filename().string()) : ed.doc.file.filename().string()) + (ed.doc.dirty ? " *" : "")).c_str());
    }

    void refresh()
    {
        rebuild_tree();
        rebuild_materials();
        sync_ui();
        render();
    }

    void clamp_selection()
    {
        Selection &s = A->ed.sel;
        const int no = static_cast<int>(A->ed.scene().objects.size()), nm = static_cast<int>(A->ed.scene().markers.size());
        for (auto it = s.objects.begin(); it != s.objects.end();)
            it = *it >= no ? s.objects.erase(it) : std::next(it);
        for (auto it = s.markers.begin(); it != s.markers.end();)
            it = *it >= nm ? s.markers.erase(it) : std::next(it);
        if (s.active >= no)
            s.active = s.objects.empty() ? -1 : *s.objects.begin();
        s.clear_elements();
    }

    void frame_all()
    {
        V3 lo, hi;
        const bool any = A->ed.bounds(lo, hi);
        if (!any)
        {
            lo = {-5, -5, -5};
            hi = {5, 5, 5};
        }
        GtkAllocation a;
        gtk_widget_get_allocation(A->area, &a);
        for (int i = 0; i < 4; ++i)
        {
            const Rect r = view_rect(i, a.width, a.height, -1);
            A->cams[i].frame(lo, hi, std::max(r.w, 100), std::max(r.h, 100));
        }
    }

    void do_undo()
    {
        A->ed.doc.undo();
        clamp_selection();
        A->ed.changed();
        refresh();
    }
    void do_redo()
    {
        A->ed.doc.redo();
        clamp_selection();
        A->ed.changed();
        refresh();
    }

    // ---- files ----------------------------------------------------------------------------------------------------

    bool load_any(const std::string &path)
    {
        Scene s;
        std::string err;
        const fs::path p(path);
        bool ok;
        if (p.extension() == ".kmodel")
            ok = load_kmodel(p, s, err);
        else
            ok = load_ship(p, s, err);
        if (!ok)
        {
            say("Could not open " + path + ": " + err);
            GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(A->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "Could not open %s", fs::path(path).filename().string().c_str());
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", err.c_str());
            gtk_dialog_run(GTK_DIALOG(d));
            gtk_widget_destroy(d);
            return false;
        }
        A->ed.doc = Document();
        A->ed.doc.scene = std::move(s);
        if (p.extension() == ".kmodel")
            A->ed.doc.file = p;
        A->ed.sel.clear();
        A->ed.mesh_gen++;
        A->need_frame = true;
        refresh();
        say("Opened " + path);
        if (p.extension() != ".kmodel" && A->ed.doc.scene.objects.empty())
        {
            GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(A->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "No model in %s", fs::path(path).filename().string().c_str());
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", "None of this ship's parts has an OBJ model (GLB and generated props are not shown), so only its engines, gear and systems can be edited.");
            gtk_dialog_run(GTK_DIALOG(d));
            gtk_widget_destroy(d);
        }
        return true;
    }

    void act_new()
    {
        if (!discard_ok())
            return;
        A->ed.doc = Document();
        A->ed.sel.clear();
        A->ed.mesh_gen++;
        refresh();
    }
    void act_close()
    {
        if (!discard_ok())
            return;
        A->ed.doc = Document();
        A->ed.sel.clear();
        A->ed.sketch.clear();
        A->vs.sketching = false;
        A->ed.mesh_gen++;
        refresh();
        say("Closed");
    }
    void act_open()
    {
        if (!discard_ok())
            return;
        const std::string f = choose(GTK_FILE_CHOOSER_ACTION_OPEN, "Open ship.json or scene", "*.json");
        if (!f.empty())
            load_any(f);
    }
    void act_open_kmodel()
    {
        if (!discard_ok())
            return;
        const std::string f = choose(GTK_FILE_CHOOSER_ACTION_OPEN, "Open scene", "*.kmodel");
        if (!f.empty())
            load_any(f);
    }
    void act_save_kmodel(bool as)
    {
        Editor &ed = A->ed;
        if (as || ed.doc.file.empty())
        {
            const std::string f = choose(GTK_FILE_CHOOSER_ACTION_SAVE, "Save scene", "*.kmodel", "model.kmodel");
            if (f.empty())
                return;
            ed.doc.file = f;
        }
        std::string err;
        if (save_kmodel(ed.scene(), ed.doc.file, err))
        {
            ed.doc.dirty = false;
            sync_ui();
            say("Saved " + ed.doc.file.string());
        }
        else
            say("Save failed: " + err);
    }
    void act_save_ship(bool as)
    {
        Editor &ed = A->ed;
        std::string path = ed.scene().ship_file;
        if (as || path.empty())
        {
            path = choose(GTK_FILE_CHOOSER_ACTION_SAVE, "Save ship.json (the folder's models/ gets the new meshes)", "*.json", "ship.json");
            if (path.empty())
                return;
        }
        std::error_code ec;
        if (fs::exists(path, ec) && A->backed_up != path)
        {
            fs::copy_file(path, path + ".bak", fs::copy_options::overwrite_existing, ec);
            A->backed_up = path;
        }
        std::string err;
        if (save_ship(ed.scene(), path, err))
        {
            ed.scene().ship_file = path;
            ed.doc.dirty = false;
            sync_ui();
            say("Saved " + path + " (previous file kept as ship.json.bak)");
        }
        else
            say("Save failed: " + err);
    }
    void act_import_obj()
    {
        const std::string f = choose(GTK_FILE_CHOOSER_ACTION_OPEN, "Import OBJ", "*.obj");
        if (f.empty())
            return;
        std::string err;
        A->ed.doc.checkpoint();
        if (import_obj(f, A->ed.scene(), err))
        {
            A->ed.changed();
            A->ed.sel.clear();
            refresh();
            say("Imported " + f);
        }
        else
            say("Import failed: " + err);
    }
    void act_export_obj()
    {
        const std::string dir = choose(GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Export OBJ into folder", "");
        if (dir.empty())
            return;
        std::string err;
        const std::string name = A->ed.doc.file.empty() ? "model" : A->ed.doc.file.stem().string();
        if (export_obj(A->ed.scene(), dir, name, err))
            say("Exported " + dir + "/" + name + ".obj");
        else
            say("Export failed: " + err);
    }

    // ---- commands -------------------------------------------------------------------------------------------------

    void add_shape(ShapeKind kind)
    {
        std::vector<Field> f;
        switch (kind)
        {
        case ShapeKind::Box: f = {{"Width (X)", 2, 0.01, 1000, 2}, {"Height (Y)", 2, 0.01, 1000, 2}, {"Length (Z)", 2, 0.01, 1000, 2}}; break;
        case ShapeKind::Cylinder: f = {{"Radius", 1, 0.01, 1000, 2}, {"Height", 2, 0.01, 1000, 2}, {"Segments", 24, 3, 128, 0}}; break;
        case ShapeKind::Cone: f = {{"Bottom radius", 1, 0, 1000, 2}, {"Top radius", 0, 0, 1000, 2}, {"Height", 2, 0.01, 1000, 2}, {"Segments", 24, 3, 128, 0}}; break;
        case ShapeKind::Sphere: f = {{"Radius", 1, 0.01, 1000, 2}, {"Segments", 24, 4, 128, 0}}; break;
        case ShapeKind::Torus: f = {{"Major radius", 2, 0.01, 1000, 2}, {"Minor radius", 0.5, 0.01, 1000, 2}, {"Segments", 32, 3, 128, 0}}; break;
        case ShapeKind::Plane: f = {{"Width", 4, 0.01, 1000, 2}, {"Depth", 4, 0.01, 1000, 2}}; break;
        }
        if (!ask("Add shape", f))
            return;
        double a = f[0].value, b = f.size() > 1 ? f[1].value : 0, c = f.size() > 2 ? f[2].value : 0;
        int seg = 24;
        switch (kind)
        {
        case ShapeKind::Cylinder: seg = static_cast<int>(f[2].value); c = 0; break;
        case ShapeKind::Cone: seg = static_cast<int>(f[3].value); break;
        case ShapeKind::Sphere: seg = static_cast<int>(f[1].value); b = 0; break;
        case ShapeKind::Torus: seg = static_cast<int>(f[2].value); break;
        default: break;
        }
        A->ed.add_shape(kind, a, b, c, seg, {0, 0, 0});
        refresh();
    }

    void begin_sketch()
    {
        const int v = A->vs.active;
        if (v < 0 || v > 2)
        {
            say("Choose a Top, Front or Side view first (click in it), then start the sketch.");
            return;
        }
        A->ed.sketch.clear();
        A->ed.sketch_kind = A->cams[v].kind;
        A->vs.sketching = true;
        say("Sketch in the " + std::string(kViewNames[v]) + " view: click points, right-click removes the last, Enter = Extrude, L = Lathe (Front/Side), Esc cancels.");
        render();
    }
    void end_sketch()
    {
        A->ed.sketch.clear();
        A->vs.sketching = false;
        sync_ui();
        render();
    }
    void finish_sketch(bool lathe)
    {
        Editor &ed = A->ed;
        if (!A->vs.sketching)
            return;
        if (lathe)
        {
            if (ed.sketch_kind == Camera::Top || ed.sketch.size() < 2)
            {
                say("Lathe needs a profile of 2 or more points drawn in the Front or Side view (the vertical line through the origin is the axis).");
                return;
            }
            std::vector<Field> f = {{"Segments", 32, 3, 128, 0}};
            if (!ask("Lathe", f))
                return;
            ed.sketch_lathe(static_cast<int>(f[0].value));
        }
        else
        {
            if (ed.sketch.size() < 3)
            {
                say("Add at least 3 points first.");
                return;
            }
            std::vector<Field> f = {{"Depth", 2, 0.01, 1000, 2}, {"Far-end scale (1 = straight, <1 tapers)", 1, 0, 10, 2}};
            if (!ask("Extrude sketch", f))
                return;
            ed.sketch_extrude(f[0].value, f[1].value);
        }
        A->vs.sketching = false;
        refresh();
    }

    void set_mode(Mode m)
    {
        A->ed.set_mode(m);
        sync_ui();
        render();
    }
    void set_tool(Tool t)
    {
        A->ed.tool = t;
        sync_ui();
        render();
    }

    void toggle_wire()
    {
        Camera &c = A->cams[A->vs.active];
        c.wire = !c.wire;
        render();
    }
    void toggle_max()
    {
        A->vs.maximised = A->vs.maximised >= 0 ? -1 : A->vs.active;
        render();
    }

    // ---- GL callbacks ---------------------------------------------------------------------------------------------

    void on_realize(GtkWidget *w, gpointer)
    {
        gtk_gl_area_make_current(GTK_GL_AREA(w));
        if (gtk_gl_area_get_error(GTK_GL_AREA(w)) || !A->gl.init())
        {
            std::fprintf(stderr, "kosmoscad: OpenGL 3.2 is required but could not be started\n");
            A->gl_ok = false;
            return;
        }
        A->gl_ok = true;
    }
    void on_unrealize(GtkWidget *w, gpointer)
    {
        gtk_gl_area_make_current(GTK_GL_AREA(w));
        if (!gtk_gl_area_get_error(GTK_GL_AREA(w)))
            A->gl.release();
        A->gl_ok = false;
    }

    void save_shot()
    {
        const int sc = gtk_widget_get_scale_factor(A->area);
        const int w = gtk_widget_get_allocated_width(A->area) * sc, h = gtk_widget_get_allocated_height(A->area) * sc;
        std::vector<unsigned char> px = A->gl.read_pixels(w, h);
        for (size_t i = 3; i < px.size(); i += 4)
            px[i] = 255;
        GdkPixbuf *pb = gdk_pixbuf_new_from_data(px.data(), GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4, nullptr, nullptr);
        GError *err = nullptr;
        if (!gdk_pixbuf_save(pb, A->shot_file.c_str(), "png", &err, nullptr))
        {
            std::fprintf(stderr, "kosmoscad: could not write %s: %s\n", A->shot_file.c_str(), err ? err->message : "?");
            g_clear_error(&err);
        }
        g_object_unref(pb);
        gtk_main_quit();
    }

    gboolean on_render(GtkGLArea *area, GdkGLContext *, gpointer)
    {
        if (!A->gl_ok)
            return TRUE;
        const int W = gtk_widget_get_allocated_width(GTK_WIDGET(area)), H = gtk_widget_get_allocated_height(GTK_WIDGET(area));
        const int sc = gtk_widget_get_scale_factor(GTK_WIDGET(area));
        if (A->need_frame)
        {
            frame_all();
            A->need_frame = false;
        }
        A->gl.render(A->ed, A->cams, A->vs, W, H, sc);
        if (!A->shot_file.empty() && ++A->shot_frames >= 3)
            save_shot();
        return TRUE;
    }

    // ---- mouse and keyboard in the views --------------------------------------------------------------------------

    double snap_step(const Camera &c)
    {
        const double base = std::pow(10.0, std::floor(std::log10(14 / c.scale)));
        for (double m : {1.0, 2.0, 5.0, 10.0})
            if (base * m * c.scale >= 14)
                return base * m;
        return base * 10;
    }

    void sketch_click(int view, double x, double y)
    {
        const Camera &c = A->cams[view];
        const Rect r = view_rect(view, gtk_widget_get_allocated_width(A->area), gtk_widget_get_allocated_height(A->area), A->vs.maximised);
        V3 right, up, fwd;
        c.basis(right, up, fwd);
        double u = dot(c.target, right) + (x - r.x - r.w / 2.0) / c.scale;
        double v = dot(c.target, up) + (r.y + r.h / 2.0 - y) / c.scale;
        const double step = snap_step(c) / 2;
        u = std::round(u / step) * step;
        v = std::round(v / step) * step;
        A->ed.sketch.push_back({u, v});
        render();
    }

    void local(int view, double x, double y, double &lx, double &ly, int &w, int &h)
    {
        const Rect r = view_rect(view, gtk_widget_get_allocated_width(A->area), gtk_widget_get_allocated_height(A->area), A->vs.maximised);
        lx = x - r.x;
        ly = y - r.y;
        w = r.w;
        h = r.h;
    }

    gboolean on_press(GtkWidget *widget, GdkEventButton *e, gpointer)
    {
        gtk_widget_grab_focus(widget);
        const int W = gtk_widget_get_allocated_width(widget), H = gtk_widget_get_allocated_height(widget);
        const int v = view_at(e->x, e->y, W, H, A->vs.maximised);
        if (v < 0)
            return TRUE;
        A->vs.active = v;
        A->press_view = v;
        A->press_button = e->button;
        A->press_x = A->last_x = e->x;
        A->press_y = A->last_y = e->y;
        A->moved = false;
        A->pressed = true;
        A->navigating = false;
        A->gizmo_drag = false;
        const bool alt = e->state & GDK_MOD1_MASK;
        if (e->button == 2 || e->button == 3 || (e->button == 1 && alt))
        {
            A->navigating = true;
            A->nav_view = v;
            return TRUE;
        }
        if (e->button != 1)
            return TRUE;
        double lx, ly;
        int w, h;
        local(v, e->x, e->y, lx, ly, w, h);
        if (A->vs.sketching && v < 3)
            return TRUE; // handled on release
        const int axis = A->ed.pick_gizmo(A->cams[v], w, h, lx, ly);
        if (axis >= 0)
        {
            A->ed.begin_drag(A->cams[v], w, h, lx, ly, axis);
            A->gizmo_drag = A->ed.dragging();
        }
        render();
        return TRUE;
    }

    gboolean on_motion(GtkWidget *widget, GdkEventMotion *e, gpointer)
    {
        const int W = gtk_widget_get_allocated_width(widget), H = gtk_widget_get_allocated_height(widget);
        const double dx = e->x - A->last_x, dy = e->y - A->last_y;
        A->last_x = e->x;
        A->last_y = e->y;
        if (A->pressed && !A->moved && std::hypot(e->x - A->press_x, e->y - A->press_y) > 3)
            A->moved = true;
        if (A->pressed && A->navigating)
        {
            Camera &c = A->cams[A->nav_view];
            const bool orbit = !c.ortho() && (A->press_button == 2 || A->press_button == 1);
            if (orbit)
            {
                c.yaw -= dx * 0.4;
                c.pitch = std::clamp(c.pitch + dy * 0.4, -89.0, 89.0);
            }
            else
            {
                V3 r, u, f;
                c.basis(r, u, f);
                const double mpp = c.metres_per_pixel(c.target, 600);
                c.target = c.target - r * (dx * (c.ortho() ? 1 / c.scale : mpp)) + u * (dy * (c.ortho() ? 1 / c.scale : mpp));
            }
            render();
            return TRUE;
        }
        const int v = A->pressed ? A->press_view : view_at(e->x, e->y, W, H, A->vs.maximised);
        if (v < 0)
            return TRUE;
        double lx, ly;
        int w, h;
        local(v, e->x, e->y, lx, ly, w, h);
        A->vs.cursor_x = lx;
        A->vs.cursor_y = ly;
        if (A->gizmo_drag)
        {
            A->ed.drag(A->cams[v], w, h, lx, ly);
            sync_transform();
            render();
            return TRUE;
        }
        if (A->pressed && A->press_button == 1 && A->moved && !A->vs.sketching)
        {
            double px, py;
            int pw, ph;
            local(v, A->press_x, A->press_y, px, py, pw, ph);
            A->vs.box = true;
            A->vs.bx0 = px;
            A->vs.by0 = py;
            A->vs.bx1 = lx;
            A->vs.by1 = ly;
            render();
            return TRUE;
        }
        const int hover = A->ed.pick_gizmo(A->cams[v], w, h, lx, ly);
        if (hover != A->vs.hover_axis || A->vs.sketching)
        {
            A->vs.hover_axis = hover;
            A->vs.active = A->pressed ? A->vs.active : v;
            render();
        }
        return TRUE;
    }

    gboolean on_release(GtkWidget *widget, GdkEventButton *e, gpointer)
    {
        if (!A->pressed)
            return TRUE;
        A->pressed = false;
        const int v = A->press_view;
        double lx, ly;
        int w, h;
        local(v, e->x, e->y, lx, ly, w, h);
        if (A->navigating)
        {
            A->navigating = false;
            if (A->vs.sketching && A->press_button == 3 && !A->moved && !A->ed.sketch.empty())
            {
                A->ed.sketch.pop_back();
                render();
            }
            return TRUE;
        }
        if (A->gizmo_drag)
        {
            A->ed.end_drag();
            A->gizmo_drag = false;
            refresh();
            return TRUE;
        }
        if (e->button != 1)
            return TRUE;
        if (A->vs.sketching && v < 3)
        {
            if (A->cams[v].kind == A->ed.sketch_kind)
                sketch_click(v, e->x, e->y);
            return TRUE;
        }
        const bool add = e->state & GDK_SHIFT_MASK;
        if (A->moved)
        {
            double px, py;
            int pw, ph;
            local(v, A->press_x, A->press_y, px, py, pw, ph);
            A->ed.box_select(A->cams[v], w, h, px, py, lx, ly, add);
        }
        else
            A->ed.click(A->cams[v], w, h, lx, ly, add);
        A->vs.box = false;
        sync_ui();
        render();
        (void)widget;
        return TRUE;
    }

    gboolean on_scroll(GtkWidget *widget, GdkEventScroll *e, gpointer)
    {
        const int v = view_at(e->x, e->y, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget), A->vs.maximised);
        if (v < 0)
            return TRUE;
        A->vs.active = v;
        A->cams[v].zoom(e->direction == GDK_SCROLL_UP ? 1.15 : (e->direction == GDK_SCROLL_DOWN ? 1 / 1.15 : (e->delta_y < 0 ? 1.15 : 1 / 1.15)));
        render();
        return TRUE;
    }

    gboolean on_key(GtkWidget *, GdkEventKey *e, gpointer)
    {
        GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(A->window));
        if (focus && (GTK_IS_EDITABLE(focus) || GTK_IS_TEXT_VIEW(focus)))
            return FALSE;
        const bool ctrl = e->state & GDK_CONTROL_MASK, shift = e->state & GDK_SHIFT_MASK;
        Editor &ed = A->ed;
        const guint k = gdk_keyval_to_lower(e->keyval);
        if (ctrl)
        {
            switch (k)
            {
            case GDK_KEY_z: shift ? do_redo() : do_undo(); return TRUE;
            case GDK_KEY_y: do_redo(); return TRUE;
            case GDK_KEY_d: ed.duplicate_selection(); refresh(); return TRUE;
            case GDK_KEY_a: ed.select_all(); sync_ui(); render(); return TRUE;
            case GDK_KEY_s: shift ? act_save_ship(false) : act_save_kmodel(false); return TRUE;
            case GDK_KEY_o: act_open(); return TRUE;
            case GDK_KEY_n: act_new(); return TRUE;
            case GDK_KEY_w: act_close(); return TRUE;
            default: return FALSE;
            }
        }
        switch (k)
        {
        case GDK_KEY_q: set_tool(Tool::Select); return TRUE;
        case GDK_KEY_w: set_tool(Tool::Move); return TRUE;
        case GDK_KEY_e: set_tool(Tool::Rotate); return TRUE;
        case GDK_KEY_r: set_tool(Tool::Scale); return TRUE;
        case GDK_KEY_1: set_mode(Mode::Object); return TRUE;
        case GDK_KEY_2: set_mode(Mode::Vertex); return TRUE;
        case GDK_KEY_3: set_mode(Mode::Edge); return TRUE;
        case GDK_KEY_4: set_mode(Mode::Face); return TRUE;
        case GDK_KEY_Delete:
        case GDK_KEY_x: ed.delete_selection(); clamp_selection(); refresh(); return TRUE;
        case GDK_KEY_space: toggle_max(); return TRUE;
        case GDK_KEY_f: A->need_frame = true; render(); return TRUE;
        case GDK_KEY_z: toggle_wire(); return TRUE;
        case GDK_KEY_g: A->vs.grid = !A->vs.grid; render(); return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter: finish_sketch(false); return TRUE;
        case GDK_KEY_l: finish_sketch(true); return TRUE;
        case GDK_KEY_BackSpace:
            if (A->vs.sketching && !ed.sketch.empty())
                ed.sketch.pop_back();
            render();
            return TRUE;
        case GDK_KEY_Escape:
            if (A->vs.sketching)
                end_sketch();
            else
            {
                ed.sel.clear();
                sync_ui();
                render();
            }
            return TRUE;
        default: return FALSE;
        }
    }

    // ---- property panels ------------------------------------------------------------------------------------------

    void on_transform_changed(GtkWidget *, gpointer)
    {
        if (A->updating)
            return;
        Xform x;
        for (int i = 0; i < 3; ++i)
        {
            x.pos[i] = val(A->sp_pos[i]);
            x.rot[i] = val(A->sp_rot[i]);
            x.scale[i] = val(A->sp_scl[i]);
        }
        A->ed.doc.checkpoint();
        if (Marker *m = edit_marker())
        {
            m->pos = x.pos;
            m->rot = x.rot;
            m->size = x.scale;
        }
        else if (Object *o = A->ed.active_object())
            o->xf = x;
        else
            return;
        A->ed.doc.dirty = true;
        render();
    }

    void on_object_props(GtkWidget *, gpointer)
    {
        if (A->updating)
            return;
        Object *o = A->ed.active_object();
        if (!o || edit_marker())
            return;
        A->ed.doc.checkpoint();
        o->name = gtk_entry_get_text(GTK_ENTRY(A->obj_name));
        o->visible = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(A->obj_visible));
        o->smooth = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(A->obj_smooth));
        A->ed.changed();
        rebuild_tree();
        sync_ui();
        render();
    }

    void on_marker_name(GtkWidget *, gpointer)
    {
        if (A->updating)
            return;
        if (Marker *m = edit_marker())
        {
            A->ed.doc.checkpoint();
            m->name = gtk_entry_get_text(GTK_ENTRY(A->mk_name));
            A->ed.doc.dirty = true;
            rebuild_tree();
            sync_ui();
        }
    }

    void on_marker_apply(GtkWidget *, gpointer)
    {
        Marker *m = edit_marker();
        if (!m)
            return;
        GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(A->mk_props));
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(b, &s, &e);
        char *text = gtk_text_buffer_get_text(b, &s, &e, FALSE);
        json j = json::parse(text, nullptr, false);
        g_free(text);
        if (j.is_discarded() || !j.is_object())
        {
            say("Properties must be a JSON object.");
            return;
        }
        A->ed.doc.checkpoint();
        m->props = j;
        A->ed.doc.dirty = true;
        say("Properties applied.");
        sync_ui();
    }

    void on_marker_place(GtkWidget *, gpointer)
    {
        if (Marker *m = edit_marker())
        {
            A->ed.doc.checkpoint();
            m->placed = true;
            A->ed.doc.dirty = true;
            refresh();
        }
    }

    void on_tree_selection(GtkTreeSelection *ts, gpointer)
    {
        if (A->updating)
            return;
        Selection &s = A->ed.sel;
        s.objects.clear();
        s.markers.clear();
        GList *rows = gtk_tree_selection_get_selected_rows(ts, nullptr);
        for (GList *l = rows; l; l = l->next)
        {
            GtkTreeIter it;
            gtk_tree_model_get_iter(GTK_TREE_MODEL(A->store), &it, static_cast<GtkTreePath *>(l->data));
            int type, index;
            gtk_tree_model_get(GTK_TREE_MODEL(A->store), &it, 1, &type, 2, &index, -1);
            (type == 0 ? s.objects : s.markers).insert(index);
        }
        g_list_free_full(rows, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));
        s.active = s.objects.empty() ? -1 : *s.objects.begin();
        s.clear_elements();
        sync_ui(false);
        render();
    }

    void on_material_changed(GtkWidget *, gpointer)
    {
        if (A->updating)
            return;
        Material &m = A->ed.scene().materials[current_material()];
        A->updating = true;
        gtk_entry_set_text(GTK_ENTRY(A->mat_name), m.name.c_str());
        GdkRGBA c{m.colour.x, m.colour.y, m.colour.z, 1};
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(A->mat_colour), &c);
        gtk_entry_set_text(GTK_ENTRY(A->mat_tex), m.texture.c_str());
        A->updating = false;
    }

    void on_material_edit(GtkWidget *, gpointer)
    {
        if (A->updating)
            return;
        Material &m = A->ed.scene().materials[current_material()];
        A->ed.doc.checkpoint();
        std::string name = slug(gtk_entry_get_text(GTK_ENTRY(A->mat_name)));
        if (!name.empty() && name != m.name)
            m.name = name;
        GdkRGBA c;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(A->mat_colour), &c);
        m.colour = {c.red, c.green, c.blue};
        m.texture = gtk_entry_get_text(GTK_ENTRY(A->mat_tex));
        A->ed.changed();
        A->updating = true;
        const int keep = current_material();
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(A->mat_combo));
        for (const Material &mm : A->ed.scene().materials)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(A->mat_combo), mm.name.c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(A->mat_combo), keep);
        A->updating = false;
        sync_ui();
        render();
    }

    // ---- UI construction ------------------------------------------------------------------------------------------

    using Cb = void (*)(GtkWidget *, gpointer);

    GtkWidget *button(const char *label, Cb cb, const char *tip = nullptr)
    {
        GtkWidget *b = gtk_button_new_with_label(label);
        g_signal_connect(b, "clicked", G_CALLBACK(cb), nullptr);
        if (tip)
            gtk_widget_set_tooltip_text(b, tip);
        return b;
    }

    GtkWidget *menu_item(GtkWidget *menu, const char *label, Cb cb)
    {
        GtkWidget *mi = gtk_menu_item_new_with_label(label);
        g_signal_connect(mi, "activate", G_CALLBACK(cb), nullptr);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
        return mi;
    }

    GtkWidget *submenu(GtkWidget *bar, const char *label)
    {
        GtkWidget *mi = gtk_menu_item_new_with_label(label), *menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(bar), mi);
        return menu;
    }

    GtkWidget *row(const char *label, GtkWidget *w0, GtkWidget *w1 = nullptr, GtkWidget *w2 = nullptr)
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *l = gtk_label_new(label);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_widget_set_size_request(l, 60, -1);
        gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 0);
        for (GtkWidget *w : {w0, w1, w2})
            if (w)
                gtk_box_pack_start(GTK_BOX(box), w, TRUE, TRUE, 0);
        return box;
    }

    GtkWidget *section(const char *title, GtkWidget *content, bool open = true)
    {
        GtkWidget *e = gtk_expander_new(title);
        gtk_expander_set_expanded(GTK_EXPANDER(e), open);
        gtk_container_add(GTK_CONTAINER(e), content);
        return e;
    }

    GtkWidget *vbox() { return gtk_box_new(GTK_ORIENTATION_VERTICAL, 4); }

    GtkWidget *two_buttons(GtkWidget *a, GtkWidget *b, GtkWidget *c = nullptr)
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        for (GtkWidget *w : {a, b, c})
            if (w)
                gtk_box_pack_start(GTK_BOX(box), w, TRUE, TRUE, 0);
        return box;
    }

    gboolean on_child_position(GtkOverlay *, GtkWidget *child, GdkRectangle *alloc, gpointer)
    {
        const int v = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "view"));
        const Rect r = view_rect(v, gtk_widget_get_allocated_width(A->area), gtk_widget_get_allocated_height(A->area), A->vs.maximised);
        GtkRequisition min, nat;
        gtk_widget_get_preferred_size(child, &min, &nat);
        alloc->x = r.x + 6;
        alloc->y = r.y + 4;
        alloc->width = std::min(nat.width, std::max(r.w - 12, 1));
        alloc->height = nat.height;
        gtk_widget_set_visible(child, r.w > 0);
        return TRUE;
    }

    void build_ui()
    {
        A->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_default_size(GTK_WINDOW(A->window), A->shot_file.empty() ? 1400 : A->shot_w + 330, A->shot_file.empty() ? 860 : A->shot_h + 90);
        g_signal_connect(A->window, "delete-event", G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer) -> gboolean { return !discard_ok(); }), nullptr);
        g_signal_connect(A->window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer) { gtk_main_quit(); }), nullptr);
        g_signal_connect(A->window, "key-press-event", G_CALLBACK(on_key), nullptr);

        GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(A->window), root);

        // ---- menus
        GtkWidget *bar = gtk_menu_bar_new();
        GtkWidget *file = submenu(bar, "File");
        menu_item(file, "New", +[](GtkWidget *, gpointer) { act_new(); });
        menu_item(file, "Close file  (Ctrl+W)", +[](GtkWidget *, gpointer) { act_close(); });
        menu_item(file, "Open ship.json...  (Ctrl+O)", +[](GtkWidget *, gpointer) { act_open(); });
        menu_item(file, "Open scene (.kmodel)...", +[](GtkWidget *, gpointer) { act_open_kmodel(); });
        menu_item(file, "Import OBJ...", +[](GtkWidget *, gpointer) { act_import_obj(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(file), gtk_separator_menu_item_new());
        menu_item(file, "Save scene (.kmodel)  (Ctrl+S)", +[](GtkWidget *, gpointer) { act_save_kmodel(false); });
        menu_item(file, "Save scene as...", +[](GtkWidget *, gpointer) { act_save_kmodel(true); });
        menu_item(file, "Save to ship.json  (Ctrl+Shift+S)", +[](GtkWidget *, gpointer) { act_save_ship(false); });
        menu_item(file, "Save ship.json as...", +[](GtkWidget *, gpointer) { act_save_ship(true); });
        menu_item(file, "Export OBJ...", +[](GtkWidget *, gpointer) { act_export_obj(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(file), gtk_separator_menu_item_new());
        menu_item(file, "Quit", +[](GtkWidget *, gpointer) { if (discard_ok()) gtk_main_quit(); });

        GtkWidget *edit = submenu(bar, "Edit");
        menu_item(edit, "Undo  (Ctrl+Z)", +[](GtkWidget *, gpointer) { do_undo(); });
        menu_item(edit, "Redo  (Ctrl+Y)", +[](GtkWidget *, gpointer) { do_redo(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(edit), gtk_separator_menu_item_new());
        menu_item(edit, "Duplicate  (Ctrl+D)", +[](GtkWidget *, gpointer) { A->ed.duplicate_selection(); refresh(); });
        menu_item(edit, "Delete  (Del)", +[](GtkWidget *, gpointer) { A->ed.delete_selection(); clamp_selection(); refresh(); });
        menu_item(edit, "Select all  (Ctrl+A)", +[](GtkWidget *, gpointer) { A->ed.select_all(); sync_ui(); render(); });

        GtkWidget *add = submenu(bar, "Add");
        menu_item(add, "Box...", +[](GtkWidget *, gpointer) { add_shape(ShapeKind::Box); });
        menu_item(add, "Cylinder...", +[](GtkWidget *, gpointer) { add_shape(ShapeKind::Cylinder); });
        menu_item(add, "Cone / frustum...", +[](GtkWidget *, gpointer) { add_shape(ShapeKind::Cone); });
        menu_item(add, "Sphere...", +[](GtkWidget *, gpointer) { add_shape(ShapeKind::Sphere); });
        menu_item(add, "Torus...", +[](GtkWidget *, gpointer) { add_shape(ShapeKind::Torus); });
        menu_item(add, "Plane...", +[](GtkWidget *, gpointer) { add_shape(ShapeKind::Plane); });
        gtk_menu_shell_append(GTK_MENU_SHELL(add), gtk_separator_menu_item_new());
        menu_item(add, "Sketch (extrude / lathe) in the active view", +[](GtkWidget *, gpointer) { begin_sketch(); });
        GtkWidget *eq = gtk_menu_item_new_with_label("Equipment"), *eq_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(eq), eq_menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(add), eq);
        for (const KindInfo &k : catalogue())
        {
            GtkWidget *mi = gtk_menu_item_new_with_label(k.label.c_str());
            g_object_set_data_full(G_OBJECT(mi), "kind", g_strdup(k.key.c_str()), g_free);
            g_signal_connect(mi, "activate", G_CALLBACK(+[](GtkWidget *w, gpointer) {
                               A->ed.add_marker(static_cast<const char *>(g_object_get_data(G_OBJECT(w), "kind")), {0, 0, 0});
                               refresh();
                           }), nullptr);
            gtk_menu_shell_append(GTK_MENU_SHELL(eq_menu), mi);
        }

        GtkWidget *view = submenu(bar, "View");
        menu_item(view, "Frame everything  (F)", +[](GtkWidget *, gpointer) { A->need_frame = true; render(); });
        menu_item(view, "Maximise / restore the active view  (Space)", +[](GtkWidget *, gpointer) { toggle_max(); });
        menu_item(view, "Wireframe / solid in the active view  (Z)", +[](GtkWidget *, gpointer) { toggle_wire(); });
        menu_item(view, "Grid on / off  (G)", +[](GtkWidget *, gpointer) { A->vs.grid = !A->vs.grid; render(); });
        menu_item(view, "Mesh edges on / off", +[](GtkWidget *, gpointer) { A->vs.edges = !A->vs.edges; render(); });
        gtk_box_pack_start(GTK_BOX(root), bar, FALSE, FALSE, 0);

        // ---- mode and tool buttons
        GtkWidget *tb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_container_set_border_width(GTK_CONTAINER(tb), 3);
        const char *mode_names[4] = {"Object (1)", "Vertex (2)", "Edge (3)", "Face (4)"};
        const char *tool_names[4] = {"Select (Q)", "Move (W)", "Rotate (E)", "Scale (R)"};
        for (int i = 0; i < 4; ++i)
        {
            A->mode_btn[i] = gtk_toggle_button_new_with_label(mode_names[i]);
            g_object_set_data(G_OBJECT(A->mode_btn[i]), "i", GINT_TO_POINTER(i));
            g_signal_connect(A->mode_btn[i], "clicked", G_CALLBACK(+[](GtkWidget *w, gpointer) {
                               if (!A->updating)
                                   set_mode(static_cast<Mode>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "i"))));
                           }), nullptr);
            gtk_box_pack_start(GTK_BOX(tb), A->mode_btn[i], FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(tb), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 6);
        for (int i = 0; i < 4; ++i)
        {
            A->tool_btn[i] = gtk_toggle_button_new_with_label(tool_names[i]);
            g_object_set_data(G_OBJECT(A->tool_btn[i]), "i", GINT_TO_POINTER(i));
            g_signal_connect(A->tool_btn[i], "clicked", G_CALLBACK(+[](GtkWidget *w, gpointer) {
                               if (!A->updating)
                                   set_tool(static_cast<Tool>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "i"))));
                           }), nullptr);
            gtk_box_pack_start(GTK_BOX(tb), A->tool_btn[i], FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(tb), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 6);
        gtk_box_pack_start(GTK_BOX(tb), button("Wire/Solid (Z)", +[](GtkWidget *, gpointer) { toggle_wire(); }), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tb), button("Maximise (Space)", +[](GtkWidget *, gpointer) { toggle_max(); }), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tb), button("Frame (F)", +[](GtkWidget *, gpointer) { A->need_frame = true; render(); }), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(root), tb, FALSE, FALSE, 0);

        // ---- left: scene tree
        GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(root), paned, TRUE, TRUE, 0);
        A->store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
        A->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(A->store));
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(A->tree), FALSE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(A->tree), gtk_tree_view_column_new_with_attributes("Scene", gtk_cell_renderer_text_new(), "text", 0, nullptr));
        GtkTreeSelection *ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(A->tree));
        gtk_tree_selection_set_mode(ts, GTK_SELECTION_MULTIPLE);
        g_signal_connect(ts, "changed", G_CALLBACK(on_tree_selection), nullptr);
        GtkWidget *tree_scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(tree_scroll), A->tree);
        gtk_widget_set_size_request(tree_scroll, 190, -1);
        gtk_paned_pack1(GTK_PANED(paned), tree_scroll, FALSE, FALSE);

        // ---- centre: the GL area with view-name labels over it
        GtkWidget *overlay = gtk_overlay_new();
        A->area = gtk_gl_area_new();
        gtk_gl_area_set_required_version(GTK_GL_AREA(A->area), 3, 2);
        gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(A->area), TRUE);
        gtk_widget_set_can_focus(A->area, TRUE);
        gtk_widget_add_events(A->area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK);
        g_signal_connect(A->area, "realize", G_CALLBACK(on_realize), nullptr);
        g_signal_connect(A->area, "unrealize", G_CALLBACK(on_unrealize), nullptr);
        g_signal_connect(A->area, "render", G_CALLBACK(on_render), nullptr);
        g_signal_connect(A->area, "button-press-event", G_CALLBACK(on_press), nullptr);
        g_signal_connect(A->area, "button-release-event", G_CALLBACK(on_release), nullptr);
        g_signal_connect(A->area, "motion-notify-event", G_CALLBACK(on_motion), nullptr);
        g_signal_connect(A->area, "scroll-event", G_CALLBACK(on_scroll), nullptr);
        gtk_container_add(GTK_CONTAINER(overlay), A->area);
        for (int i = 0; i < 4; ++i)
        {
            A->labels[i] = gtk_label_new(kViewNames[i]);
            gtk_widget_set_halign(A->labels[i], GTK_ALIGN_START);
            gtk_widget_set_valign(A->labels[i], GTK_ALIGN_START);
            g_object_set_data(G_OBJECT(A->labels[i]), "view", GINT_TO_POINTER(i));
            gtk_overlay_add_overlay(GTK_OVERLAY(overlay), A->labels[i]);
            gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), A->labels[i], TRUE);
        }
        g_signal_connect(overlay, "get-child-position", G_CALLBACK(on_child_position), nullptr);

        // ---- right: properties
        GtkWidget *side = vbox();
        gtk_container_set_border_width(GTK_CONTAINER(side), 6);

        GtkWidget *tr = vbox();
        const char *axes[3] = {"X", "Y", "Z"};
        const char *names[3] = {"Position", "Rotation", "Scale"};
        for (int k = 0; k < 3; ++k)
        {
            GtkWidget **arr = k == 0 ? A->sp_pos : (k == 1 ? A->sp_rot : A->sp_scl);
            for (int i = 0; i < 3; ++i)
            {
                arr[i] = spin(k == 1 ? -360 : (k == 2 ? 0.001 : -10000), k == 1 ? 360 : 10000, k == 1 ? 1 : 0.1, 3, k == 2 ? 1 : 0);
                gtk_widget_set_tooltip_text(arr[i], axes[i]);
                g_signal_connect(arr[i], "value-changed", G_CALLBACK(on_transform_changed), nullptr);
            }
            gtk_box_pack_start(GTK_BOX(tr), row(names[k], arr[0], arr[1], arr[2]), FALSE, FALSE, 0);
        }
        A->obj_name = gtk_entry_new();
        g_signal_connect(A->obj_name, "changed", G_CALLBACK(on_object_props), nullptr);
        A->obj_visible = gtk_check_button_new_with_label("Visible");
        A->obj_smooth = gtk_check_button_new_with_label("Smooth shading");
        g_signal_connect(A->obj_visible, "toggled", G_CALLBACK(on_object_props), nullptr);
        g_signal_connect(A->obj_smooth, "toggled", G_CALLBACK(on_object_props), nullptr);
        gtk_box_pack_start(GTK_BOX(tr), row("Name", A->obj_name), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tr), two_buttons(A->obj_visible, A->obj_smooth), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(side), section("Transform (object, or equipment marker)", tr), FALSE, FALSE, 0);

        // marker properties
        A->mk_box = vbox();
        A->mk_kind = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(A->mk_kind), 0);
        A->mk_name = gtk_entry_new();
        g_signal_connect(A->mk_name, "changed", G_CALLBACK(on_marker_name), nullptr);
        A->mk_props = gtk_text_view_new();
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(A->mk_props), TRUE);
        GtkWidget *ps = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(ps, -1, 150);
        gtk_container_add(GTK_CONTAINER(ps), A->mk_props);
        A->mk_place = button("Place in the views", +[](GtkWidget *w, gpointer d) { on_marker_place(w, d); });
        gtk_box_pack_start(GTK_BOX(A->mk_box), row("Kind", A->mk_kind), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(A->mk_box), row("Name", A->mk_name), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(A->mk_box), ps, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(A->mk_box), button("Apply properties (JSON)", +[](GtkWidget *w, gpointer d) { on_marker_apply(w, d); }), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(A->mk_box), A->mk_place, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(side), section("Equipment properties", A->mk_box), FALSE, FALSE, 0);

        // modelling tools
        GtkWidget *mt = vbox();
        A->sp_extrude = spin(-1000, 1000, 0.1, 2, 0.5);
        A->sp_inset = spin(0.001, 1000, 0.05, 3, 0.2);
        A->sp_weld = spin(0.0001, 10, 0.001, 4, 0.001);
        GtkWidget *b_ext = button("Extrude faces", +[](GtkWidget *, gpointer) { A->ed.extrude(val(A->sp_extrude)); refresh(); }, "Face mode: push the selected faces out by this distance");
        GtkWidget *b_ins = button("Inset faces", +[](GtkWidget *, gpointer) { A->ed.inset(val(A->sp_inset)); refresh(); });
        gtk_box_pack_start(GTK_BOX(mt), row("Extrude", A->sp_extrude, b_ext), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mt), row("Inset", A->sp_inset, b_ins), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mt), two_buttons(button("Delete faces", +[](GtkWidget *, gpointer) { A->ed.delete_selected_faces(); refresh(); }),
                                                    button("Flip normals", +[](GtkWidget *, gpointer) { A->ed.flip_selected_faces(); refresh(); })), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mt), two_buttons(button("Split edge", +[](GtkWidget *, gpointer) { A->ed.split_selected_edges(); refresh(); }, "Edge mode: add a vertex in the middle of each selected edge"),
                                                    button("Weld", +[](GtkWidget *, gpointer) { A->ed.weld_selected(val(A->sp_weld)); refresh(); }, "Merge vertices closer than the distance on the right"), A->sp_weld), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mt), two_buttons(button("Mirror X", +[](GtkWidget *, gpointer) { A->ed.mirror_object(0); refresh(); }),
                                                    button("Mirror Y", +[](GtkWidget *, gpointer) { A->ed.mirror_object(1); refresh(); }),
                                                    button("Mirror Z", +[](GtkWidget *, gpointer) { A->ed.mirror_object(2); refresh(); })), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mt), two_buttons(button("Union", +[](GtkWidget *, gpointer) { A->ed.boolean(BoolOp::Union); refresh(); }, "Select two or more objects; the active one (the one you picked first) keeps the result"),
                                                    button("Subtract", +[](GtkWidget *, gpointer) { A->ed.boolean(BoolOp::Subtract); refresh(); }, "Cuts the other selected objects out of the active one"),
                                                    button("Intersect", +[](GtkWidget *, gpointer) { A->ed.boolean(BoolOp::Intersect); refresh(); })), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mt), two_buttons(button("Apply rotation+scale", +[](GtkWidget *, gpointer) { A->ed.apply_transform(); refresh(); }),
                                                    button("Origin to centre", +[](GtkWidget *, gpointer) { A->ed.set_origin_to_centre(); refresh(); })), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mt), two_buttons(button("Sketch...", +[](GtkWidget *, gpointer) { begin_sketch(); }, "Draw an outline in the active Top/Front/Side view, then extrude or lathe it"),
                                                    button("Extrude sketch", +[](GtkWidget *, gpointer) { finish_sketch(false); }),
                                                    button("Lathe sketch", +[](GtkWidget *, gpointer) { finish_sketch(true); })), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(side), section("Modelling", mt), FALSE, FALSE, 0);

        // materials and UVs
        GtkWidget *mm = vbox();
        A->mat_combo = gtk_combo_box_text_new();
        g_signal_connect(A->mat_combo, "changed", G_CALLBACK(on_material_changed), nullptr);
        A->mat_name = gtk_entry_new();
        gtk_widget_set_tooltip_text(A->mat_name, "The material's name is the part name Kosmos animates (the OBJ usemtl name)");
        A->mat_colour = gtk_color_button_new();
        A->mat_tex = gtk_entry_new();
        gtk_widget_set_tooltip_text(A->mat_tex, "Texture image path (relative to the ship folder, or absolute); copied next to the OBJ on export");
        g_signal_connect(A->mat_name, "activate", G_CALLBACK(on_material_edit), nullptr);
        g_signal_connect(A->mat_tex, "activate", G_CALLBACK(on_material_edit), nullptr);
        g_signal_connect(A->mat_colour, "color-set", G_CALLBACK(on_material_edit), nullptr);
        gtk_box_pack_start(GTK_BOX(mm), row("Material", A->mat_combo), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Name", A->mat_name), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Colour", A->mat_colour), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Texture", A->mat_tex), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), two_buttons(button("New material", +[](GtkWidget *, gpointer) {
                                                          const int n = A->ed.new_material("part");
                                                          rebuild_materials();
                                                          gtk_combo_box_set_active(GTK_COMBO_BOX(A->mat_combo), n);
                                                      }),
                                                    button("Assign to selection", +[](GtkWidget *, gpointer) { A->ed.assign_material(current_material()); refresh(); },
                                                           "Faces in Face mode, otherwise every face of the selected objects")), FALSE, FALSE, 0);
        A->cb_uv = gtk_combo_box_text_new();
        for (const char *n : {"Planar", "Box", "Cylindrical", "Spherical"})
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(A->cb_uv), n);
        gtk_combo_box_set_active(GTK_COMBO_BOX(A->cb_uv), 1);
        A->cb_uv_axis = gtk_combo_box_text_new();
        for (const char *n : {"X", "Y", "Z"})
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(A->cb_uv_axis), n);
        gtk_combo_box_set_active(GTK_COMBO_BOX(A->cb_uv_axis), 2);
        gtk_box_pack_start(GTK_BOX(mm), row("UV", A->cb_uv, A->cb_uv_axis, button("Project", +[](GtkWidget *, gpointer) {
                                                A->ed.project_uvs(static_cast<UvMode>(gtk_combo_box_get_active(GTK_COMBO_BOX(A->cb_uv))), gtk_combo_box_get_active(GTK_COMBO_BOX(A->cb_uv_axis)));
                                                refresh();
                                            })), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(side), section("Materials and UVs", mm), FALSE, FALSE, 0);

        GtkWidget *side_scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(side_scroll), side);
        gtk_widget_set_size_request(side_scroll, 330, -1);
        GtkWidget *outer = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_pack1(GTK_PANED(outer), overlay, TRUE, FALSE);
        gtk_paned_pack2(GTK_PANED(outer), side_scroll, FALSE, FALSE);
        gtk_paned_pack2(GTK_PANED(paned), outer, TRUE, FALSE);

        A->status = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(A->status), 0);
        gtk_label_set_ellipsize(GTK_LABEL(A->status), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(root), A->status, FALSE, FALSE, 3);

        GtkCssProvider *css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css, "label { }", -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
    }
} // namespace

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    App app;
    A = &app;
    std::string open_file;
    const char *mode_arg = nullptr;
    int select_face = -1;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--shot" && more)
            app.shot_file = argv[++i];
        else if (a == "--size" && more)
            std::sscanf(argv[++i], "%dx%d", &app.shot_w, &app.shot_h);
        else if (a == "--mode" && more)
            mode_arg = argv[++i];
        else if (a == "--select-face" && more)
            select_face = std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h")
        {
            std::puts("kosmoscad [ship.json | scene.kmodel] [--shot FILE [--size WxH]]");
            return 0;
        }
        else if (a[0] != '-')
            open_file = a;
    }
    app.cams[0].kind = Camera::Top;
    app.cams[1].kind = Camera::Front;
    app.cams[2].kind = Camera::Side;
    app.cams[3].kind = Camera::Persp;
    for (int i = 0; i < 3; ++i)
        app.cams[i].wire = true;

    build_ui();
    gtk_widget_show_all(app.window);
    gtk_widget_set_visible(app.mk_box, FALSE);
    if (!open_file.empty())
        load_any(open_file);
    else
        refresh();
    if (mode_arg)
        app.ed.mode = std::string(mode_arg) == "vertex" ? Mode::Vertex : (std::string(mode_arg) == "edge" ? Mode::Edge : (std::string(mode_arg) == "face" ? Mode::Face : Mode::Object));
    if (select_face >= 0 && !app.ed.scene().objects.empty())
    {
        app.ed.sel.objects = {0};
        app.ed.sel.active = 0;
        app.ed.sel.faces = {select_face};
    }
    else if (!app.shot_file.empty() && !app.ed.scene().objects.empty())
    {
        app.ed.sel.objects = {0};
        app.ed.sel.active = 0;
    }
    sync_ui();
    if (!app.shot_file.empty())
        g_timeout_add(100, +[](gpointer) -> gboolean { render(); return A->shot_frames < 3; }, nullptr);
    gtk_main();
    A = nullptr;
    return 0;
}
