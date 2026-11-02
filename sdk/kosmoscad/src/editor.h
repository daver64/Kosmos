// KosmosCAD editor: selection, picking, gizmo drags and the modelling commands, on top of Document. No GTK or GL in here,
// so picking and editing are unit-tested headless; the GL view only draws what the editor holds.
#pragma once

#include "camera.h"
#include "csg.h"
#include "equipment.h"
#include "scene.h"

#include <functional>
#include <set>

namespace cad
{
    enum class Mode { Object, Vertex, Edge, Face };
    enum class Tool { Select, Move, Rotate, Scale };
    enum class BoolOp { Union, Subtract, Intersect };
    enum class ShapeKind { Box, Cylinder, Cone, Sphere, Torus, Plane };

    struct Selection
    {
        std::set<int> objects, markers;
        int active = -1;                // the object whose sub-elements are edited
        std::set<int> verts, faces;
        std::set<EdgeKey> edges;
        void clear_elements() { verts.clear(); faces.clear(); edges.clear(); }
        void clear() { objects.clear(); markers.clear(); active = -1; clear_elements(); }
    };

    class Editor
    {
    public:
        Document doc;
        Mode mode = Mode::Object;
        Tool tool = Tool::Move;
        Selection sel;
        unsigned mesh_gen = 1;          // bumps when a mesh changes, so the GL view re-uploads
        std::vector<V2> sketch;         // outline points being drawn, as (right, up) in the sketch view's plane
        Camera::Kind sketch_kind = Camera::Top;

        Scene &scene() { return doc.scene; }
        const Scene &scene() const { return doc.scene; }

        // ---- picking -------------------------------------------------------------------------------------------
        /** Click in a w x h view. `add` (shift) toggles instead of replacing. Returns false when nothing was hit. */
        bool click(const Camera &cam, int w, int h, double x, double y, bool add);
        void box_select(const Camera &cam, int w, int h, double x0, double y0, double x1, double y1, bool add);
        void select_all();
        void set_mode(Mode m);

        // ---- gizmo ---------------------------------------------------------------------------------------------
        /** Centre of the selection (objects, markers, or the chosen vertices/faces in world space); false when nothing is selected. */
        bool pivot(V3 &out) const;
        /** 0..2 for an axis handle, 3 for the centre, -1 for none. Handles are 90 px long on screen. */
        int pick_gizmo(const Camera &cam, int w, int h, double x, double y) const;
        void begin_drag(const Camera &cam, int w, int h, double x, double y, int axis);
        void drag(const Camera &cam, int w, int h, double x, double y);
        void end_drag();
        bool dragging() const { return dragging_; }
        static constexpr double kGizmoPixels = 90;

        // ---- commands (each takes its own undo checkpoint) -------------------------------------------------------
        int add_shape(ShapeKind kind, double a, double b, double c, int segments, const V3 &at);
        int add_mesh(const std::string &name, Mesh mesh, const V3 &at);
        int add_marker(const std::string &kind, const V3 &at);
        void delete_selection();
        void duplicate_selection();
        void apply_transform();
        void set_origin_to_centre();
        void mirror_object(int axis);
        void boolean(BoolOp op);
        void extrude(double distance);
        void inset(double amount);
        void delete_selected_faces();
        void flip_selected_faces();
        void split_selected_edges();
        void weld_selected(double eps);
        void assign_material(int slot);
        void project_uvs(UvMode mode, int axis);
        /** Extrudes the sketch along the view's normal, centred on the plane; `far_scale` != 1 tapers it to that scale (a loft). */
        void sketch_extrude(double depth, double far_scale = 1.0);
        V3 sketch_point(const V2 &p, double normal = 0) const;
        void sketch_lathe(int segments);
        int new_material(const std::string &name);
        void changed();                 // call after editing fields directly (name, material colour, marker props)

        bool bounds(V3 &lo, V3 &hi) const;
        /** World-space vertex positions of an object (or of the active one when -1). */
        std::vector<V3> world_verts(int object) const;

        Object *active_object();
        const Object *active_object() const;

    private:
        struct Snapshot
        {
            std::vector<std::pair<int, Xform>> objects;
            std::vector<std::pair<int, Marker>> markers;
            std::vector<V3> world_verts; // of the active object
            V3 pivot;
        };
        bool dragging_ = false;
        int drag_axis_ = -1;
        double drag_x0_ = 0, drag_y0_ = 0;
        V3 drag_start_;                  // ray/plane hit at the start (move)
        Snapshot snap_;

        void touch_mesh() { ++mesh_gen; doc.dirty = true; }
        std::set<int> element_verts() const;
    };
}
