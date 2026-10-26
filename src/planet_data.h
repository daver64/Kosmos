#pragma once

#include "terrain.h"

#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace kosmos
{
    /*
     * Readers for Orbiter's planetary quadtree archives (.tree), see ZTreeMgr in the Orbiter source. Level 4 is
     * 1 x 2 tiles (each 180 x 180 degrees) and every level after it doubles both directions; tile row 0 is the
     * north and column 0 starts at longitude -180. Each tile is zlib-compressed. Everything here is thread safe.
     */
    class OrbiterTree
    {
    public:
        static constexpr std::uint32_t none = 0xFFFFFFFFu;

        bool open(const std::string &path, std::string &error);
        /** Node of a tile, or none. */
        std::uint32_t find(int level, int ilat, int ilng) const;
        /** True when the node carries tile data of its own. */
        bool has_data(std::uint32_t node) const { return node != none && nodes_[node].size != 0; }
        std::uint32_t child(std::uint32_t node, int quadrant) const { return nodes_[node].child[quadrant]; }
        bool read(std::uint32_t node, std::vector<std::uint8_t> &out) const;
        explicit operator bool() const { return opened_; }

        /**
         * Folder of an Orbiter archive for a body under root (any folder whose name ends in the body name), and the
         * layer's file inside it ("Surf", "Elev", "Mask", "Cloud"); empty if absent.
         */
        static std::string find_layer(const std::string &root, const std::string &body, const std::string &layer);

    private:
        struct Node
        {
            std::int64_t pos;
            std::uint32_t size;
            std::uint32_t child[4];
            std::uint32_t pad;
        };

        mutable std::mutex mutex_;
        mutable std::ifstream file_;
        std::vector<Node> nodes_;
        std::uint32_t roots_[3] = {none, none, none};
        std::uint32_t root4_[2] = {none, none};
        std::int64_t data_offset_ = 0, data_length_ = 0;
        bool opened_ = false;
    };

    /** A small least-recently-used cache of decoded tiles keyed by node index. */
    template <class T>
    class TileCache
    {
    public:
        explicit TileCache(std::size_t capacity) : capacity_(capacity) {}

        template <class Loader>
        std::shared_ptr<const T> get(std::uint64_t key, Loader load)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it = map_.find(key);
                if (it != map_.end())
                {
                    order_.splice(order_.begin(), order_, it->second);
                    return it->second->second;
                }
            }
            std::shared_ptr<const T> value = load();
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = map_.find(key);
            if (it != map_.end())
                return it->second->second;
            order_.emplace_front(key, value);
            map_[key] = order_.begin();
            while (map_.size() > capacity_)
            {
                map_.erase(order_.back().first);
                order_.pop_back();
            }
            return value;
        }

    private:
        using Entry = std::pair<std::uint64_t, std::shared_ptr<const T>>;
        std::mutex mutex_;
        std::list<Entry> order_;
        std::unordered_map<std::uint64_t, typename std::list<Entry>::iterator> map_;
        std::size_t capacity_;
    };

    /** Where a body's surface sits in the world: the body-fixed to world rotation. */
    struct SurfaceFrame
    {
        glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
        /** World direction -> body-fixed longitude and latitude in radians. */
        void to_lat_lon(const glm::dvec3 &direction, double &lat, double &lon) const;
    };

    /** Terrain heights from an Orbiter Elev.tree (int16 or uint8 tiles with a one sample border). */
    class OrbiterElevation final : public sl::TerrainSource
    {
    public:
        /**
         * min_height and max_height bound the data; sea_floor, when not NaN, is the lowest height returned (a flat sea
         * at 0 for Earth, whose elevation file also holds bathymetry).
         */
        OrbiterElevation(const std::string &path, const SurfaceFrame &frame, double radius, double min_height, double max_height, double sea_floor);

        bool ok() const { return static_cast<bool>(tree_); }
        const std::string &error() const { return error_; }
        double height(double, double) const override { return 0.0; }
        double height_on_sphere(const glm::dvec3 &direction, double radius) const override;
        double height_on_sphere_lod(const glm::dvec3 &direction, double radius, double cell_size) const override;
        double min_height() const override { return min_; }
        double max_height() const override { return max_; }

    private:
        struct Tile
        {
            int width = 0, height = 0;
            double scale = 1.0, offset = 0.0;
            bool flat = true;
            std::vector<std::int16_t> samples;
        };

        std::shared_ptr<const Tile> load(std::uint32_t node) const;

        OrbiterTree tree_;
        std::string error_;
        SurfaceFrame frame_;
        double radius_, min_, max_, sea_floor_;
        mutable TileCache<Tile> cache_{600};
        int max_level_ = 4;
    };

    /** Surface colour from an Orbiter Surf.tree (DXT1 or DXT5 tiles), decoded one texel at a time. */
    class OrbiterImagery
    {
    public:
        OrbiterImagery(const std::string &path, const SurfaceFrame &frame, double radius);

        bool ok() const { return static_cast<bool>(tree_); }
        const std::string &error() const { return error_; }
        /** Colour in 0-1 for a world direction, from the tile level whose texels are about cell_size metres. */
        bool colour(const glm::dvec3 &direction, double cell_size, float rgb[3], double &metres_per_pixel) const;

    private:
        struct Tile
        {
            int width = 0, height = 0;
            bool dxt5 = false;
            std::vector<std::uint8_t> blocks;
        };

        std::shared_ptr<const Tile> load(std::uint32_t node) const;

        OrbiterTree tree_;
        std::string error_;
        SurfaceFrame frame_;
        double radius_;
        mutable TileCache<Tile> cache_{900};
        int max_level_ = 4;
    };

    /** Equirectangular image or Celestia virtual-texture imagery, sampled at the terrain's local LOD. */
    class CelestiaImagery
    {
    public:
        CelestiaImagery(const std::string &path, const SurfaceFrame &frame, double radius);
        ~CelestiaImagery();

        bool ok() const;
        const std::string &error() const;
        /** Colour in 0-1 and local texel size in metres. False means no usable tile covers this direction. */
        bool colour(const glm::dvec3 &direction, double cell_size, float rgb[3], double &metres_per_pixel) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
