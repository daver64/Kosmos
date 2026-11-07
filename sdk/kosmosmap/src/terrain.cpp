#include "terrain.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace kmap
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr int deepest = 22;

        std::string lower(std::string text)
        {
            for (char &c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        fs::path user_scenery()
        {
            const char *xdg = std::getenv("XDG_CONFIG_HOME");
            const char *home = std::getenv("HOME");
            const char *appdata = std::getenv("APPDATA");
            const fs::path base = xdg && *xdg ? fs::path(xdg) : home && *home ? fs::path(home) / ".config" : appdata ? fs::path(appdata) : fs::path();
            return base.empty() ? fs::path() : base / "kosmos" / "scenery";
        }

        struct Hit
        {
            std::uint32_t node = Tree::none;
            double u = 0.0, v = 0.0; // position inside the tile, 0-1, v from its north edge
        };

        // The deepest tile carrying data (down to max_level) under a latitude and longitude in radians.
        Hit locate(const Tree &tree, double lat, double lon, int max_level)
        {
            const double t_lat = std::clamp((0.5 * pi - lat) / pi, 0.0, 1.0);
            const double t_lng = std::clamp((lon + pi) / (2.0 * pi), 0.0, 1.0);
            Hit best;
            std::uint32_t node = Tree::none;
            for (int level = 4; level <= max_level; ++level)
            {
                const std::int64_t rows = std::int64_t(1) << (level - 4);
                const std::int64_t ilat = std::min<std::int64_t>(static_cast<std::int64_t>(t_lat * rows), rows - 1);
                const std::int64_t ilng = std::min<std::int64_t>(static_cast<std::int64_t>(t_lng * 2 * rows), 2 * rows - 1);
                if (level == 4)
                    node = tree.find(4, 0, static_cast<int>(ilng));
                else
                    node = tree.child(node, static_cast<int>(((ilat & 1) << 1) | (ilng & 1)));
                if (node == Tree::none)
                    break;
                if (tree.has_data(node))
                {
                    best.node = node;
                    best.v = t_lat * rows - static_cast<double>(ilat);
                    best.u = t_lng * 2 * rows - static_cast<double>(ilng);
                }
            }
            return best;
        }

        int level_for(double radius, double metres_per_pixel)
        {
            if (metres_per_pixel <= 0.0)
                return deepest;
            const double ratio = radius * pi / (256.0 * metres_per_pixel);
            return std::clamp(4 + static_cast<int>(std::ceil(std::log2(std::max(ratio, 1.0)))), 4, deepest);
        }
    }

    bool Tree::open(const std::string &path, std::string &error)
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

    std::uint32_t Tree::find(int level, int ilat, int ilng) const
    {
        if (level <= 3)
            return level >= 1 ? roots_[level - 1] : none;
        if (level == 4)
            return ilat == 0 && ilng >= 0 && ilng < 2 ? root4_[ilng] : none;
        const std::uint32_t parent = find(level - 1, ilat / 2, ilng / 2);
        return parent == none ? none : nodes_[parent].child[((ilat & 1) << 1) | (ilng & 1)];
    }

    bool Tree::read(std::uint32_t node, std::vector<std::uint8_t> &out) const
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

    std::string Tree::find_layer(const std::string &root, const std::string &body, const std::string &layer)
    {
        const std::string want = lower(body);
        std::error_code ec;
        const auto in_folder = [&](const fs::path &folder) -> std::string {
            for (const char *sub : {"", "Archive"})
            {
                const fs::path file = folder / sub / (layer + ".tree");
                if (fs::exists(file, ec))
                    return file.string();
            }
            return {};
        };
        for (const fs::directory_entry &entry : fs::directory_iterator(root, ec))
        {
            if (!entry.is_directory())
                continue;
            const std::string name = lower(entry.path().filename().string());
            if (name.size() >= want.size() && name.compare(name.size() - want.size(), want.size(), want) == 0)
            {
                const std::string path = in_folder(entry.path());
                if (!path.empty())
                    return path;
            }
            std::error_code inner_ec;
            for (const fs::directory_entry &child : fs::directory_iterator(entry.path(), inner_ec))
                if (child.is_directory() && lower(child.path().filename().string()) == want)
                {
                    const std::string path = in_folder(child.path());
                    if (!path.empty())
                        return path;
                }
        }
        return {};
    }

    std::shared_ptr<const Elevation::Tile> Elevation::tile(std::uint32_t node) const
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = cache_.find(node);
            if (it != cache_.end())
                return it->second;
        }
        auto result = std::make_shared<Tile>();
        std::vector<std::uint8_t> bytes;
        if (tree_.read(node, bytes) && bytes.size() >= 100 && std::memcmp(bytes.data(), "ELE", 3) == 0)
        {
            std::int32_t header, type, nx, ny;
            std::memcpy(&header, bytes.data() + 4, 4);
            std::memcpy(&type, bytes.data() + 8, 4);
            std::memcpy(&nx, bytes.data() + 12, 4);
            std::memcpy(&ny, bytes.data() + 16, 4);
            std::memcpy(&result->scale, bytes.data() + 28, 8);
            std::memcpy(&result->offset, bytes.data() + 36, 8);
            const std::size_t count = static_cast<std::size_t>(nx) * ny;
            const std::uint8_t *data = bytes.data() + header;
            if (type != 0 && nx >= 2 && ny >= 2)
            {
                result->samples.resize(count);
                if (type == -16 && bytes.size() >= header + count * 2)
                {
                    std::memcpy(result->samples.data(), data, count * 2);
                    result->width = nx;
                    result->height = ny;
                    result->flat = false;
                }
                else if (type == 8 && bytes.size() >= header + count)
                {
                    for (std::size_t i = 0; i < count; ++i)
                        result->samples[i] = data[i];
                    result->width = nx;
                    result->height = ny;
                    result->flat = false;
                }
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        // Tiles are about 130 kB; keeping a few thousand covers any screen.
        if (cache_.size() > 3000)
            cache_.clear();
        cache_[node] = result;
        return result;
    }

    double Elevation::height(double lat, double lon, double metres_per_pixel, double radius, Cursor &cursor) const
    {
        const Hit hit = locate(tree_, lat, lon, level_for(radius, metres_per_pixel));
        if (hit.node == Tree::none)
            return 0.0;
        if (cursor.node != hit.node || !cursor.tile)
        {
            cursor.tile = tile(hit.node);
            cursor.node = hit.node;
        }
        const Tile &t = *cursor.tile;
        if (t.flat)
            return t.offset;
        // The grid has a one sample border around the 257 x 257 samples that span the tile, stored from the south edge up.
        const double x = std::clamp(hit.u * 256.0 + 1.0, 0.0, t.width - 1.001);
        const double y = std::clamp((1.0 - hit.v) * 256.0 + 1.0, 0.0, t.height - 1.001);
        const int ix = static_cast<int>(x), iy = static_cast<int>(y);
        const double fx = x - ix, fy = y - iy;
        auto at = [&](int i, int j) { return static_cast<double>(t.samples[static_cast<std::size_t>(j) * t.width + i]); };
        const double v = (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy) + (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy;
        return v * t.scale + t.offset;
    }

    bool Picture::load(const std::string &path, std::string &error)
    {
        GError *gerr = nullptr;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path.c_str(), &gerr);
        if (!pixbuf)
        {
            error = gerr ? gerr->message : "cannot read " + path;
            if (gerr)
                g_error_free(gerr);
            return false;
        }
        width_ = gdk_pixbuf_get_width(pixbuf);
        height_ = gdk_pixbuf_get_height(pixbuf);
        const int channels = gdk_pixbuf_get_n_channels(pixbuf), stride = gdk_pixbuf_get_rowstride(pixbuf);
        const guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
        rgb_.resize(static_cast<std::size_t>(width_) * height_ * 3);
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
                for (int c = 0; c < 3; ++c)
                    rgb_[(static_cast<std::size_t>(y) * width_ + x) * 3 + c] = pixels[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * channels + std::min(c, channels - 1)];
        g_object_unref(pixbuf);
        return true;
    }

    void Picture::colour(double lat, double lon, float rgb[3]) const
    {
        const double fx = std::clamp((lon + pi) / (2.0 * pi), 0.0, 1.0) * width_ - 0.5;
        const double fy = std::clamp((0.5 * pi - lat) / pi, 0.0, 1.0) * height_ - 0.5;
        const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
        const double tx = fx - x0, ty = fy - y0;
        auto at = [&](int x, int y, int c) {
            x = ((x % width_) + width_) % width_;
            y = std::clamp(y, 0, height_ - 1);
            return static_cast<double>(rgb_[(static_cast<std::size_t>(y) * width_ + x) * 3 + c]);
        };
        for (int c = 0; c < 3; ++c)
            rgb[c] = static_cast<float>(((at(x0, y0, c) * (1 - tx) + at(x0 + 1, y0, c) * tx) * (1 - ty) + (at(x0, y0 + 1, c) * (1 - tx) + at(x0 + 1, y0 + 1, c) * tx) * ty) / 255.0);
    }

    void Ground::load(const std::string &root, const std::string &body, double body_radius)
    {
        name = body;
        radius = body_radius > 0.0 ? body_radius : radius;
        home = lower(body) == "earth";
        has_elevation = false;
        picture = Picture();
        source.clear();
        const std::string folder = root + "/import/orbiter";
        std::string error;
        const fs::path user = user_scenery();
        std::string tree = user.empty() ? std::string() : Tree::find_layer((user / "import/orbiter").string(), body, "Elev");
        if (tree.empty())
            tree = Tree::find_layer(folder, body, "Elev");
        if (!tree.empty() && elevation.open(tree, error))
        {
            has_elevation = true;
            source = "Orbiter elevation";
        }
        else if (!error.empty())
            source = error;
        const std::string key = lower(body);
        for (const char *ext : {".jpg", ".png"})
        {
            const fs::path path = user / "data/textures-hires" / (key + ext);
            if (!user.empty() && !picture.ok() && fs::exists(path))
            {
                if (picture.load(path.string(), error))
                    source += std::string(source.empty() ? "" : ", ") + "user image " + path.filename().string();
                else
                    source += std::string(source.empty() ? "" : ", ") + "cannot load " + path.string() + ": " + error;
            }
        }
        for (const char *dir : {"/data/textures/planets/", "/data/textures/", "/import/celestia/medres/", "/import/celestia/lores/"})
            for (const char *ext : {".jpg", ".png"})
            {
                const std::string path = root + dir + key + ext;
                if (!picture.ok() && fs::exists(path) && picture.load(path, error))
                    source += std::string(source.empty() ? "" : ", ") + "image " + fs::path(path).filename().string();
            }
        if (source.empty())
            source = "no terrain data for " + body;
    }

    std::string find_root(const std::string &hint, const std::string &program)
    {
        auto good = [](const fs::path &p) { return !p.empty() && fs::exists(p / "data"); };
        if (good(hint))
            return hint;
        if (const char *env = std::getenv("KOSMOS_DATA"); env && good(env))
            return env;
        std::error_code ec;
        fs::path p = fs::weakly_canonical(program, ec);
        for (int i = 0; i < 6 && !p.empty(); ++i)
        {
            p = p.parent_path();
            if (good(p))
                return p.string();
        }
        fs::path cwd = fs::current_path(ec);
        for (int i = 0; i < 6 && !cwd.empty(); ++i, cwd = cwd.parent_path())
            if (good(cwd))
                return cwd.string();
        return {};
    }
}
