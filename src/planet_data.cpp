#include "planet_data.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <zlib.h>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr int deepest = 22;

        // The tile a direction falls in at each level, from the root down to the deepest tile that carries data.
        struct Hit
        {
            std::uint32_t node = OrbiterTree::none;
            int level = 0;
            double u = 0.0, v = 0.0; // position inside the tile, 0-1, v from its north edge
        };

        Hit locate(const OrbiterTree &tree, double lat, double lon, int max_level)
        {
            const double t_lat = std::clamp((0.5 * pi - lat) / pi, 0.0, 1.0);
            const double t_lng = std::clamp((lon + pi) / (2.0 * pi), 0.0, 1.0);
            Hit best;
            std::uint32_t node = OrbiterTree::none;
            for (int level = 4; level <= max_level; ++level)
            {
                const std::int64_t rows = std::int64_t(1) << (level - 4);
                const std::int64_t ilat = std::min<std::int64_t>(static_cast<std::int64_t>(t_lat * rows), rows - 1);
                const std::int64_t ilng = std::min<std::int64_t>(static_cast<std::int64_t>(t_lng * 2 * rows), 2 * rows - 1);
                if (level == 4)
                    node = tree.find(4, 0, static_cast<int>(ilng));
                else
                    node = tree.child(node, static_cast<int>(((ilat & 1) << 1) | (ilng & 1)));
                if (node == OrbiterTree::none)
                    break;
                if (tree.has_data(node))
                {
                    best.node = node;
                    best.level = level;
                    best.v = t_lat * rows - static_cast<double>(ilat);
                    best.u = t_lng * 2 * rows - static_cast<double>(ilng);
                }
            }
            return best;
        }

        int level_for(double radius, double cell_size, double samples_per_tile)
        {
            if (cell_size <= 0.0)
                return deepest;
            const double ratio = radius * pi / (samples_per_tile * cell_size);
            return std::clamp(4 + static_cast<int>(std::ceil(std::log2(std::max(ratio, 1.0)))), 4, deepest);
        }
    }

    bool OrbiterTree::open(const std::string &path, std::string &error)
    {
        file_.open(path, std::ios::binary);
        if (!file_)
        {
            error = "cannot open " + path;
            return false;
        }
        std::uint8_t head[48];
        file_.read(reinterpret_cast<char *>(head), sizeof head);
        if (!file_ || std::memcmp(head, "TX\x01\x00", 4) != 0)
        {
            error = path + " is not an Orbiter .tree archive";
            return false;
        }
        std::uint32_t size, count;
        std::memcpy(&size, head + 4, 4);
        std::memcpy(&data_offset_, head + 12, 4);
        data_offset_ &= 0xFFFFFFFF;
        std::memcpy(&data_length_, head + 16, 8);
        std::memcpy(&count, head + 24, 4);
        std::memcpy(roots_, head + 28, 12);
        std::memcpy(root4_, head + 40, 8);
        if (size != 48)
        {
            error = "unexpected .tree header size in " + path;
            return false;
        }
        nodes_.resize(count);
        file_.read(reinterpret_cast<char *>(nodes_.data()), static_cast<std::streamsize>(count) * sizeof(Node));
        if (!file_)
        {
            error = "truncated node table in " + path;
            return false;
        }
        opened_ = true;
        return true;
    }

    std::uint32_t OrbiterTree::find(int level, int ilat, int ilng) const
    {
        if (level <= 3)
            return level >= 1 ? roots_[level - 1] : none;
        if (level == 4)
            return ilat == 0 && ilng >= 0 && ilng < 2 ? root4_[ilng] : none;
        const std::uint32_t parent = find(level - 1, ilat / 2, ilng / 2);
        return parent == none ? none : nodes_[parent].child[((ilat & 1) << 1) | (ilng & 1)];
    }

    bool OrbiterTree::read(std::uint32_t node, std::vector<std::uint8_t> &out) const
    {
        if (!has_data(node))
            return false;
        const std::int64_t pos = nodes_[node].pos;
        const std::int64_t end = node + 1 < nodes_.size() ? nodes_[node + 1].pos : data_length_;
        std::vector<std::uint8_t> packed(static_cast<std::size_t>(end - pos));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            file_.clear();
            file_.seekg(data_offset_ + pos);
            file_.read(reinterpret_cast<char *>(packed.data()), static_cast<std::streamsize>(packed.size()));
            if (!file_)
                return false;
        }
        out.resize(nodes_[node].size);
        uLongf length = static_cast<uLongf>(out.size());
        return uncompress(out.data(), &length, packed.data(), static_cast<uLong>(packed.size())) == Z_OK && length == out.size();
    }

    std::string OrbiterTree::find_layer(const std::string &root, const std::string &body, const std::string &layer)
    {
        namespace fs = std::filesystem;
        std::string lower = body;
        for (char &c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::error_code ec;
        for (const fs::directory_entry &entry : fs::directory_iterator(root, ec))
        {
            if (!entry.is_directory())
                continue;
            std::string name = entry.path().filename().string();
            for (char &c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name.size() < lower.size() || name.compare(name.size() - lower.size(), lower.size(), lower) != 0)
                continue;
            const fs::path file = entry.path() / (layer + ".tree");
            if (fs::exists(file, ec))
                return file.string();
        }
        return {};
    }

    void SurfaceFrame::to_lat_lon(const glm::dvec3 &d, double &lat, double &lon) const
    {
        const double c = std::cos(angle), s = std::sin(angle);
        const double bx = d.x * c - d.z * s, bz = d.x * s + d.z * c;
        lat = std::asin(std::clamp(d.y, -1.0, 1.0));
        lon = std::atan2(-bz, bx);
    }

    OrbiterElevation::OrbiterElevation(const std::string &path, const SurfaceFrame &frame, double radius, double min_height, double max_height, double sea_floor)
        : frame_(frame), radius_(radius), min_(min_height), max_(max_height), sea_floor_(sea_floor)
    {
        tree_.open(path, error_);
    }

    std::shared_ptr<const OrbiterElevation::Tile> OrbiterElevation::load(std::uint32_t node) const
    {
        auto tile = std::make_shared<Tile>();
        std::vector<std::uint8_t> bytes;
        if (!tree_.read(node, bytes) || bytes.size() < 100 || std::memcmp(bytes.data(), "ELE", 3) != 0)
            return tile;
        std::int32_t header, type, nx, ny;
        std::memcpy(&header, bytes.data() + 4, 4);
        std::memcpy(&type, bytes.data() + 8, 4);
        std::memcpy(&nx, bytes.data() + 12, 4);
        std::memcpy(&ny, bytes.data() + 16, 4);
        std::memcpy(&tile->scale, bytes.data() + 28, 8);
        std::memcpy(&tile->offset, bytes.data() + 36, 8);
        const std::size_t count = static_cast<std::size_t>(nx) * ny;
        if (type == 0 || nx < 2 || ny < 2)
            return tile;
        tile->width = nx;
        tile->height = ny;
        tile->samples.resize(count);
        const std::uint8_t *data = bytes.data() + header;
        if (type == -16 && bytes.size() >= header + count * 2)
            std::memcpy(tile->samples.data(), data, count * 2);
        else if (type == 8 && bytes.size() >= header + count)
            for (std::size_t i = 0; i < count; ++i)
                tile->samples[i] = data[i];
        else
            return std::make_shared<Tile>();
        tile->flat = false;
        return tile;
    }

    double OrbiterElevation::height_on_sphere(const glm::dvec3 &direction, double radius) const
    {
        return height_on_sphere_lod(direction, radius, 0.0);
    }

    double OrbiterElevation::height_on_sphere_lod(const glm::dvec3 &direction, double, double cell_size) const
    {
        double lat, lon;
        frame_.to_lat_lon(direction, lat, lon);
        const Hit hit = locate(tree_, lat, lon, level_for(radius_, cell_size, 256.0));
        double h = 0.0;
        if (hit.node != OrbiterTree::none)
        {
            const std::shared_ptr<const Tile> tile = cache_.get(hit.node, [&] { return load(hit.node); });
            if (tile->flat)
                h = tile->offset;
            else
            {
                // The grid has a one sample border around the 257 x 257 samples that span the tile, stored from the south edge up.
                const double x = std::clamp(hit.u * 256.0 + 1.0, 0.0, tile->width - 1.001);
                const double y = std::clamp((1.0 - hit.v) * 256.0 + 1.0, 0.0, tile->height - 1.001);
                const int ix = static_cast<int>(x), iy = static_cast<int>(y);
                const double fx = x - ix, fy = y - iy;
                auto at = [&](int i, int j) { return static_cast<double>(tile->samples[static_cast<std::size_t>(j) * tile->width + i]); };
                const double v = (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy) + (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy;
                h = v * tile->scale + tile->offset;
            }
        }
        if (std::isfinite(sea_floor_))
            h = std::max(h, sea_floor_);
        return std::clamp(h, min_, max_);
    }

    OrbiterImagery::OrbiterImagery(const std::string &path, const SurfaceFrame &frame, double radius) : frame_(frame), radius_(radius)
    {
        tree_.open(path, error_);
    }

    std::shared_ptr<const OrbiterImagery::Tile> OrbiterImagery::load(std::uint32_t node) const
    {
        auto tile = std::make_shared<Tile>();
        std::vector<std::uint8_t> bytes;
        if (!tree_.read(node, bytes) || bytes.size() < 128 || std::memcmp(bytes.data(), "DDS ", 4) != 0)
            return tile;
        std::uint32_t h, w;
        std::memcpy(&h, bytes.data() + 12, 4);
        std::memcpy(&w, bytes.data() + 16, 4);
        std::size_t offset = 128;
        const bool dxt5 = std::memcmp(bytes.data() + 84, "DXT5", 4) == 0;
        if (!dxt5 && std::memcmp(bytes.data() + 84, "DXT1", 4) != 0)
            return tile;
        const std::size_t need = static_cast<std::size_t>(w / 4) * (h / 4) * (dxt5 ? 16 : 8);
        if (w < 4 || h < 4 || bytes.size() < offset + need)
            return tile;
        tile->width = static_cast<int>(w);
        tile->height = static_cast<int>(h);
        tile->dxt5 = dxt5;
        tile->blocks.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + need));
        return tile;
    }

    namespace
    {
        void texel(const std::vector<std::uint8_t> &blocks, int width, bool dxt5, int x, int y, float rgb[3])
        {
            const std::size_t size = dxt5 ? 16 : 8;
            const std::uint8_t *block = blocks.data() + (static_cast<std::size_t>(y >> 2) * (width >> 2) + (x >> 2)) * size + (dxt5 ? 8 : 0);
            std::uint16_t c0, c1;
            std::memcpy(&c0, block, 2);
            std::memcpy(&c1, block + 2, 2);
            auto expand = [](std::uint16_t c, int out[3])
            {
                out[0] = ((c >> 11) & 31) * 255 / 31;
                out[1] = ((c >> 5) & 63) * 255 / 63;
                out[2] = (c & 31) * 255 / 31;
            };
            int p[4][3];
            expand(c0, p[0]);
            expand(c1, p[1]);
            for (int k = 0; k < 3; ++k)
            {
                if (c0 > c1 || dxt5)
                {
                    p[2][k] = (2 * p[0][k] + p[1][k]) / 3;
                    p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
                }
                else
                {
                    p[2][k] = (p[0][k] + p[1][k]) / 2;
                    p[3][k] = 0;
                }
            }
            const int index = (block[4 + (y & 3)] >> (2 * (x & 3))) & 3;
            for (int k = 0; k < 3; ++k)
                rgb[k] = static_cast<float>(p[index][k]) / 255.0f;
        }
    }

    void OrbiterImagery::colour(const glm::dvec3 &direction, double cell_size, float rgb[3]) const
    {
        double lat, lon;
        frame_.to_lat_lon(direction, lat, lon);
        const Hit hit = locate(tree_, lat, lon, level_for(radius_, cell_size, 512.0));
        rgb[0] = rgb[1] = rgb[2] = 0.0f;
        if (hit.node == OrbiterTree::none)
            return;
        const std::shared_ptr<const Tile> tile = cache_.get(hit.node, [&] { return load(hit.node); });
        if (tile->blocks.empty())
            return;
        const double x = std::clamp(hit.u * tile->width - 0.5, 0.0, tile->width - 1.001);
        const double y = std::clamp(hit.v * tile->height - 0.5, 0.0, tile->height - 1.001);
        const int ix = static_cast<int>(x), iy = static_cast<int>(y);
        const float fx = static_cast<float>(x - ix), fy = static_cast<float>(y - iy);
        float c[4][3];
        texel(tile->blocks, tile->width, tile->dxt5, ix, iy, c[0]);
        texel(tile->blocks, tile->width, tile->dxt5, ix + 1, iy, c[1]);
        texel(tile->blocks, tile->width, tile->dxt5, ix, iy + 1, c[2]);
        texel(tile->blocks, tile->width, tile->dxt5, ix + 1, iy + 1, c[3]);
        for (int k = 0; k < 3; ++k)
            rgb[k] = (c[0][k] * (1 - fx) + c[1][k] * fx) * (1 - fy) + (c[2][k] * (1 - fx) + c[3][k] * fx) * fy;
    }
}
