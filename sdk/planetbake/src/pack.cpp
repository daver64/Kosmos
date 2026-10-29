#include "pack.h"

#include "bake.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace planetbake
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string slurp(const std::string &path)
        {
            std::ifstream f(path);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        bool json_number(const std::string &text, const char *key, double &value)
        {
            const std::size_t at = text.find(std::string("\"") + key + "\"");
            if (at == std::string::npos)
                return false;
            const std::size_t colon = text.find(':', at);
            if (colon == std::string::npos)
                return false;
            char *end = nullptr;
            const double v = std::strtod(text.c_str() + colon + 1, &end);
            if (end == text.c_str() + colon + 1)
                return false;
            value = v;
            return true;
        }

        std::string json_string(const std::string &text, const char *key)
        {
            const std::size_t at = text.find(std::string("\"") + key + "\"");
            if (at == std::string::npos)
                return std::string();
            const std::size_t open = text.find('"', text.find(':', at) + 1);
            const std::size_t close = open == std::string::npos ? open : text.find('"', open + 1);
            return close == std::string::npos ? std::string() : text.substr(open + 1, close - open - 1);
        }

        void read_info(const std::string &path, Pack &pack)
        {
            const std::string text = slurp(path);
            if (text.empty())
                return;
            const std::string type = json_string(text, "type");
            if (!type.empty())
            {
                const kosmos::Archetype a = kosmos::archetype_from_name(type);
                if (a != kosmos::Archetype::none)
                    pack.style.type = a;
            }
            double v;
            if (json_number(text, "seed", v))
                pack.style.seed = static_cast<int>(v);
            if (json_number(text, "ocean", v))
                pack.style.ocean = static_cast<float>(v);
            if (json_number(text, "ice", v))
                pack.style.ice = static_cast<float>(v);
            if (json_number(text, "relief", v))
                pack.style.relief = static_cast<float>(v);
            if (json_number(text, "bands", v))
                pack.style.bands = static_cast<int>(v);
            if (json_number(text, "contrast", v))
                pack.style.contrast = static_cast<float>(v);
            if (json_number(text, "radius_m", v))
                pack.radius_m = v;
            if (json_number(text, "width", v))
                pack.width = static_cast<int>(v);
            double lo, hi;
            if (json_number(text, "height_min_m", lo) && json_number(text, "height_max_m", hi) && hi > lo)
            {
                pack.has_range = true;
                pack.height_min = lo;
                pack.height_max = hi;
            }
            const std::size_t tint = text.find("\"tint\"");
            if (tint != std::string::npos)
            {
                const std::size_t open = text.find('[', tint);
                if (open != std::string::npos)
                    std::sscanf(text.c_str() + open + 1, "%f , %f , %f", &pack.style.tint[0], &pack.style.tint[1], &pack.style.tint[2]);
            }
        }

        bool has_suffix(const std::string &s, const std::string &suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }

        std::string lower(std::string s)
        {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        std::string tile_stem(const TileRef &t) { return "tx_" + std::to_string(t.column) + "_" + std::to_string(t.row); }

        std::string lock_file(const Pack &pack, LockScope scope, int layer, const TileRef &tile)
        {
            const fs::path dir(pack.dir);
            if (scope == LockScope::pack)
                return (dir / ".locked").string();
            if (!pack.tiled)
                return (dir / (pack.name + "_" + layer_name(layer) + ".locked")).string();
            if (scope == LockScope::layer)
                return (dir / layer_name(layer) / ".locked").string();
            return (dir / layer_name(layer) / ("level" + std::to_string(tile.level)) / (tile_stem(tile) + ".locked")).string();
        }

        Rendered *prepare(Rendered &out, int layer, int w, int h)
        {
            out.w = w;
            out.h = h;
            if (layer == layer_height)
                out.height.assign(static_cast<std::size_t>(w) * h, 0);
            else
                out.layer[layer].assign(static_cast<std::size_t>(w) * h * 4, 255);
            return &out;
        }

        // One pixel of an image, as RGBA and (for heights) 16 bit.
        void store(Rendered &out, int layer, std::size_t index, const Image &img, std::size_t source)
        {
            if (layer == layer_height)
                out.height[index] = !img.gray.empty() ? img.gray[source] : static_cast<std::uint16_t>(img.rgba[source * 4] * 257);
            else
                for (int c = 0; c < 4; ++c)
                    out.layer[layer][index * 4 + c] = img.rgba[source * 4 + c];
        }
    }

    int Pack::max_level() const
    {
        int level = -1;
        for (const PackLayer &l : layer)
            if (l.present)
                level = std::max(level, l.levels - 1);
        return level;
    }

    std::string Pack::tile_path(int layer_index, int level, int column, int row, const char *ext) const
    {
        const std::string e = ext ? ext : (layer[layer_index].ext.empty() ? ".png" : layer[layer_index].ext);
        return (fs::path(dir) / layer_name(layer_index) / ("level" + std::to_string(level)) / ("tx_" + std::to_string(column) + "_" + std::to_string(row) + e)).string();
    }

    std::string Pack::image_path(int layer_index, const char *ext) const
    {
        if (!ext && !layer[layer_index].file.empty())
            return layer[layer_index].file;
        const std::string e = ext ? ext : (layer[layer_index].ext.empty() ? ".png" : layer[layer_index].ext);
        return (fs::path(dir) / (name + "_" + layer_name(layer_index) + e)).string();
    }

    std::string Pack::info_path() const { return tiled ? (fs::path(dir) / "info.json").string() : (fs::path(dir) / (name + "_info.json")).string(); }

    bool open_pack(const std::string &dir, Pack &pack, std::string &error)
    {
        pack = Pack();
        pack.style.type = kosmos::Archetype::earth;
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
        {
            error = dir + " is not a folder";
            return false;
        }
        pack.dir = fs::path(dir).lexically_normal().string();
        while (pack.dir.size() > 1 && pack.dir.back() == '/')
            pack.dir.pop_back();

        // Tile pyramids: LAYER/levelN/tx_C_R.ext
        for (int i = 0; i < layer_count; ++i)
        {
            const fs::path layer_dir = fs::path(pack.dir) / layer_name(i);
            if (!fs::is_directory(layer_dir, ec))
                continue;
            for (int level = 0; level < 12; ++level)
            {
                const fs::path level_dir = layer_dir / ("level" + std::to_string(level));
                if (!fs::is_directory(level_dir, ec))
                    break;
                pack.layer[i].present = true;
                pack.layer[i].levels = level + 1;
                if (pack.layer[i].ext.empty())
                    for (const auto &entry : fs::directory_iterator(level_dir, ec))
                    {
                        const std::string e = lower(entry.path().extension().string());
                        if (e == ".png" || e == ".dds" || e == ".jpg")
                        {
                            pack.layer[i].ext = e;
                            break;
                        }
                    }
            }
            if (!pack.layer[i].present)
                continue;
            pack.tiled = true;
            if (pack.layer[i].ext.empty())
                pack.layer[i].ext = ".png";
        }
        if (pack.tiled)
        {
            pack.name = fs::path(pack.dir).filename().string();
            read_info(pack.info_path(), pack);
            if (!pack.has_range)
                read_info((fs::path(pack.dir) / "planetbake-fill.json").string(), pack);
            if (pack.width <= 0)
                pack.width = tile_size * 2 * (1 << std::max(0, pack.max_level()));
            return true;
        }

        // Single images: NAME_layer.ext (the layer names are unusual enough for the suffix to be a safe test).
        std::map<std::string, int> stems;
        for (const auto &entry : fs::directory_iterator(pack.dir, ec))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string e = lower(entry.path().extension().string());
            if (e != ".png" && e != ".dds")
                continue;
            const std::string stem = entry.path().stem().string();
            for (int i = 0; i < layer_count; ++i)
            {
                const std::string suffix = std::string("_") + layer_name(i);
                if (has_suffix(lower(stem), suffix))
                {
                    const std::string name = stem.substr(0, stem.size() - suffix.size());
                    ++stems[name];
                    if (pack.name.empty() || stems[name] > stems[pack.name])
                        pack.name = name;
                }
            }
        }
        if (pack.name.empty())
        {
            error = dir + " holds no planet textures (neither LAYER/levelN tiles nor NAME_layer.png images)";
            return false;
        }
        for (int i = 0; i < layer_count; ++i)
            for (const char *e : {".dds", ".png"})
            {
                const std::string path = pack.image_path(i, e);
                int w, h;
                if (fs::exists(path, ec) && image_size(path, w, h))
                {
                    pack.layer[i].present = true;
                    pack.layer[i].ext = e;
                    pack.layer[i].file = path;
                    pack.layer[i].w = w;
                    pack.layer[i].h = h;
                    pack.width = std::max(pack.width, w);
                    break;
                }
            }
        read_info(pack.info_path(), pack);
        if (!pack.has_range)
            read_info((fs::path(pack.dir) / "planetbake-fill.json").string(), pack);
        return true;
    }

    TileRef tile_at(double u, double v, int level)
    {
        const int rows = 1 << level, cols = rows * 2;
        double m = 0.5 - u;
        m -= std::floor(m);
        TileRef t;
        t.level = level;
        t.column = std::clamp(static_cast<int>(m * cols), 0, cols - 1);
        t.row = std::clamp(static_cast<int>(v * rows), 0, rows - 1);
        return t;
    }

    void tile_bounds(const TileRef &tile, double &u0, double &v0, double &u1, double &v1)
    {
        const int rows = 1 << tile.level, cols = rows * 2;
        u0 = 0.5 - static_cast<double>(tile.column + 1) / cols;
        u0 -= std::floor(u0);
        u1 = u0 + 1.0 / cols;
        v0 = static_cast<double>(tile.row) / rows;
        v1 = static_cast<double>(tile.row + 1) / rows;
    }

    bool read_pack_tile(const Pack &pack, int layer, const TileRef &tile, Image &out, std::string &error)
    {
        if (!pack.layer[layer].present || !pack.tiled)
        {
            error = "no such tile";
            return false;
        }
        return read_image(pack.tile_path(layer, tile.level, tile.column, tile.row), out, error);
    }

    bool pack_preview(const Pack &pack, int layer, int w, int h, Rendered &out, std::string &error)
    {
        const PackLayer &pl = pack.layer[layer];
        if (!pl.present)
        {
            error = std::string("the pack has no ") + layer_name(layer) + " layer";
            return false;
        }
        for (int i = 0; i < layer_count; ++i)
            out.layer[i].clear();
        out.height.clear();
        prepare(out, layer, w, h);
        if (layer == layer_height)
            std::fill(out.height.begin(), out.height.end(), static_cast<std::uint16_t>(0));
        else
            for (std::size_t i = 0; i < out.layer[layer].size(); i += 4)
            {
                out.layer[layer][i] = out.layer[layer][i + 1] = out.layer[layer][i + 2] = 40;
                out.layer[layer][i + 3] = 255;
            }

        if (!pack.tiled)
        {
            Image img;
            if (!read_image(pl.file, img, error))
                return false;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const int sx = std::min(img.w - 1, static_cast<int>((x + 0.5) * img.w / w)), sy = std::min(img.h - 1, static_cast<int>((y + 0.5) * img.h / h));
                    store(out, layer, static_cast<std::size_t>(y) * w + x, img, static_cast<std::size_t>(sy) * img.w + sx);
                }
            return true;
        }

        // The finest level up to 1 that has tiles (a preview is 1024 wide; two 512 tiles make level 0).
        int level = std::min(1, pl.levels - 1);
        const int rows = 1 << level, cols = rows * 2;
        std::vector<Image> tiles(static_cast<std::size_t>(cols) * rows);
        std::vector<bool> loaded(tiles.size(), false);
        std::error_code ec;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const std::string path = pack.tile_path(layer, level, c, r);
                std::string err;
                if (fs::exists(path, ec) && read_image(path, tiles[static_cast<std::size_t>(r) * cols + c], err))
                    loaded[static_cast<std::size_t>(r) * cols + c] = true;
            }
        for (int y = 0; y < h; ++y)
        {
            const double v = (y + 0.5) / h;
            const int row = std::clamp(static_cast<int>(v * rows), 0, rows - 1);
            for (int x = 0; x < w; ++x)
            {
                double m = 0.5 - (x + 0.5) / w;
                m -= std::floor(m);
                const int column = std::clamp(static_cast<int>(m * cols), 0, cols - 1);
                const std::size_t index = static_cast<std::size_t>(row) * cols + column;
                if (!loaded[index])
                    continue;
                const Image &tile = tiles[index];
                const int tx = std::clamp(static_cast<int>((m * cols - column) * tile.w), 0, tile.w - 1);
                const int ty = std::clamp(static_cast<int>((v * rows - row) * tile.h), 0, tile.h - 1);
                store(out, layer, static_cast<std::size_t>(y) * w + x, tile, static_cast<std::size_t>(ty) * tile.w + tx);
            }
        }
        return true;
    }

    DdsKind dds_kind_of(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        char head[128] = {};
        f.read(head, sizeof head);
        if (head[84] == 'D' && head[85] == 'X' && head[86] == 'T')
            return head[87] == '1' ? DdsKind::bc1 : DdsKind::bc3;
        return DdsKind::rgba;
    }

    bool write_layer_image(const std::string &path, int layer, const std::uint8_t *rgba, const std::uint16_t *gray, int w, int h, bool dds, DdsKind kind)
    {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        const std::string part = path + ".part";
        bool ok;
        if (dds)
        {
            ok = write_dds(part, rgba, w, h, kind, true);
        }
        else
        {
            PngWriter writer;
            const PngKind png = layer == layer_height ? PngKind::gray16 : layer == layer_specular ? PngKind::gray8 : layer == layer_clouds ? PngKind::rgba8 : PngKind::rgb8;
            ok = writer.open(part, w, h, png);
            std::vector<std::uint8_t> line(static_cast<std::size_t>(writer.bytes_per_row()));
            for (int y = 0; ok && y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    const std::size_t i = static_cast<std::size_t>(y) * w + x;
                    switch (png)
                    {
                    case PngKind::gray16:
                    {
                        const std::uint16_t v = gray ? gray[i] : static_cast<std::uint16_t>(rgba[i * 4] * 257);
                        line[x * 2] = static_cast<std::uint8_t>(v >> 8);
                        line[x * 2 + 1] = static_cast<std::uint8_t>(v);
                        break;
                    }
                    case PngKind::gray8:
                        line[x] = rgba[i * 4];
                        break;
                    case PngKind::rgba8:
                        std::copy_n(rgba + i * 4, 4, line.data() + x * 4);
                        break;
                    default:
                        std::copy_n(rgba + i * 4, 3, line.data() + x * 3);
                        break;
                    }
                }
                ok = writer.row(line.data());
            }
            ok = writer.close() && ok;
        }
        if (!ok)
        {
            fs::remove(part, ec);
            return false;
        }
        fs::rename(part, path, ec);
        return !ec;
    }

    bool is_locked(const Pack &pack, LockScope scope, int layer, const TileRef &tile)
    {
        std::error_code ec;
        return fs::exists(lock_file(pack, scope, layer, tile), ec);
    }

    bool is_locked(const Pack &pack, int layer, const TileRef &tile)
    {
        if (is_locked(pack, LockScope::pack, layer, tile) || is_locked(pack, LockScope::layer, layer, tile))
            return true;
        return pack.tiled && is_locked(pack, LockScope::tile, layer, tile);
    }

    bool is_edit_locked(const Pack &pack, int layer, const TileRef &tile)
    {
        if (!pack.tiled || is_locked(pack, LockScope::pack, layer, tile) || is_locked(pack, LockScope::layer, layer, tile))
            return false;
        return slurp(lock_file(pack, LockScope::tile, layer, tile)).find("(edited tile)") != std::string::npos;
    }

    bool set_locked(const Pack &pack, LockScope scope, int layer, const TileRef &tile, bool locked, std::string &error, bool edited)
    {
        const std::string path = lock_file(pack, scope, layer, tile);
        std::error_code ec;
        if (locked)
        {
            fs::create_directories(fs::path(path).parent_path(), ec);
            std::ofstream f(path);
            f << (edited ? "planetbake lock (edited tile): this tile was edited by hand and is not overwritten by planetbake. Delete this file to unlock it.\n" : "planetbake lock: this part of the pack is not overwritten by planetbake. Delete this file to unlock it.\n");
            if (!f)
            {
                error = "cannot write " + path;
                return false;
            }
            return true;
        }
        fs::remove(path, ec);
        if (ec)
        {
            error = "cannot remove " + path;
            return false;
        }
        return true;
    }

    int count_tile_locks(const Pack &pack, int layer)
    {
        if (!pack.tiled || !pack.layer[layer].present)
            return 0;
        int n = 0;
        std::error_code ec;
        for (int level = 0; level < pack.layer[layer].levels; ++level)
            for (const auto &entry : fs::directory_iterator(fs::path(pack.dir) / layer_name(layer) / ("level" + std::to_string(level)), ec))
                if (entry.path().extension() == ".locked")
                    ++n;
        return n;
    }
}
