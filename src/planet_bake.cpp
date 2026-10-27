#include "planet_bake.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#include <cmath>
#include <png.h>

#include "sl.h"

namespace kosmos
{
    namespace fs = std::filesystem;

    fs::path generated_cache_dir()
    {
        std::error_code ec;
#ifdef _WIN32
        const char *base = std::getenv("LOCALAPPDATA");
        fs::path dir = base ? fs::path(base) / "kosmos" / "cache" : fs::temp_directory_path(ec) / "kosmos-cache";
#else
        const char *xdg = std::getenv("XDG_CACHE_HOME");
        const char *home = std::getenv("HOME");
        fs::path dir = xdg && *xdg ? fs::path(xdg) / "kosmos" : home ? fs::path(home) / ".cache" / "kosmos" : fs::temp_directory_path(ec) / "kosmos-cache";
#endif
        dir /= "planets";
        fs::create_directories(dir, ec);
        return dir;
    }

    fs::path baked_tiles_dir(const PlanetStyle &style, double radius)
    {
        char radius_text[32];
        std::snprintf(radius_text, sizeof(radius_text), "-R%.0fkm", radius / 1000.0);
        return generated_cache_dir() / "tiles" / (style.key() + radius_text);
    }

    BakeEstimate estimate_bake(int width)
    {
        BakeEstimate e;
        int levels = 1;
        while (bake_tile_size * 2 * (1 << (levels - 1)) < width && levels < 7)
            ++levels;
        e.levels = std::max(levels, 3); // level 2 is already 2048 wide
        e.width = bake_tile_size * 2 * (1 << (e.levels - 1));
        e.height = e.width / 2;
        for (int level = 0; level < e.levels; ++level)
            e.tiles += static_cast<std::int64_t>(2) * (std::int64_t(1) << (2 * level));
        e.raw_bytes = static_cast<double>(e.tiles) * bake_tile_size * bake_tile_size * 3.0;
        // Terrain colour compresses to roughly half in PNG; noisy rock a little worse, smooth ocean far better.
        e.disk_bytes = e.raw_bytes * 0.5;
        return e;
    }

    namespace
    {
        bool write_png(const sl::Bitmap &bitmap, const fs::path &path)
        {
            FILE *file = std::fopen(path.string().c_str(), "wb");
            if (!file)
                return false;
            png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
            png_infop info = png ? png_create_info_struct(png) : nullptr;
            if (!png || !info)
            {
                png_destroy_write_struct(png ? &png : nullptr, info ? &info : nullptr);
                std::fclose(file);
                return false;
            }
            if (setjmp(png_jmpbuf(png)))
            {
                png_destroy_write_struct(&png, &info);
                std::fclose(file);
                return false;
            }
            png_init_io(png, file);
            png_set_compression_level(png, 4);
            png_set_filter(png, 0, PNG_FILTER_SUB);
            png_set_IHDR(png, info, static_cast<png_uint_32>(bitmap.width), static_cast<png_uint_32>(bitmap.height), 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                         PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
            png_write_info(png, info);
            std::vector<std::uint8_t> row(static_cast<std::size_t>(bitmap.width) * 3);
            for (int y = 0; y < bitmap.height; ++y)
            {
                const std::uint8_t *source = bitmap.pixels.data() + static_cast<std::size_t>(y) * bitmap.width * 4;
                for (int x = 0; x < bitmap.width; ++x)
                    std::copy_n(source + x * 4, 3, row.data() + x * 3);
                png_write_row(png, row.data());
            }
            png_write_end(png, info);
            png_destroy_write_struct(&png, &info);
            std::fclose(file);
            return true;
        }
    }

    bool bake_tiles(const PlanetSurface &surface, const fs::path &dir, int width, const std::function<bool(double)> &progress, std::string &error)
    {
        const BakeEstimate estimate = estimate_bake(width);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
        {
            error = "cannot create " + dir.string() + ": " + ec.message();
            return false;
        }
        std::int64_t done = 0;
        for (int level = 0; level < estimate.levels; ++level)
        {
            const int rows = 1 << level, cols = rows * 2;
            const fs::path level_dir = dir / ("level" + std::to_string(level));
            fs::create_directories(level_dir, ec);
            for (int row = 0; row < rows; ++row)
                for (int column = 0; column < cols; ++column)
                {
                    const fs::path path = level_dir / ("tx_" + std::to_string(column) + "_" + std::to_string(row) + ".png");
                    if (!fs::exists(path, ec))
                    {
                        // The reader counts longitude the other way round from the generator's maps: tile u runs as 0.5 - u.
                        sl::Bitmap *tile = surface.make_region(0.5 - static_cast<double>(column) / cols, static_cast<double>(row) / rows,
                                                               0.5 - static_cast<double>(column + 1) / cols, static_cast<double>(row + 1) / rows,
                                                               bake_tile_size, bake_tile_size);
                        if (!tile)
                        {
                            error = "out of memory making a tile";
                            return false;
                        }
                        // Written under a temporary name so an interrupted bake never leaves a half tile that looks finished.
                        const fs::path partial = path.string() + ".part";
                        const bool saved = write_png(*tile, partial);
                        delete tile;
                        if (!saved)
                        {
                            error = "cannot write " + path.string() + " (disk full?)";
                            return false;
                        }
                        fs::rename(partial, path, ec);
                        if (ec)
                        {
                            error = "cannot write " + path.string();
                            return false;
                        }
                    }
                    ++done;
                    if (progress && !progress(static_cast<double>(done) / static_cast<double>(estimate.tiles)))
                    {
                        error = "stopped";
                        return false;
                    }
                }
        }
        std::ofstream ctx(dir / "tiles.ctx");
        ctx << "VirtualTexture\n{\n    ImageDirectory \".\"\n    BaseSplit 0\n    TileSize " << bake_tile_size << "\n    TileType \"png\"\n}\n";
        return true;
    }

    fs::path lazy_marker(const fs::path &dir) { return dir / "lazy.txt"; }

    int read_lazy_width(const fs::path &dir)
    {
        std::ifstream in(lazy_marker(dir));
        int width = 0;
        in >> width;
        return width > 0 ? width : 0;
    }

    bool write_lazy_marker(const fs::path &dir, int width)
    {
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::ofstream out(lazy_marker(dir));
        out << estimate_bake(width).width << "\n";
        return static_cast<bool>(out);
    }

    LazyTiles::LazyTiles(std::shared_ptr<const PlanetSurface> surface, fs::path dir, double radius, int width)
        : surface_(std::move(surface)), dir_(std::move(dir)), radius_(radius), max_level_(estimate_bake(width).levels - 1)
    {
    }

    std::shared_ptr<const LazyTiles::Tile> LazyTiles::tile(int level, int column, int row) const
    {
        const std::uint64_t key = (static_cast<std::uint64_t>(level) << 56) | (static_cast<std::uint64_t>(column) << 28) | static_cast<std::uint64_t>(row);
        // One tile is made at a time: the generator is already multithreaded, and two threads never repeat the same tile.
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = cache_.find(key);
        if (found != cache_.end())
            return found->second;
        const int rows = 1 << level, cols = rows * 2;
        const fs::path path = dir_ / ("level" + std::to_string(level)) / ("tx_" + std::to_string(column) + "_" + std::to_string(row) + ".png");
        sl::Bitmap *bitmap = nullptr;
        std::error_code ec;
        if (fs::exists(path, ec))
            bitmap = sl::load_bitmap(path.string());
        if (!bitmap || bitmap->width != bake_tile_size)
        {
            delete bitmap;
            bitmap = surface_->make_region(0.5 - static_cast<double>(column) / cols, static_cast<double>(row) / rows,
                                           0.5 - static_cast<double>(column + 1) / cols, static_cast<double>(row + 1) / rows, bake_tile_size, bake_tile_size);
            if (bitmap)
            {
                fs::create_directories(path.parent_path(), ec);
                const std::string partial = path.string() + ".part";
                if (write_png(*bitmap, partial))
                    fs::rename(partial, path, ec);
            }
        }
        auto result = std::make_shared<Tile>();
        if (bitmap)
        {
            result->rgb.resize(static_cast<std::size_t>(bake_tile_size) * bake_tile_size * 3);
            for (std::size_t i = 0; i < static_cast<std::size_t>(bake_tile_size) * bake_tile_size; ++i)
                std::copy_n(bitmap->pixels.data() + i * 4, 3, result->rgb.data() + i * 3);
            delete bitmap;
        }
        if (cache_.size() >= 96)
            cache_.erase(cache_.begin());
        cache_[key] = result;
        return result;
    }

    bool LazyTiles::colour(const sl::dvec3 &d, double cell_size, float rgb[3]) const
    {
        constexpr double pi = 3.14159265358979323846;
        const double latitude = std::asin(std::clamp(d.y, -1.0, 1.0));
        double t = std::fmod(0.5 - std::atan2(d.z, d.x) / (2.0 * pi), 1.0);
        if (t < 0.0)
            t += 1.0;
        const double v = std::clamp(0.5 - latitude / pi, 0.0, 1.0);
        // The coarsest level whose pixels are no bigger than the cell; none that fine means the generator should answer.
        int level = max_level_;
        for (int l = 0; l <= max_level_; ++l)
            if (pi * radius_ / (static_cast<double>(1 << l) * bake_tile_size) <= cell_size)
            {
                level = l;
                break;
            }
        if (pi * radius_ / (static_cast<double>(1 << level) * bake_tile_size) > cell_size * 1.0001 && cell_size > 0.0)
            return false;
        const int rows = 1 << level, cols = rows * 2;
        const int column = std::min(static_cast<int>(t * cols), cols - 1), row = std::min(static_cast<int>(v * rows), rows - 1);
        const std::shared_ptr<const Tile> source = tile(level, column, row);
        if (source->rgb.empty())
            return false;
        const double x = std::clamp((t * cols - column) * bake_tile_size - 0.5, 0.0, bake_tile_size - 1.001);
        const double y = std::clamp((v * rows - row) * bake_tile_size - 0.5, 0.0, bake_tile_size - 1.001);
        const int ix = static_cast<int>(x), iy = static_cast<int>(y);
        const double fx = x - ix, fy = y - iy;
        for (int c = 0; c < 3; ++c)
        {
            auto texel = [&](int px, int py) { return static_cast<double>(source->rgb[(static_cast<std::size_t>(py) * bake_tile_size + px) * 3 + c]); };
            const double top = texel(ix, iy) * (1.0 - fx) + texel(ix + 1, iy) * fx;
            const double bottom = texel(ix, iy + 1) * (1.0 - fx) + texel(ix + 1, iy + 1) * fx;
            rgb[c] = static_cast<float>((top * (1.0 - fy) + bottom * fy) / 255.0);
        }
        return true;
    }

    std::string format_bytes(double bytes)
    {
        const char *units[] = {"B", "KB", "MB", "GB", "TB"};
        int unit = 0;
        while (bytes >= 1024.0 && unit < 4)
        {
            bytes /= 1024.0;
            ++unit;
        }
        char text[32];
        std::snprintf(text, sizeof(text), bytes >= 100.0 || unit == 0 ? "%.0f %s" : "%.1f %s", bytes, units[unit]);
        return text;
    }
}
