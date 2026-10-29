#include "bake.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace planetbake
{
    namespace fs = std::filesystem;

    namespace
    {
        // Typical bytes a pixel on disk for PNG output (terrain colour compresses to about half, a smooth gloss map to almost nothing).
        double png_bytes_per_pixel(int layer)
        {
            switch (layer)
            {
            case layer_colour:
                return 1.6;
            case layer_height:
                return 1.0;
            case layer_normal:
                return 2.0;
            case layer_specular:
                return 0.15;
            default:
                return 0.1;
            }
        }

        bool dds_layer(const Job &job, int layer) { return job.format == Format::dds && layer != layer_height; }

        const char *extension(const Job &job, int layer) { return dds_layer(job, layer) ? ".dds" : ".png"; }

        LayerSet usable_layers(const Job &job, const World &world)
        {
            LayerSet set;
            for (int i = 0; i < layer_count; ++i)
                set.on[i] = job.layers.on[i] && world.applicable(i);
            return set;
        }

        // The layers a job asks for that mean something for this world, without building the world.
        LayerSet usable_layers(const Job &job)
        {
            LayerSet set;
            const kosmos::PlanetStyle &style = job.style;
            for (int i = 0; i < layer_count; ++i)
            {
                const bool applicable = i == layer_colour ? style.valid() : style.has_ground() && (i != layer_night || style.type == kosmos::Archetype::earth);
                set.on[i] = job.layers.on[i] && applicable;
            }
            return set;
        }

        int tile_levels(int width)
        {
            int levels = 1;
            while (tile_size * 2 * (1 << (levels - 1)) < width && levels < 7)
                ++levels;
            return std::max(levels, 3); // level 2 is 4096 wide
        }

        void put_row(int layer, const Rendered &r, int y, std::vector<std::uint8_t> &line)
        {
            const int w = r.w;
            if (layer == layer_height)
            {
                line.resize(static_cast<std::size_t>(w) * 2);
                const std::uint16_t *source = r.height.data() + static_cast<std::size_t>(y) * w;
                for (int x = 0; x < w; ++x)
                {
                    line[x * 2] = static_cast<std::uint8_t>(source[x] >> 8);
                    line[x * 2 + 1] = static_cast<std::uint8_t>(source[x]);
                }
                return;
            }
            const std::uint8_t *source = r.layer[layer].data() + static_cast<std::size_t>(y) * w * 4;
            if (layer == layer_specular)
            {
                line.resize(static_cast<std::size_t>(w));
                for (int x = 0; x < w; ++x)
                    line[x] = source[x * 4];
                return;
            }
            line.resize(static_cast<std::size_t>(w) * 3);
            for (int x = 0; x < w; ++x)
                std::copy_n(source + x * 4, 3, line.data() + x * 3);
        }

        PngKind png_kind(int layer) { return layer == layer_height ? PngKind::gray16 : layer == layer_specular ? PngKind::gray8 : PngKind::rgb8; }

        bool write_whole(const Job &job, int layer, const Rendered &r, const fs::path &final_path)
        {
            const fs::path part = final_path.string() + ".part";
            bool ok;
            if (dds_layer(job, layer))
                ok = write_dds(part.string(), r.layer[layer].data(), r.w, r.h, job.dds, true);
            else
            {
                PngWriter writer;
                std::vector<std::uint8_t> line;
                ok = writer.open(part.string(), r.w, r.h, png_kind(layer));
                for (int y = 0; ok && y < r.h; ++y)
                {
                    put_row(layer, r, y, line);
                    ok = writer.row(line.data());
                }
                ok = writer.close() && ok;
            }
            if (!ok)
                return false;
            std::error_code ec;
            fs::rename(part, final_path, ec);
            return !ec;
        }

        void write_info(const Job &job, const World &world, const LayerSet &layers, int width, const fs::path &path)
        {
            const kosmos::PlanetStyle &s = job.style;
            std::ostringstream text;
            text.precision(9);
            text << "{\n  \"generator\": \"planetbake\",\n  \"type\": \"" << kosmos::archetype_name(s.type) << "\",\n  \"seed\": " << s.seed
                 << ",\n  \"ocean\": " << s.ocean << ",\n  \"ice\": " << s.ice << ",\n  \"relief\": " << s.relief << ",\n  \"tint\": [" << s.tint[0] << ", "
                 << s.tint[1] << ", " << s.tint[2] << "],\n  \"bands\": " << s.bands << ",\n  \"contrast\": " << s.contrast << ",\n  \"radius_m\": " << job.radius
                 << ",\n  \"width\": " << width << ",\n  \"height\": " << width / 2 << ",\n  \"height_min_m\": " << world.lowest() << ",\n  \"height_max_m\": "
                 << world.highest() << ",\n  \"height_encoding\": \"16 bit PNG, linear from height_min_m (0) to height_max_m (65535)\""
                 << ",\n  \"normal_encoding\": \"RGB = 0.5 + 0.5 * (east, north, up) of the unit normal; strength " << job.normal_strength << "\""
                 << ",\n  \"layers\": [";
            bool first = true;
            for (int i = 0; i < layer_count; ++i)
                if (layers.on[i])
                {
                    text << (first ? "" : ", ") << "\"" << layer_name(i) << "\"";
                    first = false;
                }
            text << "],\n  \"format\": \"" << (job.format == Format::dds ? std::string("dds ") + dds_kind_name(job.dds) : std::string("png")) << "\"\n}\n";
            std::ofstream file(path);
            file << text.str();
        }

        bool run_image(const Job &job, const World &world, const LayerSet &layers, const Progress &progress, std::string &error)
        {
            const int w = effective_width(job), h = w / 2;
            const std::string stem = job.name.empty() ? std::string(kosmos::archetype_name(job.style.type)) + "-" + std::to_string(job.style.seed) : job.name;
            struct Output
            {
                int layer;
                fs::path final_path, part;
                PngWriter png;
                std::vector<std::uint8_t> whole; // a DDS layer is kept whole, for its mip levels
                Rendered *held = nullptr;
            };
            std::vector<std::unique_ptr<Output>> outputs;
            std::error_code ec;
            for (int i = 0; i < layer_count; ++i)
            {
                if (!layers.on[i])
                    continue;
                auto output = std::make_unique<Output>();
                output->layer = i;
                output->final_path = fs::path(job.out) / (stem + "_" + layer_name(i) + extension(job, i));
                output->part = output->final_path.string() + ".part";
                if (dds_layer(job, i))
                    output->whole.assign(static_cast<std::size_t>(w) * h * 4, 0);
                else if (!output->png.open(output->part.string(), w, h, png_kind(i)))
                {
                    error = "cannot write " + output->part.string();
                    return false;
                }
                outputs.push_back(std::move(output));
            }
            const int rows = std::clamp((1 << 22) / w, 8, 256);
            Rendered rendered;
            std::vector<std::uint8_t> line;
            for (int y0 = 0; y0 < h; y0 += rows)
            {
                const int n = std::min(rows, h - y0);
                const Region region{0.0, static_cast<double>(y0) / h, 1.0, static_cast<double>(y0 + n) / h, w, n};
                if (!render_region(world, region, layers, rendered, job.threads, error))
                    return false;
                for (auto &output : outputs)
                {
                    if (dds_layer(job, output->layer))
                    {
                        std::copy(rendered.layer[output->layer].begin(), rendered.layer[output->layer].end(), output->whole.begin() + static_cast<std::ptrdiff_t>(y0) * w * 4);
                        continue;
                    }
                    for (int y = 0; y < n; ++y)
                    {
                        put_row(output->layer, rendered, y, line);
                        if (!output->png.row(line.data()))
                        {
                            error = "cannot write " + output->part.string() + " (disk full?)";
                            return false;
                        }
                    }
                }
                if (progress && !progress(static_cast<double>(y0 + n) / h * 0.9, "Rendering rows " + std::to_string(y0 + n) + " of " + std::to_string(h)))
                {
                    error = "stopped";
                    return false;
                }
            }
            int done = 0;
            for (auto &output : outputs)
            {
                bool ok;
                if (dds_layer(job, output->layer))
                {
                    if (progress)
                        progress(0.9 + 0.1 * done / static_cast<double>(outputs.size()), std::string("Compressing ") + layer_name(output->layer));
                    ok = write_dds(output->part.string(), output->whole.data(), w, h, job.dds, true);
                    std::vector<std::uint8_t>().swap(output->whole);
                }
                else
                    ok = output->png.close();
                if (ok)
                    fs::rename(output->part, output->final_path, ec);
                if (!ok || ec)
                {
                    error = "cannot write " + output->final_path.string() + " (disk full?)";
                    return false;
                }
                ++done;
            }
            write_info(job, world, layers, w, fs::path(job.out) / (stem + "_info.json"));
            if (progress)
                progress(1.0, "Done");
            return true;
        }

        fs::path tile_path(const Job &job, int layer, int level, int column, int row)
        {
            return fs::path(job.out) / layer_name(layer) / ("level" + std::to_string(level)) / ("tx_" + std::to_string(column) + "_" + std::to_string(row) + extension(job, layer));
        }

        bool run_tiles(const Job &job, const World &world, const LayerSet &layers, const Progress &progress, std::string &error)
        {
            const int levels = tile_levels(job.width);
            const int width = tile_size * 2 * (1 << (levels - 1));
            std::int64_t total = 0;
            for (int level = 0; level < levels; ++level)
                total += static_cast<std::int64_t>(2) << (2 * level);
            std::error_code ec;
            for (int i = 0; i < layer_count; ++i)
                if (layers.on[i])
                    for (int level = 0; level < levels; ++level)
                        fs::create_directories(fs::path(job.out) / layer_name(i) / ("level" + std::to_string(level)), ec);
            Rendered rendered;
            std::int64_t done = 0;
            for (int level = 0; level < levels; ++level)
            {
                const int rows = 1 << level, cols = rows * 2;
                for (int row = 0; row < rows; ++row)
                    for (int column = 0; column < cols; ++column)
                    {
                        LayerSet missing;
                        for (int i = 0; i < layer_count; ++i)
                            missing.on[i] = layers.on[i] && !fs::exists(tile_path(job, i, level, column, row), ec);
                        if (missing.any())
                        {
                            // The reader counts longitude the other way round from the generator's maps: tile u runs as 0.5 - u.
                            const Region region{0.5 - static_cast<double>(column) / cols, static_cast<double>(row) / rows, 0.5 - static_cast<double>(column + 1) / cols,
                                                static_cast<double>(row + 1) / rows, tile_size, tile_size};
                            if (!render_region(world, region, missing, rendered, job.threads, error))
                                return false;
                            for (int i = 0; i < layer_count; ++i)
                                if (missing.on[i] && !write_whole(job, i, rendered, tile_path(job, i, level, column, row)))
                                {
                                    error = "cannot write " + tile_path(job, i, level, column, row).string() + " (disk full?)";
                                    return false;
                                }
                        }
                        ++done;
                        if (progress && !progress(static_cast<double>(done) / static_cast<double>(total),
                                                   "Level " + std::to_string(level) + ", tile " + std::to_string(done) + " of " + std::to_string(total)))
                        {
                            error = "stopped";
                            return false;
                        }
                    }
            }
            for (int i = 0; i < layer_count; ++i)
                if (layers.on[i])
                {
                    std::ofstream ctx(fs::path(job.out) / layer_name(i) / "tiles.ctx");
                    ctx << "VirtualTexture\n{\n    ImageDirectory \".\"\n    BaseSplit 0\n    TileSize " << tile_size << "\n    TileType \""
                        << (dds_layer(job, i) ? "dds" : "png") << "\"\n}\n";
                }
            write_info(job, world, layers, width, fs::path(job.out) / "info.json");
            return true;
        }
    }

    int effective_width(const Job &job)
    {
        if (job.tiles)
            return tile_size * 2 * (1 << (tile_levels(job.width) - 1));
        return std::clamp(job.width, 256, max_image_width) & ~1;
    }

    Estimate estimate(const Job &job)
    {
        Estimate e;
        const LayerSet layers = usable_layers(job);
        e.width = effective_width(job);
        e.height = e.width / 2;
        if (job.tiles)
        {
            e.levels = tile_levels(job.width);
            for (int level = 0; level < e.levels; ++level)
                e.tiles += static_cast<std::int64_t>(2) << (2 * level);
        }
        const double pixels = job.tiles ? static_cast<double>(e.tiles) * tile_size * tile_size : static_cast<double>(e.width) * e.height;
        for (int i = 0; i < layer_count; ++i)
            if (layers.on[i])
            {
                ++e.layer_count;
                e.disk_bytes += pixels * (dds_layer(job, i) ? dds_bytes_per_pixel(job.dds) : png_bytes_per_pixel(i));
                if (!job.tiles && dds_layer(job, i))
                    e.memory_bytes += static_cast<double>(e.width) * e.height * 4.0 * 1.34; // kept whole, plus the mip levels while compressing
            }
        // Working memory of the strip or tile being made.
        e.memory_bytes += job.tiles ? 64.0 * (1 << 20) : 256.0 * (1 << 20);
        if (job.tiles && job.width > max_tiles_width)
        {
            e.fits = false;
            e.problem = "tile pyramids go up to " + std::to_string(max_tiles_width) + " pixels wide";
        }
        else if (!job.tiles && job.width > max_image_width)
        {
            e.fits = false;
            e.problem = "single images go up to " + std::to_string(max_image_width) + " pixels wide; use a tile pyramid for more";
        }
        else if (e.memory_bytes > static_cast<double>(job.memory_mb) * (1 << 20))
        {
            e.fits = false;
            e.problem = "needs about " + format_bytes(e.memory_bytes) + " of memory, over the " + std::to_string(job.memory_mb) + " MB limit (use PNG, fewer layers or a tile pyramid)";
        }
        return e;
    }

    bool run(const Job &job, const Progress &progress, std::string &error)
    {
        error.clear();
        if (!job.style.valid())
        {
            error = "no world type";
            return false;
        }
        if (job.out.empty())
        {
            error = "no output folder";
            return false;
        }
        const LayerSet wanted = usable_layers(job);
        if (!wanted.any())
        {
            error = "none of the chosen layers applies to this world";
            return false;
        }
        const Estimate e = estimate(job);
        if (!e.fits)
        {
            error = e.problem;
            return false;
        }
        std::error_code ec;
        fs::create_directories(job.out, ec);
        if (ec)
        {
            error = "cannot create " + job.out + ": " + ec.message();
            return false;
        }
        World world(job.style, job.radius, job.normal_strength);
        const LayerSet layers = usable_layers(job, world);
        return job.tiles ? run_tiles(job, world, layers, progress, error) : run_image(job, world, layers, progress, error);
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
        std::snprintf(text, sizeof(text), unit == 0 ? "%.0f %s" : "%.1f %s", bytes, units[unit]);
        return text;
    }
}
