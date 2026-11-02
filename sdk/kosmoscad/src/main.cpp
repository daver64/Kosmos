// KosmosCAD: a small 3D modeller for Kosmos ships (four views, mesh editing, equipment markers). GTK 3 for the panels, OpenGL for every view.
//   kosmoscad [ship.json | scene.kmodel] [--shot FILE [--size WxH] [--select-face N] [--mode object|vertex|edge|face] [--uv] [--textured]]
// --shot draws the views to a PNG and exits.
#include "editor.h"
#include "gl_view.h"
#include "image.h"
#include "mesh_io.h"
#include "ship_io.h"

#include <epoxy/gl.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace cad;

namespace
{
    const char *kViewNames[5] = {"Top", "Front", "Side", "Perspective", "UV editor"};

    struct App
    {
        Editor ed;
        GlView gl;
        Camera cams[4];
        ViewState vs;
        bool gl_ok = false, updating = false, need_frame = true;

        GtkWidget *window = nullptr, *area = nullptr, *status = nullptr, *tree = nullptr, *labels[5] = {};
        GtkListStore *store = nullptr;
        GtkWidget *sp_pos[3] = {}, *sp_rot[3] = {}, *sp_scl[3] = {};
        GtkWidget *obj_name = nullptr, *obj_visible = nullptr, *obj_smooth = nullptr;
        GtkWidget *mk_box = nullptr, *mk_kind = nullptr, *mk_name = nullptr, *mk_props = nullptr, *mk_place = nullptr;
        GtkWidget *mat_combo = nullptr, *mat_name = nullptr, *mat_colour = nullptr, *mat_tex = nullptr, *mat_normal = nullptr, *mat_spec = nullptr, *mat_metal = nullptr, *mat_rough = nullptr, *mat_emit = nullptr;
        GtkWidget *sp_extrude = nullptr, *sp_inset = nullptr, *sp_weld = nullptr;
        GtkWidget *cb_uv = nullptr, *cb_uv_axis = nullptr;
        GtkWidget *mode_btn[4] = {}, *tool_btn[4] = {};

        int press_view = -1, press_button = 0, nav_view = -1;
        double press_x = 0, press_y = 0, last_x = 0, last_y = 0;
        bool pressed = false, moved = false, gizmo_drag = false, navigating = false;
        std::string backed_up;

        // UV editor
        Editor::UvTool uv_tool = Editor::UvTool::Move;
        bool uv_drag = false, press_ctrl = false, drop_tex = false;
        V2 uv_pivot;
        GtkWidget *uv_tool_btn[3] = {}, *uv_btn = nullptr, *uv_checker = nullptr, *uv_num[4] = {};
        bool render_pending = false;
        GtkWidget *render_win = nullptr, *render_img = nullptr;
        GdkPixbuf *render_pb = nullptr;
        fs::path edit_mesh;                    // set when Kosmos opened a part here: saving writes it back to this file
        GtkWidget *scene_win = nullptr, *props_win = nullptr, *scene_btn = nullptr, *props_btn = nullptr;

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
            std::string rest = pattern; // "*.obj;*.glb" adds each pattern to the one filter
            for (size_t at = 0; at <= rest.size();)
            {
                const size_t semi = rest.find(';', at);
                gtk_file_filter_add_pattern(f, rest.substr(at, semi == std::string::npos ? std::string::npos : semi - at).c_str());
                if (semi == std::string::npos)
                    break;
                at = semi + 1;
            }
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
        gtk_window_set_title(GTK_WINDOW(A->window), (std::string("KosmosCAD - ") + (!A->edit_mesh.empty() ? A->edit_mesh.filename().string() + " (editing for Kosmos)" : ed.doc.file.empty() ? (ed.scene().ship_file.empty() ? "untitled" : fs::path(ed.scene().ship_file).parent_path().filename().string()) : ed.doc.file.filename().string()) + (ed.doc.dirty ? " *" : "")).c_str());
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
        else if (is_mesh_file(p))
            ok = import_mesh_file(p, s, err);
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
        A->edit_mesh.clear();
        if (p.extension() == ".kmodel")
            A->ed.doc.file = p;
        A->ed.sel.clear();
        A->ed.uv_sel.clear();
        A->ed.mesh_gen++;
        A->drop_tex = true;
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
        A->edit_mesh.clear();
        A->ed.sel.clear();
        A->ed.mesh_gen++;
        refresh();
    }
    void act_close()
    {
        if (!discard_ok())
            return;
        A->ed.doc = Document();
        A->edit_mesh.clear();
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
        const std::string f = choose(GTK_FILE_CHOOSER_ACTION_OPEN, "Open ship.json, scene or mesh", "*.json;*.kmodel;*.obj;*.msh;*.glb;*.gltf");
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
    // "Send to Kosmos": writes the scene back to the mesh file Kosmos opened here; Kosmos reloads the part when the file changes.
    void act_send()
    {
        if (A->edit_mesh.empty())
        {
            say("Not editing a Kosmos part (Kosmos starts KosmosCAD with --edit-mesh FILE from its part editor)");
            return;
        }
        std::string err;
        A->ed.scene().base_dir = A->edit_mesh.parent_path().string();
        if (write_mesh_file(A->ed.scene(), A->edit_mesh, err))
        {
            A->ed.doc.dirty = false;
            sync_ui();
            say("Sent " + A->edit_mesh.filename().string() + " to Kosmos");
        }
        else
            say("Not sent: " + err);
    }
    std::string texture_base();

    void act_save_kmodel(bool as)
    {
        if (!as && !A->edit_mesh.empty())
        {
            act_send();
            return;
        }
        Editor &ed = A->ed;
        const std::string old_base = texture_base();
        if (as || ed.doc.file.empty())
        {
            const std::string f = choose(GTK_FILE_CHOOSER_ACTION_SAVE, "Save scene", "*.kmodel", "model.kmodel");
            if (f.empty())
                return;
            ed.doc.file = f;
        }
        // Resolve images against where the scene was, then keep them with the file: relative paths, outside images copied beside it.
        ed.scene().base_dir = old_base;
        if (const int n = localise_textures(ed.scene(), texture_base()))
            say("Copied " + std::to_string(n) + " texture file(s) into " + texture_base() + "/textures");
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
        if (!as && !A->edit_mesh.empty())
        {
            act_send();
            return;
        }
        Editor &ed = A->ed;
        std::string path = ed.scene().ship_file;
        if (as || path.empty())
        {
            path = choose(GTK_FILE_CHOOSER_ACTION_SAVE, "Save ship.json (the folder's models/ gets the new meshes)", "*.json", "ship.json");
            if (path.empty())
                return;
        }
        ed.scene().base_dir = texture_base();
        localise_textures(ed.scene(), fs::path(path).parent_path());
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
    void act_import_mesh()
    {
        const std::string f = choose(GTK_FILE_CHOOSER_ACTION_OPEN, "Import a mesh (OBJ, Orbiter MSH, glTF/GLB)", "*.obj;*.msh;*.glb;*.gltf");
        if (f.empty())
            return;
        std::string err;
        A->ed.doc.checkpoint();
        if (import_mesh_file(f, A->ed.scene(), err))
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
        A->ed.scene().base_dir = texture_base();
        if (export_obj(A->ed.scene(), dir, name, err))
            say("Exported " + dir + "/" + name + ".obj");
        else
            say("Export failed: " + err);
    }

    void act_export_glb()
    {
        std::string f = choose(GTK_FILE_CHOOSER_ACTION_SAVE, "Export GLB", "*.glb", "model.glb");
        if (f.empty())
            return;
        if (f.size() < 4 || f.substr(f.size() - 4) != ".glb")
            f += ".glb";
        std::string err;
        A->ed.scene().base_dir = texture_base();
        if (write_mesh_file(A->ed.scene(), f, err))
            say("Exported " + f);
        else
            say("Export failed: " + err);
    }

    // ---- commands -------------------------------------------------------------------------------------------------

    void add_shape(ShapeKind kind)
    {
        std::vector<Field> f;
        switch (kind)
        {
        case ShapeKind::Box: f = std::vector<Field>{{"Width (X)", 2, 0.01, 1000, 2}, {"Height (Y)", 2, 0.01, 1000, 2}, {"Length (Z)", 2, 0.01, 1000, 2}}; break;
        case ShapeKind::Cylinder: f = std::vector<Field>{{"Radius", 1, 0.01, 1000, 2}, {"Height", 2, 0.01, 1000, 2}, {"Segments", 24, 3, 128, 0}}; break;
        case ShapeKind::Cone: f = std::vector<Field>{{"Bottom radius", 1, 0, 1000, 2}, {"Top radius", 0, 0, 1000, 2}, {"Height", 2, 0.01, 1000, 2}, {"Segments", 24, 3, 128, 0}}; break;
        case ShapeKind::Sphere: f = std::vector<Field>{{"Radius", 1, 0.01, 1000, 2}, {"Segments", 24, 4, 128, 0}}; break;
        case ShapeKind::Torus: f = std::vector<Field>{{"Major radius", 2, 0.01, 1000, 2}, {"Minor radius", 0.5, 0.01, 1000, 2}, {"Segments", 32, 3, 128, 0}}; break;
        case ShapeKind::Plane: f = std::vector<Field>{{"Width", 4, 0.01, 1000, 2}, {"Depth", 4, 0.01, 1000, 2}}; break;
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
        if (A->vs.active > 3)
            return;
        Camera &c = A->cams[A->vs.active];
        if (c.wire)
            c.wire = false;
        else if (!c.textured)
            c.textured = true;
        else
        {
            c.textured = false;
            c.wire = true;
        }
        say(std::string(kViewNames[A->vs.active]) + ": " + (c.wire ? "wireframe" : (c.textured ? "textured" : "solid")) + "  (Z cycles wire, solid, textured)");
        render();
    }
    void toggle_max()
    {
        A->vs.maximised = A->vs.maximised >= 0 ? -1 : A->vs.active;
        render();
    }

    // ---- UV editor --------------------------------------------------------------------------------------------------

    void uv_refresh()
    {
        if (!A->vs.uv)
            return;
        A->ed.uv_prepare();
        const auto scope = A->ed.uv_scope();
        if (!scope.empty())
        {
            const Mesh &m = A->ed.scene().objects[scope[0].object].mesh;
            A->vs.uv_material = std::clamp(m.f[scope[0].face].mat, 0, static_cast<int>(A->ed.scene().materials.size()) - 1);
        }
        render();
    }

    void uv_frame()
    {
        const Rect r = view_rect(4, gtk_widget_get_allocated_width(A->area), gtk_widget_get_allocated_height(A->area), -1, true);
        A->vs.uv_cx = A->vs.uv_cy = 0.5;
        A->vs.uv_zoom = std::max(60.0, std::min(r.w, r.h) * 0.8);
    }

    void set_uv_tool(Editor::UvTool t)
    {
        A->uv_tool = t;
        A->updating = true;
        for (int i = 0; i < 3; ++i)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(A->uv_tool_btn[i]), i == static_cast<int>(t));
        A->updating = false;
        say(t == Editor::UvTool::Move ? "UV move: drag to slide the image mapping (G)" : (t == Editor::UvTool::Rotate ? "UV rotate: drag around the centre (R)" : "UV scale: drag away from or towards the centre (S)"));
    }

    void toggle_uv()
    {
        A->vs.uv = !A->vs.uv;
        if (A->vs.uv)
        {
            A->vs.maximised = -1;
            A->vs.active = 4;
            A->cams[3].wire = false;
            A->cams[3].textured = true;
            A->ed.uv_sel.clear();
            uv_frame();
            if (A->ed.uv_scope().empty())
                say("UV editor: select an object (or faces in Face mode) to edit its UVs.");
            else
                say("UV editor: drag to move/rotate/scale the mapping (G R S), click a UV point to select it, Ctrl+drag to box-select, middle drag pans.");
            uv_refresh();
        }
        else
            A->vs.active = 3;
        gtk_widget_queue_resize(gtk_widget_get_parent(A->area));
        A->updating = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(A->uv_btn), A->vs.uv);
        A->updating = false;
        render();
    }

    void uv_view_coords(double lx, double ly, int w, int h, double &u, double &v)
    {
        u = A->vs.uv_cx + (lx - w / 2.0) / A->vs.uv_zoom;
        v = A->vs.uv_cy - (ly - h / 2.0) / A->vs.uv_zoom;
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

    // The frame just drawn as a pixbuf the caller owns.
    GdkPixbuf *grab_frame()
    {
        const int sc = gtk_widget_get_scale_factor(A->area);
        const int w = gtk_widget_get_allocated_width(A->area) * sc, h = gtk_widget_get_allocated_height(A->area) * sc;
        std::vector<unsigned char> px = A->gl.read_pixels(w, h);
        for (size_t i = 3; i < px.size(); i += 4)
            px[i] = 255;
        GdkPixbuf *view = gdk_pixbuf_new_from_data(px.data(), GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4, nullptr, nullptr);
        GdkPixbuf *copy = gdk_pixbuf_copy(view); // the view points at `px`, which goes away here
        g_object_unref(view);
        return copy;
    }

    bool write_pixbuf(GdkPixbuf *pb, const std::string &file)
    {
        GError *err = nullptr;
        const bool ok = gdk_pixbuf_save(pb, file.c_str(), "png", &err, nullptr);
        if (!ok)
        {
            std::fprintf(stderr, "kosmoscad: could not write %s: %s\n", file.c_str(), err ? err->message : "?");
            g_clear_error(&err);
        }
        return ok;
    }

    bool write_frame(const std::string &file)
    {
        GdkPixbuf *pb = grab_frame();
        const bool ok = write_pixbuf(pb, file);
        g_object_unref(pb);
        return ok;
    }

    // The render window: the finished picture with a Save button, so a render is looked at before it is written anywhere.
    void show_render(GdkPixbuf *pb)
    {
        if (A->render_pb)
            g_object_unref(A->render_pb);
        A->render_pb = pb;
        if (!A->render_win)
        {
            GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(A->window));
            gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4), *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            gtk_container_set_border_width(GTK_CONTAINER(box), 4);
            auto add_button = [&](const char *label, GCallback cb)
            {
                GtkWidget *b = gtk_button_new_with_label(label);
                g_signal_connect(b, "clicked", cb, nullptr);
                gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
            };
            add_button("Save as PNG...", G_CALLBACK(+[](GtkWidget *, gpointer) {
                           if (!A->render_pb)
                               return;
                           const std::string f = choose(GTK_FILE_CHOOSER_ACTION_SAVE, "Save render", "*.png", "render.png");
                           if (!f.empty())
                               say(write_pixbuf(A->render_pb, f) ? "Saved " + f : "Could not write " + f);
                       }));
            add_button("Copy to clipboard", G_CALLBACK(+[](GtkWidget *, gpointer) {
                           if (A->render_pb)
                               gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), A->render_pb);
                       }));
            add_button("Render again", G_CALLBACK(+[](GtkWidget *, gpointer) {
                           A->render_pending = true;
                           render();
                       }));
            add_button("Close", G_CALLBACK(+[](GtkWidget *, gpointer) { gtk_widget_hide(A->render_win); }));
            A->render_img = gtk_image_new();
            GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_container_add(GTK_CONTAINER(scroll), A->render_img);
            gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
            gtk_container_add(GTK_CONTAINER(win), box);
            g_signal_connect(win, "delete-event", G_CALLBACK(+[](GtkWidget *w, GdkEvent *, gpointer) -> gboolean { gtk_widget_hide(w); return TRUE; }), nullptr);
            g_signal_connect(win, "key-press-event", G_CALLBACK(+[](GtkWidget *w, GdkEventKey *e, gpointer) -> gboolean {
                               if (e->keyval == GDK_KEY_Escape)
                               {
                                   gtk_widget_hide(w);
                                   return TRUE;
                               }
                               return FALSE;
                           }), nullptr);
            A->render_win = win;
            gtk_widget_show_all(box);
            gtk_window_set_default_size(GTK_WINDOW(win), std::min(1100, gdk_pixbuf_get_width(pb) + 12), std::min(780, gdk_pixbuf_get_height(pb) + 50));
        }
        gtk_image_set_from_pixbuf(GTK_IMAGE(A->render_img), pb);
        gtk_window_set_title(GTK_WINDOW(A->render_win), ("Render " + std::to_string(gdk_pixbuf_get_width(pb)) + "x" + std::to_string(gdk_pixbuf_get_height(pb))).c_str());
        gtk_window_present(GTK_WINDOW(A->render_win));
    }

    void save_shot()
    {
        write_frame(A->shot_file);
        gtk_main_quit();
    }

    // Textures: DDS through our own decoder, everything else through gdk-pixbuf.
    bool load_texture_file(const std::string &path, Image &out)
    {
        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".dds")
            return load_dds_file(path, out);
        GdkPixbuf *pb = gdk_pixbuf_new_from_file(path.c_str(), nullptr);
        if (!pb)
            return false;
        if (!gdk_pixbuf_get_has_alpha(pb))
        {
            GdkPixbuf *a = gdk_pixbuf_add_alpha(pb, FALSE, 0, 0, 0);
            g_object_unref(pb);
            pb = a;
        }
        out.w = gdk_pixbuf_get_width(pb);
        out.h = gdk_pixbuf_get_height(pb);
        out.rgba.resize(static_cast<size_t>(out.w) * out.h * 4);
        const unsigned char *src = gdk_pixbuf_get_pixels(pb);
        const int stride = gdk_pixbuf_get_rowstride(pb);
        for (int y = 0; y < out.h; ++y)
            std::memcpy(&out.rgba[static_cast<size_t>(y) * out.w * 4], src + static_cast<size_t>(y) * stride, static_cast<size_t>(out.w) * 4);
        g_object_unref(pb);
        return true;
    }

    // The folder relative texture paths are resolved against: the ship.json's, or the .kmodel's.
    std::string texture_base()
    {
        const Scene &sc = A->ed.scene();
        if (!sc.ship_file.empty())
            return fs::path(sc.ship_file).parent_path().string();
        if (!A->ed.doc.file.empty())
            return A->ed.doc.file.parent_path().string();
        return "";
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
            if (A->vs.uv)
                uv_frame();
            A->need_frame = false;
        }
        if (A->drop_tex)
        {
            A->gl.drop_textures();
            A->drop_tex = false;
        }
        A->gl.base_dir = texture_base();
        A->ed.scene().base_dir = A->gl.base_dir;
        A->gl.render(A->ed, A->cams, A->vs, W, H, sc);
        if (A->render_pending)
        {
            // Render: the perspective view alone, textured, without grid or overlays.
            ViewState rv = A->vs;
            rv.uv = false;
            rv.maximised = 3;
            rv.active = 3;
            rv.clean = true;
            Camera rc[4] = {A->cams[0], A->cams[1], A->cams[2], A->cams[3]};
            rc[3].wire = false;
            rc[3].textured = true;
            A->gl.render(A->ed, rc, rv, W, H, sc);
            A->render_pending = false;
            GdkPixbuf *pb = grab_frame();
            gtk_widget_queue_draw(GTK_WIDGET(area)); // put the normal view back
            g_idle_add(+[](gpointer d) -> gboolean { show_render(static_cast<GdkPixbuf *>(d)); return G_SOURCE_REMOVE; }, pb);
            say("Rendered");
        }
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
        const Rect r = view_rect(view, gtk_widget_get_allocated_width(A->area), gtk_widget_get_allocated_height(A->area), A->vs.maximised, A->vs.uv);
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
        const Rect r = view_rect(view, gtk_widget_get_allocated_width(A->area), gtk_widget_get_allocated_height(A->area), A->vs.maximised, A->vs.uv);
        lx = x - r.x;
        ly = y - r.y;
        w = r.w;
        h = r.h;
    }

    gboolean on_press(GtkWidget *widget, GdkEventButton *e, gpointer)
    {
        gtk_widget_grab_focus(widget);
        const int W = gtk_widget_get_allocated_width(widget), H = gtk_widget_get_allocated_height(widget);
        const int v = view_at(e->x, e->y, W, H, A->vs.maximised, A->vs.uv);
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
        A->press_ctrl = e->state & GDK_CONTROL_MASK;
        A->uv_drag = false;
        if (v == 4)
        {
            render();
            return TRUE;
        }
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
        if (A->pressed && A->navigating && A->nav_view == 4)
        {
            A->vs.uv_cx -= dx / A->vs.uv_zoom;
            A->vs.uv_cy += dy / A->vs.uv_zoom;
            render();
            return TRUE;
        }
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
        if (v == 4)
        {
            if (A->pressed && A->press_button == 1 && A->moved)
            {
                double px, py;
                int pw, ph;
                local(v, A->press_x, A->press_y, px, py, pw, ph);
                if (A->press_ctrl)
                {
                    A->vs.box = true;
                    A->vs.bx0 = px;
                    A->vs.by0 = py;
                    A->vs.bx1 = lx;
                    A->vs.by1 = ly;
                }
                else
                {
                    if (!A->uv_drag)
                    {
                        V2 lo, hi;
                        if (A->ed.uv_bounds(lo, hi) && A->ed.uv_begin())
                        {
                            A->uv_pivot = {(lo.u + hi.u) / 2, (lo.v + hi.v) / 2};
                            A->uv_drag = true;
                        }
                    }
                    if (A->uv_drag)
                    {
                        using T = Editor::UvTool;
                        const double zoom = A->vs.uv_zoom;
                        if (A->uv_tool == T::Move)
                            A->ed.uv_apply(T::Move, (lx - px) / zoom, -(ly - py) / zoom, A->uv_pivot);
                        else
                        {
                            const double cx = w / 2.0 + (A->uv_pivot.u - A->vs.uv_cx) * zoom, cy = h / 2.0 - (A->uv_pivot.v - A->vs.uv_cy) * zoom;
                            if (A->uv_tool == T::Rotate)
                            {
                                double deg = (std::atan2(-(ly - cy), lx - cx) - std::atan2(-(py - cy), px - cx)) * 180 / 3.14159265358979323846;
                                A->ed.uv_apply(T::Rotate, deg, 0, A->uv_pivot);
                            }
                            else
                            {
                                const double d0 = std::max(std::hypot(px - cx, py - cy), 4.0), f = std::max(std::hypot(lx - cx, ly - cy) / d0, 0.02);
                                A->ed.uv_apply(T::Scale, f, f, A->uv_pivot);
                            }
                        }
                    }
                }
                render();
            }
            return TRUE;
        }
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
        if (v == 4)
        {
            if (A->uv_drag)
            {
                A->ed.uv_end();
                A->uv_drag = false;
            }
            else if (A->moved && A->press_ctrl)
            {
                double px, py, u0, v0, u1, v1;
                int pw, ph;
                local(v, A->press_x, A->press_y, px, py, pw, ph);
                uv_view_coords(px, py, pw, ph, u0, v0);
                uv_view_coords(lx, ly, w, h, u1, v1);
                A->ed.uv_box_select(u0, v0, u1, v1, e->state & GDK_SHIFT_MASK);
            }
            else if (!A->moved)
            {
                double u, vv;
                uv_view_coords(lx, ly, w, h, u, vv);
                A->ed.uv_pick(u, vv, 10.0 / A->vs.uv_zoom, e->state & GDK_SHIFT_MASK);
            }
            A->vs.box = false;
            render();
            return TRUE;
        }
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
        A->ed.uv_sel.clear();
        sync_ui();
        uv_refresh();
        render();
        (void)widget;
        return TRUE;
    }

    gboolean on_scroll(GtkWidget *widget, GdkEventScroll *e, gpointer)
    {
        const int v = view_at(e->x, e->y, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget), A->vs.maximised, A->vs.uv);
        if (v < 0)
            return TRUE;
        A->vs.active = v;
        if (v == 4)
        {
            const Rect r = view_rect(4, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget), -1, true);
            double u0, v0, u1, v1;
            uv_view_coords(e->x - r.x, e->y - r.y, r.w, r.h, u0, v0);
            const double f = e->direction == GDK_SCROLL_UP ? 1.15 : (e->direction == GDK_SCROLL_DOWN ? 1 / 1.15 : (e->delta_y < 0 ? 1.15 : 1 / 1.15));
            A->vs.uv_zoom = std::clamp(A->vs.uv_zoom * f, 20.0, 20000.0);
            uv_view_coords(e->x - r.x, e->y - r.y, r.w, r.h, u1, v1);
            A->vs.uv_cx += u0 - u1;
            A->vs.uv_cy += v0 - v1;
            render();
            return TRUE;
        }
        A->cams[v].zoom(e->direction == GDK_SCROLL_UP ? 1.15 : (e->direction == GDK_SCROLL_DOWN ? 1 / 1.15 : (e->delta_y < 0 ? 1.15 : 1 / 1.15)));
        render();
        return TRUE;
    }

    // Window layout remembered between sessions (~/.config/kosmoscad/layout.txt): "name x y w h visible" per window.
    struct Geom { int x = 0, y = 0, w = 0, h = 0; bool vis = false, known = false; };
    Geom g_geom[3]; // main, scene, properties

    std::string layout_path()
    {
        const char *home = std::getenv("HOME");
        return home ? std::string(home) + "/.config/kosmoscad/layout.txt" : std::string();
    }

    void layout_load()
    {
        const std::string path = layout_path();
        std::ifstream in(path);
        std::string name;
        Geom g;
        int vis = 0;
        while (in && (in >> name >> g.x >> g.y >> g.w >> g.h >> vis))
        {
            g.vis = vis != 0;
            g.known = g.w > 50 && g.h > 50;
            const int i = name == "main" ? 0 : (name == "scene" ? 1 : (name == "props" ? 2 : -1));
            if (i >= 0)
                g_geom[i] = g;
        }
    }

    void layout_capture()
    {
        GtkWidget *wins[3] = {A->window, A->scene_win, A->props_win};
        for (int i = 0; i < 3; ++i)
        {
            Geom &g = g_geom[i];
            if (i > 0 && !gtk_widget_get_visible(wins[i]))
            {
                // A hidden window reports 0,0; keep what was recorded when it was hidden.
                g.vis = false;
                continue;
            }
            gtk_window_get_position(GTK_WINDOW(wins[i]), &g.x, &g.y);
            gtk_window_get_size(GTK_WINDOW(wins[i]), &g.w, &g.h);
            g.vis = i == 0 || gtk_widget_get_visible(wins[i]);
            g.known = true;
        }
    }

    void layout_save()
    {
        const std::string path = layout_path();
        if (path.empty())
            return;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path);
        const char *names[3] = {"main", "scene", "props"};
        for (int i = 0; i < 3; ++i)
            if (g_geom[i].known)
                out << names[i] << ' ' << g_geom[i].x << ' ' << g_geom[i].y << ' ' << g_geom[i].w << ' ' << g_geom[i].h << ' ' << (g_geom[i].vis ? 1 : 0) << '\n';
    }

    // The scene list and the properties live in floating tool windows so the views get the whole main window.
    // Remembers where a tool window was, so it can come back there after being hidden.
    void remember_tool(GtkWidget *win)
    {
        if (!gtk_widget_get_visible(win))
            return;
        Geom &g = g_geom[win == A->scene_win ? 1 : 2];
        gtk_window_get_position(GTK_WINDOW(win), &g.x, &g.y);
        gtk_window_get_size(GTK_WINDOW(win), &g.w, &g.h);
        g.known = g.w > 50 && g.h > 50;
    }

    // Keeps a window's title bar on a monitor so it cannot be stranded off screen.
    void clamp_to_screen(GtkWidget *win, int &x, int &y, int w)
    {
        GdkDisplay *d = gdk_display_get_default();
        GdkMonitor *m = d ? gdk_display_get_monitor_at_point(d, x + w / 2, y + 20) : nullptr;
        GdkRectangle r{0, 0, 1280, 800};
        if (m)
            gdk_monitor_get_geometry(m, &r);
        else if (d && gdk_display_get_n_monitors(d) > 0)
            gdk_monitor_get_geometry(gdk_display_get_monitor(d, 0), &r);
        x = std::max(r.x, std::min(x, r.x + r.width - 80));
        y = std::max(r.y + 30, std::min(y, r.y + r.height - 60));
    }

    void show_tool(GtkWidget *win, GtkWidget *button, bool show)
    {
        if (show)
        {
            if (!gtk_widget_get_visible(win) && !g_object_get_data(G_OBJECT(win), "placed"))
            {
                const Geom &sg = g_geom[win == A->scene_win ? 1 : 2];
                if (sg.known && (sg.x != 0 || sg.y != 0))
                {
                    int x = sg.x, y = sg.y;
                    clamp_to_screen(win, x, y, sg.w);
                    gtk_window_resize(GTK_WINDOW(win), sg.w, sg.h);
                    gtk_window_move(GTK_WINDOW(win), x, y);
                }
                else
                {
                int x = 0, y = 0, w = 0, h = 0;
                gtk_window_get_position(GTK_WINDOW(A->window), &x, &y);
                gtk_window_get_size(GTK_WINDOW(A->window), &w, &h);
                const bool right = win == A->props_win;
                gtk_window_move(GTK_WINDOW(win), right ? x + w - 360 : x + 24, y + 110);
                }
                g_object_set_data(G_OBJECT(win), "placed", GINT_TO_POINTER(1));
            }
            gtk_window_present(GTK_WINDOW(win));
        }
        else
        {
            remember_tool(win);
            gtk_widget_hide(win);
        }
        A->updating = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), show);
        A->updating = false;
    }
    void toggle_tool(GtkWidget *win, GtkWidget *button) { show_tool(win, button, !gtk_widget_get_visible(win)); }

    gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer)
    {
        GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(w));
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
        case GDK_KEY_F2: toggle_tool(A->scene_win, A->scene_btn); return TRUE;
        case GDK_KEY_F3: toggle_tool(A->props_win, A->props_btn); return TRUE;
        case GDK_KEY_u: toggle_uv(); return TRUE;
        case GDK_KEY_g: if (A->vs.uv && A->vs.active == 4) { set_uv_tool(Editor::UvTool::Move); return TRUE; } A->vs.grid = !A->vs.grid; render(); return TRUE;
        case GDK_KEY_s: if (A->vs.uv && A->vs.active == 4) { set_uv_tool(Editor::UvTool::Scale); return TRUE; } break;
        case GDK_KEY_r: if (A->vs.uv && A->vs.active == 4) { set_uv_tool(Editor::UvTool::Rotate); return TRUE; } set_tool(Tool::Scale); return TRUE;
        case GDK_KEY_q: set_tool(Tool::Select); return TRUE;
        case GDK_KEY_w: set_tool(Tool::Move); return TRUE;
        case GDK_KEY_e: set_tool(Tool::Rotate); return TRUE;
        case GDK_KEY_1: set_mode(Mode::Object); return TRUE;
        case GDK_KEY_2: set_mode(Mode::Vertex); return TRUE;
        case GDK_KEY_3: set_mode(Mode::Edge); return TRUE;
        case GDK_KEY_4: set_mode(Mode::Face); return TRUE;
        case GDK_KEY_Delete:
        case GDK_KEY_x: ed.delete_selection(); clamp_selection(); refresh(); return TRUE;
        case GDK_KEY_space: toggle_max(); return TRUE;
        case GDK_KEY_f:
            if (A->vs.uv && A->vs.active == 4)
                uv_frame();
            else
                A->need_frame = true;
            render();
            return TRUE;
        case GDK_KEY_z: toggle_wire(); return TRUE;
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
        return FALSE;
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
        A->ed.uv_sel.clear();
        sync_ui(false);
        uv_refresh();
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
        gtk_entry_set_text(GTK_ENTRY(A->mat_normal), m.normal_map.c_str());
        gtk_entry_set_text(GTK_ENTRY(A->mat_spec), m.spec_map.c_str());
        gtk_range_set_value(GTK_RANGE(A->mat_metal), m.metallic);
        gtk_range_set_value(GTK_RANGE(A->mat_rough), m.roughness);
        GdkRGBA ec{m.emissive.x, m.emissive.y, m.emissive.z, 1};
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(A->mat_emit), &ec);
        A->updating = false;
        if (A->vs.uv)
        {
            A->vs.uv_material = current_material();
            render();
        }
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
        m.normal_map = gtk_entry_get_text(GTK_ENTRY(A->mat_normal));
        m.spec_map = gtk_entry_get_text(GTK_ENTRY(A->mat_spec));
        // Once the model has a folder, its images live there: pick one from elsewhere and it is copied in.
        if (!texture_base().empty())
        {
            A->ed.scene().base_dir = texture_base();
            if (const int n = localise_textures(A->ed.scene(), texture_base()))
                say("Copied " + std::to_string(n) + " texture file(s) into " + texture_base() + "/textures");
            A->updating = true;
            gtk_entry_set_text(GTK_ENTRY(A->mat_tex), m.texture.c_str());
            gtk_entry_set_text(GTK_ENTRY(A->mat_normal), m.normal_map.c_str());
            gtk_entry_set_text(GTK_ENTRY(A->mat_spec), m.spec_map.c_str());
            A->updating = false;
        }
        m.metallic = gtk_range_get_value(GTK_RANGE(A->mat_metal));
        m.roughness = gtk_range_get_value(GTK_RANGE(A->mat_rough));
        GdkRGBA ec;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(A->mat_emit), &ec);
        m.emissive = {ec.red, ec.green, ec.blue};
        A->ed.changed();
        A->drop_tex = true;
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

    // Sliders fire on every step of a drag; one undo checkpoint per burst of changes.
    void on_material_slider(GtkWidget *, gpointer)
    {
        if (A->updating)
            return;
        static gint64 last = 0;
        const gint64 now = g_get_monotonic_time();
        Material &m = A->ed.scene().materials[current_material()];
        if (now - last > 1000000)
            A->ed.doc.checkpoint();
        last = now;
        m.metallic = gtk_range_get_value(GTK_RANGE(A->mat_metal));
        m.roughness = gtk_range_get_value(GTK_RANGE(A->mat_rough));
        A->ed.changed();
        render();
    }

    // "..." beside a texture path: pick an image, then apply it like pressing Enter in the entry.
    GtkWidget *browse_button(GtkWidget *entry, const char *title)
    {
        GtkWidget *b = gtk_button_new_with_label("...");
        gtk_widget_set_tooltip_text(b, "Browse for an image (PNG, JPG, DDS...)");
        g_object_set_data(G_OBJECT(b), "entry", entry);
        g_object_set_data(G_OBJECT(b), "title", const_cast<char *>(title));
        g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkWidget *w, gpointer) {
                           GtkWidget *e = GTK_WIDGET(g_object_get_data(G_OBJECT(w), "entry"));
                           const std::string f = choose(GTK_FILE_CHOOSER_ACTION_OPEN, static_cast<const char *>(g_object_get_data(G_OBJECT(w), "title")), "*.png;*.jpg;*.jpeg;*.dds;*.bmp;*.tga");
                           if (f.empty())
                               return;
                           gtk_entry_set_text(GTK_ENTRY(e), f.c_str());
                           on_material_edit(nullptr, nullptr);
                       }), nullptr);
        return b;
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
        const Rect r = view_rect(v, gtk_widget_get_allocated_width(A->area), gtk_widget_get_allocated_height(A->area), A->vs.maximised, A->vs.uv);
        GtkRequisition min, nat;
        gtk_widget_get_preferred_size(child, &min, &nat);
        alloc->x = r.x + 6;
        alloc->y = r.y + 4;
        alloc->width = std::min(nat.width, std::max(r.w - 12, 1));
        alloc->height = nat.height;
        gtk_widget_set_visible(child, r.w > 0);
        return TRUE;
    }

    GtkWidget *tool_window(const char *title, GtkWidget *content, int w, int h)
    {
        GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(win), title);
        gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(A->window));
        gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_UTILITY);
        gtk_window_set_default_size(GTK_WINDOW(win), w, h);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
        gtk_widget_show_all(content);
        gtk_container_add(GTK_CONTAINER(win), content);
        g_signal_connect(win, "delete-event", G_CALLBACK(+[](GtkWidget *w, GdkEvent *, gpointer) -> gboolean {
                           A->updating = true;
                           gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w == A->scene_win ? A->scene_btn : A->props_btn), FALSE);
                           A->updating = false;
                           remember_tool(w);
                           gtk_widget_hide(w);
                           return TRUE;
                       }), nullptr);
        g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), nullptr);
        return win;
    }

    void build_ui()
    {
        A->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_default_size(GTK_WINDOW(A->window), A->shot_file.empty() ? 1400 : A->shot_w, A->shot_file.empty() ? 860 : A->shot_h + 90);
        g_signal_connect(A->window, "delete-event", G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer) -> gboolean {
                           if (!discard_ok())
                               return TRUE;
                           layout_capture();
                           return FALSE;
                       }), nullptr);
        g_signal_connect(A->window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer) { gtk_main_quit(); }), nullptr);
        g_signal_connect(A->window, "key-press-event", G_CALLBACK(on_key), nullptr);

        GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(A->window), root);

        // ---- menus
        GtkWidget *bar = gtk_menu_bar_new();
        GtkWidget *file = submenu(bar, "File");
        menu_item(file, "New", +[](GtkWidget *, gpointer) { act_new(); });
        menu_item(file, "Close file  (Ctrl+W)", +[](GtkWidget *, gpointer) { act_close(); });
        menu_item(file, "Open ship.json, scene or mesh...  (Ctrl+O)", +[](GtkWidget *, gpointer) { act_open(); });
        menu_item(file, "Open scene (.kmodel)...", +[](GtkWidget *, gpointer) { act_open_kmodel(); });
        menu_item(file, "Import mesh (OBJ, Orbiter MSH, glTF)...", +[](GtkWidget *, gpointer) { act_import_mesh(); });
        menu_item(file, "Send to Kosmos  (Ctrl+S while editing a part)", +[](GtkWidget *, gpointer) { act_send(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(file), gtk_separator_menu_item_new());
        menu_item(file, "Save scene (.kmodel)  (Ctrl+S)", +[](GtkWidget *, gpointer) { act_save_kmodel(false); });
        menu_item(file, "Save scene as...", +[](GtkWidget *, gpointer) { act_save_kmodel(true); });
        menu_item(file, "Save to ship.json  (Ctrl+Shift+S)", +[](GtkWidget *, gpointer) { act_save_ship(false); });
        menu_item(file, "Save ship.json as...", +[](GtkWidget *, gpointer) { act_save_ship(true); });
        menu_item(file, "Export OBJ...", +[](GtkWidget *, gpointer) { act_export_obj(); });
        menu_item(file, "Export GLB...", +[](GtkWidget *, gpointer) { act_export_glb(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(file), gtk_separator_menu_item_new());
        menu_item(file, "Quit", +[](GtkWidget *, gpointer) { if (discard_ok()) { layout_capture(); gtk_main_quit(); } });

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

        GtkWidget *modify = submenu(bar, "Modify");
        menu_item(modify, "Extrude faces  (distance in Properties)", +[](GtkWidget *, gpointer) { A->ed.extrude(val(A->sp_extrude)); refresh(); });
        menu_item(modify, "Inset faces  (amount in Properties)", +[](GtkWidget *, gpointer) { A->ed.inset(val(A->sp_inset)); refresh(); });
        menu_item(modify, "Delete faces", +[](GtkWidget *, gpointer) { A->ed.delete_selected_faces(); refresh(); });
        menu_item(modify, "Flip normals", +[](GtkWidget *, gpointer) { A->ed.flip_selected_faces(); refresh(); });
        menu_item(modify, "Split edge", +[](GtkWidget *, gpointer) { A->ed.split_selected_edges(); refresh(); });
        menu_item(modify, "Weld vertices  (distance in Properties)", +[](GtkWidget *, gpointer) { A->ed.weld_selected(val(A->sp_weld)); refresh(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(modify), gtk_separator_menu_item_new());
        menu_item(modify, "Mirror X", +[](GtkWidget *, gpointer) { A->ed.mirror_object(0); refresh(); });
        menu_item(modify, "Mirror Y", +[](GtkWidget *, gpointer) { A->ed.mirror_object(1); refresh(); });
        menu_item(modify, "Mirror Z", +[](GtkWidget *, gpointer) { A->ed.mirror_object(2); refresh(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(modify), gtk_separator_menu_item_new());
        menu_item(modify, "Union", +[](GtkWidget *, gpointer) { A->ed.boolean(BoolOp::Union); refresh(); });
        menu_item(modify, "Subtract", +[](GtkWidget *, gpointer) { A->ed.boolean(BoolOp::Subtract); refresh(); });
        menu_item(modify, "Intersect", +[](GtkWidget *, gpointer) { A->ed.boolean(BoolOp::Intersect); refresh(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(modify), gtk_separator_menu_item_new());
        menu_item(modify, "Apply rotation + scale", +[](GtkWidget *, gpointer) { A->ed.apply_transform(); refresh(); });
        menu_item(modify, "Origin to centre", +[](GtkWidget *, gpointer) { A->ed.set_origin_to_centre(); refresh(); });
        gtk_menu_shell_append(GTK_MENU_SHELL(modify), gtk_separator_menu_item_new());
        menu_item(modify, "Extrude sketch", +[](GtkWidget *, gpointer) { finish_sketch(false); });
        menu_item(modify, "Lathe sketch", +[](GtkWidget *, gpointer) { finish_sketch(true); });

        GtkWidget *view = submenu(bar, "View");
        menu_item(view, "Frame everything  (F)", +[](GtkWidget *, gpointer) { A->need_frame = true; render(); });
        menu_item(view, "Maximise / restore the active view  (Space)", +[](GtkWidget *, gpointer) { toggle_max(); });
        menu_item(view, "Wire / solid / textured in the active view  (Z)", +[](GtkWidget *, gpointer) { toggle_wire(); });
        menu_item(view, "UV editor on / off  (U)", +[](GtkWidget *, gpointer) { toggle_uv(); });
        menu_item(view, "Render...  (opens a window with a Save button)", +[](GtkWidget *, gpointer) {
            A->render_pending = true;
            render();
        });
        menu_item(view, "Scene list window  (F2)", +[](GtkWidget *, gpointer) { toggle_tool(A->scene_win, A->scene_btn); });
        menu_item(view, "Properties window  (F3)", +[](GtkWidget *, gpointer) { toggle_tool(A->props_win, A->props_btn); });
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
        gtk_box_pack_start(GTK_BOX(tb), button("Wire/Solid/Textured (Z)", +[](GtkWidget *, gpointer) { toggle_wire(); }), FALSE, FALSE, 0);
        A->scene_btn = gtk_toggle_button_new_with_label("Scene (F2)");
        A->props_btn = gtk_toggle_button_new_with_label("Properties (F3)");
        g_signal_connect(A->scene_btn, "toggled", G_CALLBACK(+[](GtkWidget *w, gpointer) { if (!A->updating) show_tool(A->scene_win, A->scene_btn, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))); }), nullptr);
        g_signal_connect(A->props_btn, "toggled", G_CALLBACK(+[](GtkWidget *w, gpointer) { if (!A->updating) show_tool(A->props_win, A->props_btn, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))); }), nullptr);
        gtk_box_pack_start(GTK_BOX(tb), A->scene_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tb), A->props_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tb), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 6);
        A->uv_btn = gtk_toggle_button_new_with_label("UV editor (U)");
        gtk_widget_set_tooltip_text(A->uv_btn, "Slide, rotate and scale the texture mapping of the selected faces or objects, like Blender's UV editor");
        g_signal_connect(A->uv_btn, "toggled", G_CALLBACK(+[](GtkWidget *, gpointer) { if (!A->updating) toggle_uv(); }), nullptr);
        gtk_box_pack_start(GTK_BOX(tb), A->uv_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tb), button("Maximise (Space)", +[](GtkWidget *, gpointer) { toggle_max(); }), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tb), button("Frame (F)", +[](GtkWidget *, gpointer) { A->need_frame = true; render(); }), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(root), tb, FALSE, FALSE, 0);

        // ---- left: scene tree
        A->store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
        A->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(A->store));
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(A->tree), FALSE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(A->tree), gtk_tree_view_column_new_with_attributes("Scene", gtk_cell_renderer_text_new(), "text", 0, nullptr));
        GtkTreeSelection *ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(A->tree));
        gtk_tree_selection_set_mode(ts, GTK_SELECTION_MULTIPLE);
        g_signal_connect(ts, "changed", G_CALLBACK(on_tree_selection), nullptr);
        GtkWidget *tree_scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(tree_scroll), A->tree);

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
        for (int i = 0; i < 5; ++i)
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
        gtk_widget_set_tooltip_text(A->mat_tex, "Colour (diffuse) image: a path relative to the ship folder, or absolute; copied next to the OBJ on export");
        A->mat_normal = gtk_entry_new();
        gtk_widget_set_tooltip_text(A->mat_normal, "Normal (bump) map, tangent space. Written as map_Bump in the MTL and as the glTF normal texture; Kosmos shades with it");
        A->mat_spec = gtk_entry_new();
        gtk_widget_set_tooltip_text(A->mat_spec, "Specular map in glTF metallic-roughness layout (green = roughness, blue = metallic). Used by GLB parts");
        A->mat_metal = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.01);
        A->mat_rough = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.01);
        gtk_scale_set_draw_value(GTK_SCALE(A->mat_metal), TRUE);
        gtk_scale_set_draw_value(GTK_SCALE(A->mat_rough), TRUE);
        gtk_widget_set_tooltip_text(A->mat_metal, "0 = paint or plastic, 1 = bare metal (GLB parts)");
        gtk_widget_set_tooltip_text(A->mat_rough, "0 = mirror-like, 1 = matt; sets the size of the highlight");
        A->mat_emit = gtk_color_button_new();
        gtk_widget_set_tooltip_text(A->mat_emit, "Light the surface gives off (windows, panels, displays); black for none (GLB parts)");
        g_signal_connect(A->mat_normal, "activate", G_CALLBACK(on_material_edit), nullptr);
        g_signal_connect(A->mat_spec, "activate", G_CALLBACK(on_material_edit), nullptr);
        g_signal_connect(A->mat_metal, "value-changed", G_CALLBACK(on_material_slider), nullptr);
        g_signal_connect(A->mat_rough, "value-changed", G_CALLBACK(on_material_slider), nullptr);
        g_signal_connect(A->mat_emit, "color-set", G_CALLBACK(on_material_edit), nullptr);
        g_signal_connect(A->mat_name, "activate", G_CALLBACK(on_material_edit), nullptr);
        g_signal_connect(A->mat_tex, "activate", G_CALLBACK(on_material_edit), nullptr);
        g_signal_connect(A->mat_colour, "color-set", G_CALLBACK(on_material_edit), nullptr);
        gtk_box_pack_start(GTK_BOX(mm), row("Material", A->mat_combo), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Name", A->mat_name), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Colour", A->mat_colour), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Texture", A->mat_tex, browse_button(A->mat_tex, "Colour texture")), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Normal", A->mat_normal, browse_button(A->mat_normal, "Normal map")), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Spec map", A->mat_spec, browse_button(A->mat_spec, "Metallic-roughness map")), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Metallic", A->mat_metal), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Roughness", A->mat_rough), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mm), row("Emissive", A->mat_emit), FALSE, FALSE, 0);
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

        // the UV editor's tools (the editor itself is the left half of the view area when switched on)
        GtkWidget *uvp = vbox();
        GtkWidget *uvt = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        const char *uv_tool_names[3] = {"Move (G)", "Rotate (R)", "Scale (S)"};
        for (int i = 0; i < 3; ++i)
        {
            A->uv_tool_btn[i] = gtk_toggle_button_new_with_label(uv_tool_names[i]);
            g_object_set_data(G_OBJECT(A->uv_tool_btn[i]), "i", GINT_TO_POINTER(i));
            g_signal_connect(A->uv_tool_btn[i], "clicked", G_CALLBACK(+[](GtkWidget *w, gpointer) {
                               if (!A->updating)
                                   set_uv_tool(static_cast<Editor::UvTool>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "i"))));
                           }), nullptr);
            gtk_box_pack_start(GTK_BOX(uvt), A->uv_tool_btn[i], TRUE, TRUE, 0);
        }
        A->updating = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(A->uv_tool_btn[0]), TRUE);
        A->updating = false;
        gtk_box_pack_start(GTK_BOX(uvp), uvt, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(uvp), two_buttons(button("Flip U", +[](GtkWidget *, gpointer) { A->ed.uv_flip(true); render(); }),
                                                     button("Flip V", +[](GtkWidget *, gpointer) { A->ed.uv_flip(false); render(); }),
                                                     button("Rotate 90", +[](GtkWidget *, gpointer) { A->ed.uv_rotate90(); render(); })), FALSE, FALSE, 0);
        A->uv_checker = gtk_check_button_new_with_label("Checker instead of the image");
        g_signal_connect(A->uv_checker, "toggled", G_CALLBACK(+[](GtkWidget *w, gpointer) {
                           A->vs.checker = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
                           render();
                       }), nullptr);
        A->uv_num[0] = spin(-100, 100, 0.05, 3, 0);
        A->uv_num[1] = spin(-100, 100, 0.05, 3, 0);
        A->uv_num[2] = spin(-360, 360, 5, 1, 0);
        A->uv_num[3] = spin(0.001, 1000, 0.1, 3, 1);
        gtk_box_pack_start(GTK_BOX(uvp), row("Offset U V", A->uv_num[0], A->uv_num[1]), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(uvp), row("Rotate / scale", A->uv_num[2], A->uv_num[3]), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(uvp), two_buttons(button("Apply numbers", +[](GtkWidget *, gpointer) {
                                                          A->ed.uv_transform(val(A->uv_num[0]), val(A->uv_num[1]), val(A->uv_num[2]), val(A->uv_num[3]));
                                                          render();
                                                      }, "Scales and rotates about the centre of the UVs being edited, then offsets them"),
                                                    button("Fit to 0..1", +[](GtkWidget *, gpointer) { A->ed.uv_fit(); render(); })), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(uvp), A->uv_checker, FALSE, FALSE, 0);
        for (int i = 0; i < 2; ++i)
        {
            GtkWidget *cb = gtk_check_button_new_with_label(i == 0 ? "Show UV points" : "Dim the other faces (Face mode)");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb), TRUE);
            g_object_set_data(G_OBJECT(cb), "i", GINT_TO_POINTER(i));
            g_signal_connect(cb, "toggled", G_CALLBACK(+[](GtkWidget *w, gpointer) {
                               const bool on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
                               (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "i")) == 0 ? A->vs.uv_points : A->vs.uv_dim) = on;
                               render();
                           }), nullptr);
            gtk_box_pack_start(GTK_BOX(uvp), cb, FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(side), section("UV editor (U)", uvp), FALSE, FALSE, 0);

        GtkWidget *side_scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(side_scroll), side);
        gtk_box_pack_start(GTK_BOX(root), overlay, TRUE, TRUE, 0);
        A->scene_win = tool_window("Scene", tree_scroll, 230, 380);
        A->props_win = tool_window("Properties", side_scroll, 350, 700);

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
    app.gl.loader = load_texture_file;
    std::string open_file, edit_mesh_arg;
    const char *mode_arg = nullptr;
    bool uv_arg = false, textured_arg = false, panels_arg = false;
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
        else if (a == "--maximise" && more)
            app.vs.maximised = std::atoi(argv[++i]);
        else if (a == "--panels")
            panels_arg = true;
        else if (a == "--uv")
            uv_arg = true;
        else if (a == "--textured")
            textured_arg = true;
        else if (a == "--select-face" && more)
            select_face = std::atoi(argv[++i]);
        else if (a == "--edit-mesh" && more)
            edit_mesh_arg = argv[++i];
        else if (a == "--help" || a == "-h")
        {
            std::puts("kosmoscad [ship.json | scene.kmodel | mesh.obj/.msh/.glb/.gltf] [--edit-mesh PART.glb|PART.obj] [--shot FILE [--size WxH] [--uv] [--textured] [--panels]]");
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
    const bool use_layout = app.shot_file.empty();
    if (use_layout)
    {
        layout_load();
        if (g_geom[0].known)
        {
            gtk_window_set_default_size(GTK_WINDOW(app.window), g_geom[0].w, g_geom[0].h);
            gtk_window_move(GTK_WINDOW(app.window), g_geom[0].x, g_geom[0].y);
        }
    }
    gtk_widget_show_all(app.window);
    gtk_widget_set_visible(app.mk_box, FALSE);
    if (!edit_mesh_arg.empty())
    {
        if (load_any(edit_mesh_arg))
        {
            A->edit_mesh = edit_mesh_arg;
            refresh();
        }
    }
    else if (!open_file.empty())
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
    if (textured_arg)
        for (Camera &c : app.cams)
        {
            c.wire = false;
            c.textured = true;
        }
    if (uv_arg)
        toggle_uv();
    if (panels_arg)
    {
        show_tool(app.scene_win, app.scene_btn, true);
        show_tool(app.props_win, app.props_btn, true);
    }
    if (use_layout)
    {
        if (g_geom[1].vis)
            show_tool(app.scene_win, app.scene_btn, true);
        if (g_geom[2].vis)
            show_tool(app.props_win, app.props_btn, true);
        say("F2 Scene list  |  F3 Properties  |  U UV editor  |  Z wire/solid/textured  |  Space maximise a view  |  Modify menu for modelling tools");
    }
    if (!app.shot_file.empty())
        g_timeout_add(100, +[](gpointer) -> gboolean { render(); return A->shot_frames < 3; }, nullptr);
    gtk_main();
    if (use_layout)
        layout_save();
    A = nullptr;
    return 0;
}
