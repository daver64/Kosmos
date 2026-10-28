#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace kmap
{
    constexpr double pi = 3.14159265358979323846;

    /** Orbiter's planetary quadtree archive (.tree): zlib-compressed tiles, level 4 being two 180 degree tiles. Thread safe. */
    class Tree
    {
    public:
        static constexpr std::uint32_t none = 0xFFFFFFFFu;

        bool open(const std::string &path, std::string &error);
        std::uint32_t find(int level, int ilat, int ilng) const;
        bool has_data(std::uint32_t node) const { return node != none && nodes_[node].size != 0; }
        std::uint32_t child(std::uint32_t node, int quadrant) const { return nodes_[node].child[quadrant]; }
        bool read(std::uint32_t node, std::vector<std::uint8_t> &out) const;
        explicit operator bool() const { return opened_; }

        /** The "Elev" archive for a body in the folders under root (any folder whose name ends in the body name). */
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

    /** Heights in metres from an Elev.tree. Each thread passes its own Cursor so tile lookups mostly skip the shared cache. */
    class Elevation
    {
    public:
        struct Tile
        {
            int width = 0, height = 0;
            double scale = 1.0, offset = 0.0;
            bool flat = true;
            std::vector<std::int16_t> samples;
        };

        struct Cursor
        {
            std::uint32_t node = Tree::none;
            std::shared_ptr<const Tile> tile;
        };

        bool open(const std::string &path, std::string &error) { return tree_.open(path, error); }
        /** Height at a latitude and longitude (radians) from the tile level suited to metres_per_pixel on a body of the given radius. */
        double height(double lat, double lon, double metres_per_pixel, double radius, Cursor &cursor) const;

    private:
        std::shared_ptr<const Tile> tile(std::uint32_t node) const;

        Tree tree_;
        mutable std::mutex mutex_;
        mutable std::unordered_map<std::uint32_t, std::shared_ptr<const Tile>> cache_;
    };

    /** An equirectangular picture of a body (JPEG or PNG), north at the top and longitude -180 at the left. */
    class Picture
    {
    public:
        bool load(const std::string &path, std::string &error);
        void colour(double lat, double lon, float rgb[3]) const;
        bool ok() const { return width_ > 0; }

    private:
        int width_ = 0, height_ = 0;
        std::vector<std::uint8_t> rgb_;
    };

    /** Everything known about the ground of one body. */
    struct Ground
    {
        std::string name;
        double radius = 6371000.0;
        bool home = false; // Earth: heights below zero are sea
        Elevation elevation;
        bool has_elevation = false;
        Picture picture;
        std::string source; // a line of text for the status bar

        /** Finds the files for a body under the Kosmos folder. */
        void load(const std::string &root, const std::string &body, double body_radius);
    };

    /** Finds the Kosmos folder (the one holding data/): an explicit path, KOSMOS_DATA, or by walking up from the program. */
    std::string find_root(const std::string &hint, const std::string &program);
}
