#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>


namespace kosmos
{
    /**
     * Global land cover (EarthEnv consensus classes, 30 arc-second / about 1 km): one file per class holding the percentage
     * (0-100) of each cell covered by that class. The twelve full-resolution files add up to several gigabytes
     * uncompressed, so they are condensed once into a single "dominant class" raster at 4 bits per cell
     * (about 380 MB, memory mapped so only the parts being looked at are ever read).
     *
     *   1 needleleaf trees    2 evergreen broadleaf  3 deciduous broadleaf  4 mixed/other trees
     *   5 shrubs              6 herbaceous           7 cultivated           8 regularly flooded
     *   9 urban/built-up     10 snow/ice            11 barren              12 open water
     * and 0 for no data.
     */
    enum LandClass
    {
        lc_nodata = 0,
        lc_needleleaf,
        lc_evergreen_broadleaf,
        lc_deciduous_broadleaf,
        lc_mixed_trees,
        lc_shrubs,
        lc_herbaceous,
        lc_cultivated,
        lc_flooded,
        lc_urban,
        lc_snow,
        lc_barren,
        lc_water,
        lc_count
    };

    /** Visual ground types the terrain textures are chosen for. */
    enum GroundGroup
    {
        group_water = 0,
        group_snow,
        group_urban,
        group_farm, // cultivated land
        group_forest,
        group_barren,
        group_grass, // herbaceous cover and wetland
        group_scrub, // shrubland
        group_count
    };

    /** How much of each ground group a land class contributes (rows sum to 1). */
    const float *class_to_groups(int land_class);
    const char *group_name(int group);

    /** Per-class weights (index 0 unused) of the land cover around a point; sums to 1 when valid. */
    struct LandSample
    {
        float weight[lc_count] = {};
        bool valid = false;
    };

    class Landclass
    {
    public:
        Landclass();
        ~Landclass();
        Landclass(const Landclass &) = delete;
        Landclass &operator=(const Landclass &) = delete;

        /**
         * Open `cache_path`, first (re)building it from `consensus_full_class_N.tif` files in `source_directory` when it
         * is missing or older than they are. `progress` (optional) is told 0-1 and a message during a build.
         * Returns false with a message if there is no usable data.
         */
        bool open(const std::string &source_directory, const std::string &cache_path, std::string &error,
                  const std::function<void(double, const std::string &)> &progress = {}, bool force_rebuild = false);
        /** A small in-memory raster for tests: `cells` is row-major land classes, covering lon0..lon0+w*cell. */
        void set_cells(int width, int height, double lon0, double lat_top, double cell_degrees,
                       const std::vector<std::uint8_t> &cells);
        void close();

        bool valid() const { return width_ > 0; }
        int width() const { return width_; }
        int height() const { return height_; }
        /** Dominant class of the cell containing a point (0 when there is no data). */
        int class_at(double lon_deg, double lat_deg) const;
        /** Bilinear blend of the four nearest cell centres, so class borders are soft rather than 1 km blocks. */
        LandSample sample(double lon_deg, double lat_deg) const;
        static constexpr int kCacheVersion = 1;

    private:
        int cell(int x, int y) const;

        int width_ = 0, height_ = 0;
        double lon0_ = -180.0, lat_top_ = 90.0, cell_ = 1.0 / 120.0;
        const std::uint8_t *data_ = nullptr;
        std::size_t stride_ = 0;
        struct Mapping;
        std::unique_ptr<Mapping> mapping_;
        std::vector<std::uint8_t> owned_;
    };

    /** Read one 128x128-style tile of an 8-bit grey TIFF (uncompressed or LZW), for the condensing step and tests. */
    class TiledTiff
    {
    public:
        bool open(const std::string &path, std::string &error);
        int width() const { return width_; }
        int height() const { return height_; }
        int tile_width() const { return tile_w_; }
        int tile_height() const { return tile_h_; }
        int tiles_across() const { return (width_ + tile_w_ - 1) / tile_w_; }
        int tiles_down() const { return (height_ + tile_h_ - 1) / tile_h_; }
        /** Decode a tile into tile_width*tile_height bytes. */
        bool read_tile(int tile_x, int tile_y, std::vector<std::uint8_t> &out);

    private:
        std::string path_;
        int width_ = 0, height_ = 0, tile_w_ = 0, tile_h_ = 0, compression_ = 1;
        std::vector<std::uint64_t> offsets_, counts_;
        std::vector<std::uint8_t> scratch_;
        std::shared_ptr<void> file_;
    };

    /** TIFF-flavour LZW (MSB first, early change). Returns the number of bytes produced. */
    std::size_t lzw_decode(const std::uint8_t *in, std::size_t in_size, std::uint8_t *out, std::size_t out_size);
} // namespace kosmos
