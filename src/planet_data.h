#pragma once

#include "terrain.h"

#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <fstream>
#include <functional>
#include <list>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kosmos
{
    /*
     * Readers for Orbiter's planetary quadtree archives (.tree), see ZTreeMgr in the Orbiter source. Level 4 is
     * 1 x 2 tiles (each 180 x 180 degrees) and every level after it doubles both directions; tile row 0 is the
     * north and column 0 starts at longitude -180. Each tile is zlib-compressed. The index is limited to 128 MiB;
     * tile reads have separate limits. Everything here is thread safe.
     */
    class OrbiterTree
    {
    public:
        static constexpr std::uint32_t none = 0xFFFFFFFFu;

        struct Tile
        {
            std::uint32_t node = none;
            int level = 0;
            int row = 0;
            int column = 0;
            /** Requested tile's rectangle within this ancestor, measured from its north-west corner. */
            double u0 = 0.0, v0 = 0.0, u1 = 0.0, v1 = 0.0;
        };

        bool open(const std::string &path, std::string &error);
        /** Node of a tile, or none. */
        std::uint32_t find(int level, int ilat, int ilng) const;
        /** Deepest data-bearing ancestor of a tile, including the tile itself; none if unavailable or invalid. */
        Tile ancestor(int level, int row, int column) const;
        /** True when the node carries tile data of its own. */
        bool has_data(std::uint32_t node) const { return opened_ && node < nodes_.size() && nodes_[node].size != 0; }
        bool has_children(std::uint32_t node) const;
        std::uint32_t child(std::uint32_t node, int quadrant) const { return nodes_[node].child[quadrant]; }
        /** Read a tile, rejecting either compressed or decompressed data above max_bytes. */
        bool read(std::uint32_t node, std::vector<std::uint8_t> &out, std::size_t max_bytes = 256u << 20) const;
        explicit operator bool() const { return opened_; }

        // Find a launcher-installed high-resolution or standard-pack layer, in priority order.
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

    /** A least-recently-used cache of decoded tiles, bounded by count and payload bytes. */
    template <class T>
    class TileCache
    {
    public:
        explicit TileCache(std::size_t capacity, std::size_t max_bytes = std::numeric_limits<std::size_t>::max(),
                           std::function<std::size_t(const T &)> weight = {})
            : capacity_(capacity), max_bytes_(max_bytes), weight_(std::move(weight)) {}

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
            {
                order_.splice(order_.begin(), order_, it->second);
                return it->second->second;
            }
            const std::size_t bytes = weight_ ? weight_(*value) : 0;
            if (bytes > max_bytes_ || capacity_ == 0)
                return value;
            order_.emplace_front(key, value);
            map_[key] = order_.begin();
            bytes_ += bytes;
            while (map_.size() > capacity_ || bytes_ > max_bytes_)
            {
                bytes_ -= weight_ ? weight_(*order_.back().second) : 0;
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
        std::size_t max_bytes_;
        std::function<std::size_t(const T &)> weight_;
        std::size_t bytes_ = 0;
    };

    /** Where a body's surface sits in the world: the body-fixed to world rotation. */
    struct SurfaceFrame
    {
        glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
        /** World direction -> body-fixed longitude and latitude in radians. */
        void to_lat_lon(const glm::dvec3 &direction, double &lat, double &lon) const;
    };

    /** Terrain heights from Orbiter archives and individual Elev/Elev_mod tiles beside them. */
    class OrbiterElevation final : public sl::TerrainSource
    {
    public:
        /**
         * min_height and max_height bound the data; sea_floor, when not NaN, is the lowest height returned (a flat sea
         * at 0 for Earth, whose elevation file also holds bathymetry). Modifications apply to samples of matching
         * elevation tiles before interpolation; unsupported or unreadable modification tiles are logged.
         */
        OrbiterElevation(const std::string &path, const SurfaceFrame &frame, double radius, double min_height, double max_height, double sea_floor);

        bool ok() const { return static_cast<bool>(tree_) || !elevation_tiles_.empty(); }
        const std::string &error() const { return error_; }
        double height(double, double) const override { return 0.0; }
        double height_on_sphere(const glm::dvec3 &direction, double radius) const override;
        double height_on_sphere_lod(const glm::dvec3 &direction, double radius, double cell_size) const override;
        double min_height() const override { return min_; }
        double max_height() const override { return max_; }

    private:
        struct Tile
        {
            double scale = 1.0, offset = 0.0;
            bool valid = false;
            bool flat = true;
            bool unsigned_samples = false;
            std::vector<std::uint16_t> samples;
            std::shared_ptr<const Tile> modification;
            std::size_t memory_bytes() const
            {
                return samples.capacity() * sizeof(samples[0]) + (modification ? modification->memory_bytes() : 0);
            }
        };

        std::shared_ptr<Tile> decode(const std::vector<std::uint8_t> &bytes, bool modification) const;
        std::shared_ptr<Tile> load(int level, int row, int column, bool modification) const;

        OrbiterTree tree_;
        OrbiterTree modifications_;
        std::string error_;
        std::string elevation_root_, modification_root_;
        std::unordered_set<std::uint64_t> elevation_tiles_, modification_tiles_;
        SurfaceFrame frame_;
        double radius_, min_, max_, sea_floor_;
        mutable TileCache<Tile> cache_{600, 64u << 20, [](const Tile &tile) { return tile.memory_bytes(); }};
        int max_level_ = 4;
    };

    /** Surface colour from Orbiter BC1/BC2/BC3 DDS tiles, decoded one texel at a time. */
    class OrbiterImagery
    {
    public:
        OrbiterImagery(const std::string &path, const SurfaceFrame &frame, double radius);

        bool ok() const { return static_cast<bool>(tree_) || !override_tiles_.empty(); }
        const std::string &error() const { return error_; }
        /** Whether the exact Orbiter tile exists in the archive or individual-file overrides. */
        bool has_tile(int level, int row, int column) const;
        /** Whether the archive or individual-file overrides have a descendant beneath this tile. */
        bool has_children(int level, int row, int column) const;
        /** Colour in 0-1 from the tile level whose texels are about cell_size metres, falling back through unreadable ancestors. */
        bool colour(const glm::dvec3 &direction, double cell_size, float rgb[3], double &metres_per_pixel) const;
        /**
         * Read a DDS tile for the requested Orbiter tile, falling back through ancestors when a tile is missing or
         * unreadable. The returned rectangle locates the requested tile within the returned DDS. max_bytes limits
         * both compressed and decompressed archive data. max_decoded_bytes optionally limits RGBA pixel data, with
         * oversized overrides falling back to the same-level archive before trying ancestors. Thread-safe.
         */
        bool read_tile(int level, int row, int column, OrbiterTree::Tile &tile, std::vector<std::uint8_t> &dds,
                       std::size_t max_bytes = 256u << 20, std::size_t max_decoded_bytes = 0) const;

    private:
        struct Tile
        {
            int width = 0, height = 0;
            bool alpha_block = false;
            std::vector<std::uint8_t> blocks;
            std::size_t memory_bytes() const { return blocks.capacity() * sizeof(blocks[0]); }
        };

        std::shared_ptr<const Tile> load(int level, int row, int column) const;
        bool read_exact(int level, int row, int column, std::vector<std::uint8_t> &dds,
                        std::size_t max_bytes, std::uint32_t &node, std::size_t max_decoded_bytes = 0) const;

        OrbiterTree tree_;
        std::string error_;
        SurfaceFrame frame_;
        double radius_;
        std::string override_root_;
        std::unordered_set<std::uint64_t> override_tiles_, override_parents_;
        mutable TileCache<Tile> cache_{900, 64u << 20, [](const Tile &tile) { return tile.memory_bytes(); }};
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
