#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "planet_gen.h"

namespace kosmos
{
    /** Where generated planet images and tile sets are kept between runs; outside the install so a read-only install works. */
    std::filesystem::path generated_cache_dir();

    /** The folder holding the baked Celestia-layout tile set of an invented world (it may not exist). */
    std::filesystem::path baked_tiles_dir(const PlanetStyle &style, double radius);

    constexpr int bake_tile_size = 512;

    struct BakeEstimate
    {
        int width = 0, height = 0; // the finest level, in pixels
        int levels = 0;            // level0 .. level(levels-1)
        std::int64_t tiles = 0;
        double disk_bytes = 0.0;   // as written (compressed PNG, a typical ratio)
        double raw_bytes = 0.0;    // uncompressed RGB, for the memory a viewer would need
    };

    /** The finest width is rounded to a power of two times the tile size, at least 2048. */
    BakeEstimate estimate_bake(int width);

    /**
     * Writes the whole tile pyramid (level0 is two tiles; each level doubles the rows and columns) under dir, and a .ctx
     * descriptor. Tiles already on disk are kept, so an interrupted bake resumes. progress gets 0-1 and returns false to stop.
     */
    bool bake_tiles(const PlanetSurface &surface, const std::filesystem::path &dir, int width, const std::function<bool(double)> &progress,
                    std::string &error);

    /**
     * Tiles made the first time the terrain asks for them and kept on disk, so only the parts of a world you visit cost space. A tile
     * that is too coarse for the requested cell size is skipped: below `width` pixels across the equator the generator itself is used.
     * Safe to call from the terrain worker threads.
     */
    class LazyTiles
    {
    public:
        LazyTiles(std::shared_ptr<const PlanetSurface> surface, std::filesystem::path dir, double radius, int width);
        /** direction is the unit direction in the body frame. False if no tile is fine enough (the caller then uses the generator). */
        bool colour(const sl::dvec3 &direction, double cell_size, float rgb[3]) const;

    private:
        struct Tile
        {
            std::vector<std::uint8_t> rgb; // bake_tile_size squared, 3 bytes a pixel
        };
        std::shared_ptr<const Tile> tile(int level, int column, int row) const;

        std::shared_ptr<const PlanetSurface> surface_;
        std::filesystem::path dir_;
        double radius_;
        int max_level_;
        mutable std::mutex mutex_;
        mutable std::map<std::uint64_t, std::shared_ptr<const Tile>> cache_;
    };

    /** The marker file whose presence in a world's tile folder turns on lazy baking; it holds the width. */
    std::filesystem::path lazy_marker(const std::filesystem::path &dir);
    /** The width written in the marker, or 0 if the folder is not lazy. */
    int read_lazy_width(const std::filesystem::path &dir);
    bool write_lazy_marker(const std::filesystem::path &dir, int width);

    /** "1.2 GB" style text. */
    std::string format_bytes(double bytes);
}
