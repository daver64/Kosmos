#include "planet_data.h"

#include "dds.h"
#include "bitmap_release.h"
#include "draw.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <zlib.h>
#include <sstream>
#include <unordered_set>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr int deepest = 22;

        std::uint64_t image_tile_key(int level, int column, int row)
        {
            return (static_cast<std::uint64_t>(level) << 56) | (static_cast<std::uint64_t>(column) << 28) | static_cast<std::uint64_t>(row);
        }

        std::string lower(std::string text)
        {
            for (char &c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

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
        file_.seekg(0, std::ios::end);
        const std::uint64_t file_size = static_cast<std::uint64_t>(std::max<std::streamoff>(0, file_.tellg()));
        file_.seekg(sizeof head);
        if (static_cast<std::uint64_t>(count) * sizeof(Node) > file_size)
        {
            error = "node table larger than " + path;
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
        return parent >= nodes_.size() ? none : nodes_[parent].child[((ilat & 1) << 1) | (ilng & 1)];
    }

    bool OrbiterTree::read(std::uint32_t node, std::vector<std::uint8_t> &out) const
    {
        if (!has_data(node))
            return false;
        // A tile is at most a few MB; the cap and range checks keep a corrupt table from sizing huge buffers.
        constexpr std::uint32_t max_tile_bytes = 256u << 20;
        const std::int64_t pos = nodes_[node].pos;
        const std::int64_t end = node + 1 < nodes_.size() ? nodes_[node + 1].pos : static_cast<std::int64_t>(data_length_);
        if (pos < 0 || end < pos || end > static_cast<std::int64_t>(data_length_) || nodes_[node].size > max_tile_bytes ||
            static_cast<std::uint64_t>(end - pos) > max_tile_bytes)
            return false;
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
        const fs::path name = fs::u8path(body);
        if (body.empty() || name.has_parent_path() || name == "." || name == ".." ||
            (layer != "Surf" && layer != "Elev"))
            return {};
        std::error_code ec;
        const fs::path base = fs::u8path(root);
        for (const fs::path &folder : {base / "import/orbiter", base / "import/orbiter/MinorBodies",
                                      base / "data/orbiter-defaults/earth", base / "data/orbiter-defaults/planets"})
        {
            const fs::path file = folder / name / "Archive" / (layer + ".tree");
            if (fs::is_regular_file(file, ec))
                return file.string();
            if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory)
                std::fprintf(stderr, "[kosmos] Cannot inspect Orbiter scenery %s: %s\n", file.string().c_str(), ec.message().c_str());
            ec.clear();
        }
        return {};
    }

    void SurfaceFrame::to_lat_lon(const glm::dvec3 &d, double &lat, double &lon) const
    {
        const glm::dvec3 b = glm::inverse(orientation) * d;
        lat = std::asin(std::clamp(b.y, -1.0, 1.0));
        lon = std::atan2(-b.z, b.x);
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

    bool OrbiterImagery::colour(const glm::dvec3 &direction, double cell_size, float rgb[3], double &metres_per_pixel) const
    {
        double lat, lon;
        frame_.to_lat_lon(direction, lat, lon);
        const Hit hit = locate(tree_, lat, lon, level_for(radius_, cell_size, 512.0));
        metres_per_pixel = std::numeric_limits<double>::infinity();
        if (hit.node == OrbiterTree::none)
            return false;
        const std::shared_ptr<const Tile> tile = cache_.get(hit.node, [&] { return load(hit.node); });
        if (tile->blocks.empty())
            return false;
        const double rows = static_cast<double>(std::int64_t(1) << (hit.level - 4));
        const double cols = rows * 2.0;
        metres_per_pixel = std::max(pi * radius_ / (rows * tile->height),
                                    2.0 * pi * radius_ * std::max(std::cos(lat), 0.0) / (cols * tile->width));
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
        return true;
    }

    struct CelestiaImagery::Impl
    {
        struct Tile
        {
            sl::Bitmap *bitmap = nullptr;
            ~Tile() { discard_bitmap(bitmap); } // may run on a terrain worker thread; discard_bitmap defers the GL-touching work to the main thread
        };

        struct TileSet
        {
            std::string root;
            std::string extension;
            int base_split = 0;
            int min_level = 0;
            int max_level = -1;
            int tile_size = 0;
            std::vector<std::unordered_set<std::uint64_t>> present;

            bool valid() const { return max_level >= min_level; }
            bool has(int level, int column, int row) const
            {
                return level >= min_level && level <= max_level &&
                       present[static_cast<std::size_t>(level)].count(image_tile_key(level, column, row)) != 0;
            }
            std::string path(int level, int column, int row) const
            {
                char name[64];
                std::snprintf(name, sizeof(name), "tx_%d_%d.%s", column, row, extension.c_str());
                return (std::filesystem::path(root) / ("level" + std::to_string(level)) / name).string();
            }
        };

        std::string error;
        SurfaceFrame frame;
        double radius = 0.0;
        std::string single_image;
        int image_width = 0, image_height = 0;
        TileSet tiles;
        mutable TileCache<Tile> cache{96};

        explicit Impl(const std::string &path, const SurfaceFrame &surface_frame, double body_radius) : frame(surface_frame), radius(body_radius)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path source(path);
            if (path.empty())
            {
                error = "no Celestia surface image";
                return;
            }
            if (fs::is_regular_file(source, ec) && lower(source.extension().string()) != ".ctx")
            {
                single_image = source.string();
                const std::shared_ptr<const Tile> tile = cache.get(0, [&] { return load_tile(single_image); });
                if (tile->bitmap)
                {
                    image_width = tile->bitmap->width;
                    image_height = tile->bitmap->height;
                    return;
                }
                single_image.clear();
                error = "cannot decode surface image " + path;
                return;
            }

            std::string ctx;
            if (fs::is_regular_file(source, ec) && lower(source.extension().string()) == ".ctx")
                ctx = source.string();
            else if (fs::is_directory(source, ec))
            {
                for (const fs::directory_entry &entry : fs::directory_iterator(source, ec))
                    if (entry.is_regular_file(ec) && lower(entry.path().extension().string()) == ".ctx")
                    {
                        ctx = entry.path().string();
                        break;
                    }
            }

            std::string tile_type;
            if (!ctx.empty())
            {
                std::ifstream in(ctx);
                std::stringstream buffer;
                buffer << in.rdbuf();
                const std::string text = buffer.str();
                auto field = [&](const std::string &key)
                {
                    const std::size_t at = text.find(key);
                    if (at == std::string::npos)
                        return std::string{};
                    std::size_t begin = at + key.size();
                    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
                        ++begin;
                    if (begin < text.size() && text[begin] == '"')
                    {
                        const std::size_t end = text.find('"', begin + 1);
                        return end == std::string::npos ? std::string{} : text.substr(begin + 1, end - begin - 1);
                    }
                    std::size_t end = begin;
                    while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != '}')
                        ++end;
                    return text.substr(begin, end - begin);
                };
                const std::string directory = field("ImageDirectory");
                const std::string split = field("BaseSplit");
                tile_type = lower(field("TileType"));
                if (!split.empty())
                    tiles.base_split = std::max(0, std::atoi(split.c_str()));
                const fs::path beside = fs::path(ctx).parent_path();
                const fs::path named = beside / directory;
                tiles.root = !directory.empty() && fs::is_directory(named, ec) ? named.string() : beside.string();
            }
            else if (fs::is_directory(source, ec))
                tiles.root = source.string();

            if (!tiles.root.empty())
            {
                for (const fs::directory_entry &level_dir : fs::directory_iterator(tiles.root, ec))
                {
                    const std::string name = level_dir.path().filename().string();
                    if (!level_dir.is_directory() || name.rfind("level", 0) != 0 || name.size() < 6)
                        continue;
                    const int level = std::atoi(name.c_str() + 5);
                    if (level < 0 || level > 26)
                        continue;
                    if (static_cast<int>(tiles.present.size()) <= level)
                        tiles.present.resize(static_cast<std::size_t>(level) + 1);
                    for (const fs::directory_entry &file : fs::directory_iterator(level_dir.path(), ec))
                    {
                        if (!file.is_regular_file(ec))
                            continue;
                        const std::string filename = file.path().filename().string();
                        int column = 0, row = 0;
                        if (std::sscanf(filename.c_str(), "tx_%d_%d.", &column, &row) != 2 || column < 0 || row < 0)
                            continue;
                        const std::string extension = file.path().extension().string();
                        if (tiles.extension.empty())
                            tiles.extension = extension.substr(1);
                        if (lower(tiles.extension) != lower(extension.substr(1)))
                            continue;
                        tiles.present[static_cast<std::size_t>(level)].insert(image_tile_key(level, column, row));
                        tiles.max_level = std::max(tiles.max_level, level);
                    }
                }
                tiles.min_level = 0;
                while (tiles.min_level <= tiles.max_level && tiles.present[static_cast<std::size_t>(tiles.min_level)].empty())
                    ++tiles.min_level;
                if (!tiles.valid() && !tile_type.empty())
                    tiles.extension = tile_type;
            }

            if (tiles.valid())
            {
                for (int level = tiles.min_level; level <= tiles.max_level && tiles.tile_size == 0; ++level)
                    for (std::uint64_t key : tiles.present[static_cast<std::size_t>(level)])
                    {
                        const int column = static_cast<int>((key >> 28) & 0x0FFFFFFF);
                        const int row = static_cast<int>(key & 0x0FFFFFFF);
                        const std::shared_ptr<const Tile> tile = cache.get(key, [&] { return load_tile(tiles.path(level, column, row)); });
                        if (tile->bitmap)
                            tiles.tile_size = tile->bitmap->width;
                        break;
                    }
                if (tiles.tile_size > 0)
                    return;
                tiles.max_level = -1;
                error = "no readable Celestia tiles under " + tiles.root;
            }

            if (fs::is_directory(source, ec))
            {
                for (const fs::directory_entry &entry : fs::directory_iterator(source, ec))
                {
                    if (!entry.is_regular_file(ec))
                        continue;
                    const std::string ext = lower(entry.path().extension().string());
                    if (ext == ".dds" || ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                    {
                        single_image = entry.path().string();
                        break;
                    }
                }
            }
            if (!single_image.empty())
            {
                const std::shared_ptr<const Tile> tile = cache.get(0, [&] { return load_tile(single_image); });
                if (tile->bitmap)
                {
                    image_width = tile->bitmap->width;
                    image_height = tile->bitmap->height;
                    return;
                }
                single_image.clear();
            }
            if (error.empty())
                error = "no readable image or tile pyramid at " + path;
        }

        std::shared_ptr<const Tile> load_tile(const std::string &path) const
        {
            auto tile = std::make_shared<Tile>();
            const std::string ext = lower(std::filesystem::path(path).extension().string());
            if (ext == ".dds")
            {
                sl::DdsOptions options;
                options.allow_compressed_upload = false;
                tile->bitmap = sl::load_dds(path, options);
            }
            else
                tile->bitmap = sl::load_bitmap(path);
            if (tile->bitmap && (tile->bitmap->compressed || tile->bitmap->pixels.empty()))
            {
                discard_bitmap(tile->bitmap);
                tile->bitmap = nullptr;
            }
            constexpr int max_image_size = 4096;
            if (tile->bitmap && std::max(tile->bitmap->width, tile->bitmap->height) > max_image_size)
            {
                const int width = std::max(1, tile->bitmap->width * max_image_size / std::max(tile->bitmap->width, tile->bitmap->height));
                const int height = std::max(1, tile->bitmap->height * max_image_size / std::max(tile->bitmap->width, tile->bitmap->height));
                sl::Bitmap *smaller = sl::create_bitmap(width, height);
                if (smaller)
                {
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < width; ++x)
                        {
                            const int sx = std::min(tile->bitmap->width - 1, x * tile->bitmap->width / width);
                            const int sy = std::min(tile->bitmap->height - 1, y * tile->bitmap->height / height);
                            const std::size_t source = (static_cast<std::size_t>(sy) * tile->bitmap->width + sx) * 4;
                            const std::size_t destination = (static_cast<std::size_t>(y) * width + x) * 4;
                            std::copy_n(tile->bitmap->pixels.data() + source, 4, smaller->pixels.data() + destination);
                        }
                    discard_bitmap(tile->bitmap);
                    tile->bitmap = smaller;
                }
            }
            return tile;
        }

        static void sample(const sl::Bitmap &bitmap, double u, double v, float rgb[3])
        {
            const double x = std::clamp(u * bitmap.width - 0.5, 0.0, bitmap.width - 1.001);
            const double y = std::clamp(v * bitmap.height - 0.5, 0.0, bitmap.height - 1.001);
            const int ix = static_cast<int>(x), iy = static_cast<int>(y);
            const double fx = x - ix, fy = y - iy;
            auto texel = [&](int px, int py, int channel)
            {
                return static_cast<float>(bitmap.pixels[(static_cast<std::size_t>(py) * bitmap.width + px) * 4 + channel]) / 255.0f;
            };
            for (int channel = 0; channel < 3; ++channel)
            {
                const float top = static_cast<float>(texel(ix, iy, channel) * (1.0 - fx) + texel(ix + 1, iy, channel) * fx);
                const float bottom = static_cast<float>(texel(ix, iy + 1, channel) * (1.0 - fx) + texel(ix + 1, iy + 1, channel) * fx);
                rgb[channel] = static_cast<float>(top * (1.0 - fy) + bottom * fy);
            }
        }

        bool colour(const glm::dvec3 &direction, double cell_size, float rgb[3], double &metres_per_pixel) const
        {
            double lat, lon;
            frame.to_lat_lon(direction, lat, lon);
            const double t_lat = std::clamp((0.5 * pi - lat) / pi, 0.0, 1.0);
            double t_lon = std::fmod((lon + pi) / (2.0 * pi), 1.0);
            if (t_lon < 0.0)
                t_lon += 1.0;
            metres_per_pixel = std::numeric_limits<double>::infinity();
            if (!single_image.empty())
            {
                const std::shared_ptr<const Tile> tile = cache.get(0, [&] { return load_tile(single_image); });
                if (!tile->bitmap)
                    return false;
                sample(*tile->bitmap, t_lon, t_lat, rgb);
                metres_per_pixel = std::max(pi * radius / image_height,
                                            2.0 * pi * radius * std::max(std::cos(lat), 0.0) / image_width);
                return true;
            }
            if (!tiles.valid() || tiles.tile_size <= 0)
                return false;
            auto rows_at = [&](int level) { return std::int64_t(1) << (level + tiles.base_split); };
            auto pixel_size = [&](int level, int width, int height)
            {
                const double rows = static_cast<double>(rows_at(level));
                const double cols = rows * 2.0;
                return std::max(pi * radius / (rows * height),
                                2.0 * pi * radius * std::max(std::cos(lat), 0.0) / (cols * width));
            };
            int target = tiles.max_level;
            if (cell_size > 0.0 && tiles.min_level < tiles.max_level)
            {
                const double base_size = pixel_size(tiles.min_level, tiles.tile_size, tiles.tile_size);
                const int extra = std::max(0, static_cast<int>(std::ceil(std::log2(std::max(base_size / cell_size, 1.0)))));
                target = std::min(tiles.max_level, tiles.min_level + extra);
            }
            for (int level = target; level >= tiles.min_level; --level)
            {
                const std::int64_t rows = rows_at(level);
                const std::int64_t cols = rows * 2;
                const int column = std::min(static_cast<int>(t_lon * cols), static_cast<int>(cols - 1));
                const int row = std::min(static_cast<int>(t_lat * rows), static_cast<int>(rows - 1));
                if (!tiles.has(level, column, row))
                    continue;
                const std::uint64_t key = image_tile_key(level, column, row);
                const std::shared_ptr<const Tile> tile = cache.get(key, [&] { return load_tile(tiles.path(level, column, row)); });
                if (!tile->bitmap)
                    continue;
                const double u = t_lon * cols - column, v = t_lat * rows - row;
                sample(*tile->bitmap, u, v, rgb);
                metres_per_pixel = pixel_size(level, tile->bitmap->width, tile->bitmap->height);
                return true;
            }
            return false;
        }
    };

    CelestiaImagery::CelestiaImagery(const std::string &path, const SurfaceFrame &frame, double radius)
        : impl_(std::make_unique<Impl>(path, frame, radius))
    {
    }

    CelestiaImagery::~CelestiaImagery() = default;

    bool CelestiaImagery::ok() const
    {
        return impl_ && ((!impl_->single_image.empty() && impl_->image_width > 0 && impl_->image_height > 0) ||
                         (impl_->tiles.valid() && impl_->tiles.tile_size > 0));
    }

    const std::string &CelestiaImagery::error() const
    {
        return impl_->error;
    }

    bool CelestiaImagery::colour(const glm::dvec3 &direction, double cell_size, float rgb[3], double &metres_per_pixel) const
    {
        return impl_ && ok() && impl_->colour(direction, cell_size, rgb, metres_per_pixel);
    }
}
