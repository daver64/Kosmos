#pragma once

#include "eva.h"

#include "clutter.h"
#include "json.hpp"

#include <cstdint>
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
     *   'A' airlock hatch (like a doorway but closed): the way in from outside
     *   ' ' nothing / outside
     * Each level is `height` cells of air between a floor slab and the next level's slab (the roof on the top level), so the grid is
     * (height + 1) * levels + 1 cells tall. Interior coordinates are metres: x along a row, z down the rows (the plan's top edge is the
     * front, -z, which is the way a person looks at yaw 0), y up with the floor of the bottom level at y = 0. The grid maps into the
     * building's ship frame with `to_ship()`.
     */
    class Interior
    {
    public:
        enum class Cell : std::uint8_t
        {
            air,
            wall,
            slab,
            hatch
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

        /** True if a person standing with feet at `feet` (interior metres), of the given radius and height, overlaps a solid cell. */
        bool blocked(const sl::dvec3 &feet, double radius, double height) const;

        /** Faces of the walls seen from outdoors (lit by the Sun) and from inside (baked), in the ship frame. */
        void build_meshes(sl::MeshData &exterior, sl::MeshData &inside) const;

    private:
        bool solid(int x, int y, int z) const;
        void classify();

        double cell_ = 0.5;
        int nx_ = 0, ny_ = 0, nz_ = 0;
        std::vector<Cell> cells_;
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
        void step(const Interior &interior, const EvaInput &input, double gravity, double dt);
        sl::dvec3 eye() const { return position + sl::dvec3(0.0, eye_height - 0.7 * crouch, 0.0); }
        /** The way the person looks, in the interior frame. */
        sl::dvec3 forward() const;
    };
} // namespace kosmos
