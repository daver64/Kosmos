#pragma once

#include "eva.h"

#include "clutter.h"
#include "json.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kosmos
{
    /**
     * The inside of a building: a grid of square cells (a cube each, `cell()` metres on a side). Level plans are written as text, one
     * character per cell column:
     *   '#' wall (solid from floor to ceiling)      '.' floor, open air above
     *   'D' open doorway (floor, air to the door height, wall above)
     *   'W' window: a wall with a glazed opening at eye height (solid to walk into, see-through)
     *   'd' sliding door (see Door)
     *   'A' airlock hatch (like a doorway but closed): the way in from outside
     *   ' ' nothing / outside
     * Each level is `height` cells of air between a floor slab and the next level's slab (the roof on the top level), so the grid is
     * (height + 1) * levels + 1 cells tall. Interior coordinates are metres: x along a row, z down the rows (the plan's top edge is the
     * front, -z, which is the way a person looks at yaw 0), y up with the floor of the bottom level at y = 0. The grid maps into the
     * building's ship frame with `to_ship()`.
     */
    /** What changes while the building is lived in: how far each door is open, and the state of the airlock. */
    struct InteriorState
    {
        std::vector<double> door_open; // 0 shut .. 1 open, one per Interior::doors()
        enum class Airlock
        {
            normal,  // sealed and held at pressure
            venting, // pumping down to let someone out
            vacuum   // sealed, empty, hatch last used from outside
        };
        Airlock airlock = Airlock::normal;
        std::vector<int> vessel_of_room; // region -> Systems vessel index, -1 if none (resolved on first use)
        bool resolved = false;
    };

    class Interior
    {
    public:
        enum class Cell : std::uint8_t
        {
            air,
            wall,
            slab,
            hatch,
            glass
        };

        /** Reads the "interior" object of a building's ship.json. `ground_z` is the height of the ground in an upright building's ship frame. */
        static std::shared_ptr<Interior> parse(const nlohmann::json &json, bool upright, double ground_z, std::string &error);

        double cell() const { return cell_; }
        int nx() const { return nx_; }
        int ny() const { return ny_; }
        int nz() const { return nz_; }
        double height_m() const { return (ny_ - 1) * cell_; }
        Cell at(int x, int y, int z) const;
        /** Air that is open to the outdoors (as opposed to enclosed rooms). */
        bool outdoors(int x, int y, int z) const;

        sl::dvec3 to_ship(const sl::dvec3 &interior) const { return frame_ * interior + origin_; }
        sl::dvec3 from_ship(const sl::dvec3 &ship) const { return glm::transpose(frame_) * (ship - origin_); }
        sl::dvec3 dir_to_ship(const sl::dvec3 &interior) const { return frame_ * interior; }
        sl::dvec3 dir_from_ship(const sl::dvec3 &ship) const { return glm::transpose(frame_) * ship; }

        /** Where a person appears on coming in, the way they face (radians), and the outside of the hatch, all in the ship frame / interior frame as named. */
        sl::dvec3 entry() const { return entry_; }
        double entry_yaw() const { return entry_yaw_; }
        sl::dvec3 door_ship() const { return door_ship_; }
        /** The way out of the hatch, a unit vector in the interior frame. */
        sl::dvec3 outward() const { return outward_; }
        bool has_door() const { return has_door_; }

        /** A board on a wall that shows the building's control panel. `normal` points into the room (interior frame). */
        struct Panel
        {
            sl::dvec3 pos{0.0}, normal{0.0, 0.0, 1.0};
            double width = 1.2;
        };
        const std::vector<Panel> &panels() const { return panels_; }
        /** The panel an eye at `eye` looking along `forward` (both interior frame) is facing within `reach` metres, or -1. */
        int panel_in_view(const sl::dvec3 &eye, const sl::dvec3 &forward, double reach) const;

        /** A ceiling lamp or similar: a point light inside the building, on while the building's bus is powered. */
        struct Light
        {
            sl::dvec3 pos{0.0}; // interior metres
            sl::dvec3 colour{1.0, 0.95, 0.85};
            double intensity = 1.5, range = 7.0;
        };
        const std::vector<Light> &lights() const { return lights_; }
        /** A modder's mesh (absolute path), in interior coordinates, drawn instead of the generated walls. Collision still follows the plan. */
        std::string mesh_path;

        /** A sliding door ('d' in the plan): a one-cell gap in a wall that slides up into the lintel. */
        struct Door
        {
            int x = 0, z = 0;
            bool along_x = true; // the wall runs along x (so the passage is along z)
            int room_a = -1, room_b = -1;
            sl::dvec3 centre{0.0}; // interior metres, at floor level
        };
        const std::vector<Door> &doors() const { return doors_; }
        double door_height() const { return door_cells * cell_; }

        /** The enclosed space (room) at an interior position, or -1. Rooms are split by walls, doors and the hatch. */
        int room_at(const sl::dvec3 &p) const;
        int room_count() const { return static_cast<int>(room_names_.size()); }
        const std::string &room_name(int room) const { return room_names_[static_cast<std::size_t>(room)]; }
        /** Name of the room behind the outside hatch (the airlock), if one is declared with "airlock". */
        const std::string &airlock_name() const { return airlock_name_; }

        /** True if a person standing with feet at `feet` (interior metres), of the given radius and height, overlaps a solid cell. Doors count when `state` says they are shut. */
        bool blocked(const sl::dvec3 &feet, double radius, double height, const InteriorState *state = nullptr) const;

        /** Faces of the walls seen from outdoors (lit by the Sun) and from inside, and of the window glass, in the ship frame. */
        void build_meshes(sl::MeshData &exterior, sl::MeshData &inside, sl::MeshData &glass) const;

    private:
        bool solid(int x, int y, int z) const;
        void classify();
        void place_doors(int w, int d, const std::function<char(int, int, int)> &ch);
        void read_extras(const nlohmann::json &json);
        void find_entry(int w, int d, const std::function<char(int, int, int)> &ch);
        void find_rooms(const nlohmann::json &json, int plan_levels);
        static constexpr int door_cells = 4;

        double cell_ = 0.5;
        int nx_ = 0, ny_ = 0, nz_ = 0;
        std::vector<Cell> cells_;
        std::vector<Panel> panels_;
        std::vector<Light> lights_;
        std::vector<Door> doors_;
        std::vector<std::uint8_t> door_col_; // per plan column, 1 where a door stands
        std::vector<int> door_at_; // per plan column, index into doors_ or -1
        std::vector<int> room_;    // per cell, room id or -1
        std::vector<std::string> room_names_;
        std::string airlock_name_;
        std::vector<std::uint8_t> outdoor_;
        sl::dmat3 frame_{1.0};
        sl::dvec3 origin_{0.0};
        sl::dvec3 entry_{0.0}, door_ship_{0.0}, outward_{0.0, 0.0, -1.0};
        double entry_yaw_ = 0.0;
        bool has_door_ = false;
    };

    /** A person walking inside a building, in interior coordinates. Driven with the same input as walking outside. */
    struct InteriorWalker
    {
        sl::dvec3 position{0.0}, velocity{0.0};
        double yaw = 0.0, pitch = 0.0, crouch = 0.0;
        bool grounded = false;
        double speed = 0.0;
        static constexpr double radius = 0.22;
        static constexpr double eye_height = 1.7;

        void place(const Interior &interior);
        void step(const Interior &interior, const EvaInput &input, double gravity, double dt, const InteriorState *state = nullptr);
        sl::dvec3 eye() const { return position + sl::dvec3(0.0, eye_height - 0.7 * crouch, 0.0); }
        /** The way the person looks, in the interior frame. */
        sl::dvec3 forward() const;
    };
} // namespace kosmos
