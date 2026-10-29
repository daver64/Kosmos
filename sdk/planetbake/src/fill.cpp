#include "fill.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <thread>

#include "settings.h"

namespace planetbake
{
    namespace fs = std::filesystem;

    std::int64_t FillReport::written() const
    {
        std::int64_t n = from_parent + generated;
        for (std::int64_t v : from_source)
            n += v;
        return n;
    }

    namespace
    {
        std::string lower(std::string s)
        {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool contains(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

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

        // Only characters that can sit safely inside a double quoted shell argument.
        bool safe_url(const std::string &url)
        {
            if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0)
                return false;
            for (char c : url)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || std::string("._~:/?#@!&'()*+,;=%-").find(c) != std::string::npos))
                    return false;
            return true;
        }

        bool safe_path(const std::string &p)
        {
            for (char c : p)
                if (c == '"' || c == '$' || c == '`' || c == '\\' || c == '\n')
                    return false;
            return true;
        }

        template <class F>
        void parallel_for(int n, unsigned threads, F fn)
        {
            threads = std::clamp(threads, 1u, max_threads);
            std::atomic<int> next{0};
            auto work = [&] {
                for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1))
                    fn(i);
            };
            std::vector<std::thread> pool;
            for (unsigned t = 1; t < threads && t < static_cast<unsigned>(n); ++t)
                pool.emplace_back(work);
            work();
            for (std::thread &t : pool)
                t.join();
        }

        struct Buffer
        {
            int w = 0, h = 0;
            std::vector<std::uint8_t> rgba;
            std::vector<std::uint16_t> gray; // heights, in the pack's encoding
        };

        // Bilinear lookup in an equirectangular image: x wraps, y clamps.
        struct Lookup
        {
            const Image &img;
            bool height;
            void at(double sx, double sy, float out[4]) const
            {
                sx -= 0.5;
                sy -= 0.5;
                const double fx = std::floor(sx), fy = std::floor(sy);
                const double tx = sx - fx, ty = sy - fy;
                int x0 = static_cast<int>(std::fmod(fx, static_cast<double>(img.w))), y0 = static_cast<int>(fy);
                if (x0 < 0)
                    x0 += img.w;
                const int x1 = (x0 + 1) % img.w;
                const int y1 = std::clamp(y0 + 1, 0, img.h - 1);
                y0 = std::clamp(y0, 0, img.h - 1);
                const std::size_t i00 = static_cast<std::size_t>(y0) * img.w + x0, i10 = static_cast<std::size_t>(y0) * img.w + x1, i01 = static_cast<std::size_t>(y1) * img.w + x0,
                                  i11 = static_cast<std::size_t>(y1) * img.w + x1;
                auto mix = [&](double a, double b, double c, double d) { return static_cast<float>((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty); };
                if (height)
                {
                    if (!img.gray.empty())
                        out[0] = mix(img.gray[i00], img.gray[i10], img.gray[i01], img.gray[i11]) / 65535.0f;
                    else
                        out[0] = mix(img.rgba[i00 * 4], img.rgba[i10 * 4], img.rgba[i01 * 4], img.rgba[i11 * 4]) / 255.0f;
                    return;
                }
                for (int c = 0; c < 4; ++c)
                    out[c] = mix(img.rgba[i00 * 4 + c], img.rgba[i10 * 4 + c], img.rgba[i01 * 4 + c], img.rgba[i11 * 4 + c]);
            }
        };

        // One tile from a source image. For heights, `fraction` gets 0-1 values and rgba stays empty.
        void sample_source(const Image &img, bool height, double u0, double v0, double u1, double v1, int w, int h, unsigned threads, std::vector<std::uint8_t> &rgba,
                           std::vector<float> &fraction)
        {
            if (height)
                fraction.assign(static_cast<std::size_t>(w) * h, 0.0f);
            else
                rgba.assign(static_cast<std::size_t>(w) * h * 4, 255);
            const double span = std::fabs(u1 - u0) * img.w / w;
            const int k = std::clamp(static_cast<int>(std::ceil(span)), 1, 4);
            const Lookup lookup{img, height};
            parallel_for(h, threads, [&](int y) {
                for (int x = 0; x < w; ++x)
                {
                    float sum[4] = {0, 0, 0, 0};
                    for (int j = 0; j < k; ++j)
                        for (int i = 0; i < k; ++i)
                        {
                            const double u = u0 + (u1 - u0) * ((x + (i + 0.5) / k) / w);
                            const double v = v0 + (v1 - v0) * ((y + (j + 0.5) / k) / h);
                            const double fu = u - std::floor(u);
                            float px[4] = {0, 0, 0, 0};
                            lookup.at(fu * img.w, v * img.h, px);
                            for (int c = 0; c < 4; ++c)
                                sum[c] += px[c];
                        }
                    const float inv = 1.0f / static_cast<float>(k * k);
                    const std::size_t index = static_cast<std::size_t>(y) * w + x;
                    if (height)
                        fraction[index] = sum[0] * inv;
                    else
                        for (int c = 0; c < 4; ++c)
                            rgba[index * 4 + c] = static_cast<std::uint8_t>(std::clamp(sum[c] * inv + 0.5f, 0.0f, 255.0f));
                }
            });
        }

        // A quarter of a coarser tile blown up to a full tile.
        void upsample_parent(const Image &parent, int quadrant_x, int quadrant_y, int w, int h, Buffer &out, bool height)
        {
            out.w = w;
            out.h = h;
            if (height)
                out.gray.assign(static_cast<std::size_t>(w) * h, 0);
            else
                out.rgba.assign(static_cast<std::size_t>(w) * h * 4, 255);
            const Lookup lookup{parent, height};
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const double sx = (quadrant_x * w + x + 0.5) * 0.5 * parent.w / w, sy = (quadrant_y * h + y + 0.5) * 0.5 * parent.h / h;
                    float px[4] = {0, 0, 0, 0};
                    lookup.at(sx, sy, px);
                    const std::size_t index = static_cast<std::size_t>(y) * w + x;
                    if (height)
                        out.gray[index] = static_cast<std::uint16_t>(std::clamp(px[0] * 65535.0f + 0.5f, 0.0f, 65535.0f));
                    else
                        for (int c = 0; c < 4; ++c)
                            out.rgba[index * 4 + c] = static_cast<std::uint8_t>(std::clamp(px[c] + 0.5f, 0.0f, 255.0f));
                }
        }

        // The layer a file name says it is; false for files to ignore.
        bool classify_name(const std::string &name_in, int &layer)
        {
            const std::string name = lower(name_in);
            layer = layer_colour;
            if (contains(name, "thumb") || contains(name, "preview") || contains(name, "icon"))
                return false;
            if (contains(name, "cloud"))
                layer = layer_clouds;
            else if (contains(name, "night") || contains(name, "light"))
                layer = layer_night;
            else if (contains(name, "spec") || contains(name, "gloss"))
                layer = layer_specular;
            else if (contains(name, "normal"))
                layer = layer_normal;
            else if (contains(name, "height") || contains(name, "elev") || contains(name, "dem") || contains(name, "disp") || contains(name, "bump"))
                layer = layer_height;
            return true;
        }

        bool image_extension(const std::string &e) { return e == ".png" || e == ".dds" || e == ".jpg" || e == ".jpeg" || e == ".tif" || e == ".tiff"; }

        bool resolve_image(const std::string &path, std::string &resolved, std::string &error)
        {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec))
            {
                error = path + " is not a file";
                return false;
            }
            if (!safe_path(path))
            {
                error = "the file name has characters that are not allowed";
                return false;
            }
            if (native_image(path))
            {
                resolved = path;
                return true;
            }
            resolved = convert_to_png(path, (fs::path(cache_dir()) / "converted").string(), error);
            return !resolved.empty();
        }
    }

    std::vector<CatalogEntry> catalogue()
    {
        std::vector<CatalogEntry> list;
        auto add = [&](const char *name, const char *layer, const std::string &url, const char *file, const char *note, bool range = false, double lo = 0, double hi = 0) {
            CatalogEntry e;
            e.name = name;
            e.layer = layer;
            e.url = url;
            e.file = file;
            e.note = note;
            e.has_range = range;
            e.height_min = lo;
            e.height_max = hi;
            list.push_back(e);
        };
        const std::string base = "https://eoimages.gsfc.nasa.gov/images/imagerecords/";
        add("bluemarble", "colour", base + "73000/73909/world.topo.bathy.200412.3x21600x10800.jpg", "bluemarble-21600.jpg",
            "NASA Blue Marble Next Generation with topography and bathymetry, 21600 x 10800, about 30 MB; NASA imagery, free to use with credit");
        add("bluemarble-small", "colour", base + "73000/73909/world.topo.bathy.200412.3x5400x2700.jpg", "bluemarble-5400.jpg", "The same at 5400 x 2700, about 2.5 MB");
        add("gebco", "height", base + "73000/73934/gebco_08_rev_elev_21600x10800.png", "gebco-elev-21600.png",
            "NASA's greyscale render of GEBCO elevations, 21600 x 10800; the metre range is approximate", true, -10994.0, 8848.0);
        add("blackmarble", "night", base + "79000/79765/dnb_land_ocean_ice.2012.3600x1800.jpg", "blackmarble-3600.jpg", "NASA Earth at Night 2012, 3600 x 1800");

        std::ifstream f(fs::path(settings_dir()) / "sources.json");
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        for (std::size_t open = text.find('{'); open != std::string::npos; open = text.find('{', open + 1))
        {
            const std::size_t close = text.find('}', open);
            if (close == std::string::npos)
                break;
            const std::string object = text.substr(open, close - open);
            CatalogEntry e;
            e.name = json_string(object, "name");
            e.layer = json_string(object, "layer");
            e.url = json_string(object, "url");
            e.file = json_string(object, "file");
            e.note = json_string(object, "note");
            double lo, hi;
            if (json_number(object, "height_min_m", lo) && json_number(object, "height_max_m", hi))
            {
                e.has_range = true;
                e.height_min = lo;
                e.height_max = hi;
            }
            if (!e.name.empty() && !e.url.empty())
            {
                if (e.file.empty())
                    e.file = fs::path(e.url).filename().string();
                list.erase(std::remove_if(list.begin(), list.end(), [&](const CatalogEntry &o) { return o.name == e.name; }), list.end());
                list.push_back(e);
            }
            open = close;
        }
        return list;
    }

    bool download_source(const CatalogEntry &entry, SourceFile &out, const Progress &progress, std::string &error)
    {
        if (!safe_url(entry.url) || !safe_path(entry.file) || entry.file.find('/') != std::string::npos)
        {
            error = "the catalogue entry " + entry.name + " has a URL or file name that is not allowed";
            return false;
        }
        const fs::path dir = fs::path(cache_dir()) / "downloads";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path target = dir / entry.file;
        if (!fs::exists(target, ec))
        {
            if (progress)
                progress(0.0, "Downloading " + entry.name + " (" + entry.url + ")");
            const fs::path part = target.string() + ".part";
            const std::string command = "curl -L -f -sS --retry 2 -o \"" + part.string() + "\" \"" + entry.url + "\"";
            if (!safe_path(part.string()) || std::system(command.c_str()) != 0)
            {
                fs::remove(part, ec);
                error = "cannot download " + entry.url + " (is curl installed and the network up?)";
                return false;
            }
            fs::rename(part, target, ec);
            if (ec)
            {
                error = "cannot write " + target.string();
                return false;
            }
        }
        std::string resolved;
        if (!resolve_image(target.string(), resolved, error))
            return false;
        out.path = resolved;
        out.has_range = entry.has_range;
        out.height_min = entry.height_min;
        out.height_max = entry.height_max;
        return true;
    }

    bool add_source(Sources &sources, const std::string &argument, const Progress &progress, std::string &error)
    {
        std::string target = argument;
        int forced = -1;
        const std::size_t eq = argument.find('=');
        if (eq != std::string::npos && layer_from_name(argument.substr(0, eq)) >= 0)
        {
            forced = layer_from_name(argument.substr(0, eq));
            target = argument.substr(eq + 1);
        }
        std::error_code ec;
        if (!fs::exists(target, ec))
        {
            for (const CatalogEntry &e : catalogue())
                if (e.name == target)
                {
                    const int layer = forced >= 0 ? forced : layer_from_name(e.layer);
                    if (layer < 0)
                    {
                        error = "the catalogue entry " + e.name + " has no layer";
                        return false;
                    }
                    return download_source(e, sources.layer[layer], progress, error);
                }
            error = target + " is neither a file, a folder nor a catalogue name (see --list-sources)";
            return false;
        }
        if (fs::is_regular_file(target, ec))
        {
            int layer = forced;
            if (layer < 0)
                classify_name(fs::path(target).stem().string(), layer);
            std::string resolved;
            if (!resolve_image(target, resolved, error))
                return false;
            sources.layer[layer].path = resolved;
            return true;
        }
        // A folder: the biggest image for each layer, by its name.
        std::uintmax_t best[layer_count] = {};
        std::string chosen[layer_count];
        int budget = 4000;
        for (auto it = fs::recursive_directory_iterator(target, ec); it != fs::recursive_directory_iterator() && budget-- > 0; it.increment(ec))
        {
            if (ec || it.depth() > 2 || !it->is_regular_file())
                continue;
            if (!image_extension(lower(it->path().extension().string())))
                continue;
            int layer;
            if (!classify_name(it->path().stem().string(), layer))
                continue;
            const std::uintmax_t size = it->file_size(ec);
            if (size > best[layer])
            {
                best[layer] = size;
                chosen[layer] = it->path().string();
            }
        }
        int found = 0;
        for (int i = 0; i < layer_count; ++i)
        {
            if (chosen[i].empty() || (forced >= 0 && forced != i))
                continue;
            std::string resolved, err;
            if (!resolve_image(chosen[i], resolved, err))
            {
                error = err;
                return false;
            }
            sources.layer[i].path = resolved;
            ++found;
        }
        if (!found)
        {
            error = target + " holds no images to use (PNG, DDS, JPEG or TIFF files whose names say what they are)";
            return false;
        }
        return true;
    }

    bool fill_pack(Pack &pack, const FillOptions &options, const Progress &progress, FillReport &report, std::string &error)
    {
        report = FillReport();
        error.clear();
        std::error_code ec;
        const bool tiled = pack.tiled;
        const int target_level = tiled ? pack.max_level() : 0;
        if (tiled && target_level < 0)
        {
            error = "the pack has no tiles to base the fill on";
            return false;
        }

        std::unique_ptr<World> world;
        auto get_world = [&]() -> World & {
            if (!world)
                world = std::make_unique<World>(pack.style, pack.radius_m, 8.0f, 1.0f);
            return *world;
        };

        std::vector<int> work;
        for (int i = 0; i < layer_count; ++i)
        {
            if (!options.layers.on[i])
                continue;
            if (pack.layer[i].present || !options.sources.layer[i].path.empty() || (options.generate && get_world().applicable(i)))
                work.push_back(i);
        }
        if (work.empty())
        {
            error = "nothing to fill: no layers selected, or none that apply";
            return false;
        }

        std::int64_t total = 0;
        for (std::size_t n = 0; n < work.size(); ++n)
        {
            if (tiled)
                for (int level = 0; level <= target_level; ++level)
                    total += static_cast<std::int64_t>(2) << (2 * level);
            else
                total += 1;
        }
        std::int64_t done = 0;
        std::string provenance;

        for (int layer : work)
        {
            const bool height = layer == layer_height;
            const bool new_layer = !pack.layer[layer].present;
            const SourceFile &source = options.sources.layer[layer];
            const bool use_source = !source.path.empty();

            // Output format: the layer's own, else the job's (heights are always PNG, clouds need alpha).
            std::string ext = pack.layer[layer].ext;
            DdsKind kind = options.dds;
            if (ext.empty())
            {
                ext = options.format == Format::dds && !height ? ".dds" : ".png";
                if (layer == layer_clouds && kind == DdsKind::bc1)
                    kind = DdsKind::bc3;
            }
            else if (ext == ".dds")
            {
                const std::string probe = tiled ? pack.tile_path(layer, 0, 0, 0) : pack.layer[layer].file;
                if (fs::exists(probe, ec))
                    kind = dds_kind_of(probe);
            }
            const bool dds = ext == ".dds";

            Image source_image;
            if (use_source)
            {
                int sw = 0, sh = 0;
                if (image_size(source.path, sw, sh) && static_cast<double>(sw) * sh * 6.0 > static_cast<double>(options.memory_mb) * 1048576.0)
                {
                    error = source.path + " is too big for the memory limit (" + std::to_string(options.memory_mb) + " MB; raise --memory)";
                    return false;
                }
                if (!options.dry_run)
                {
                    if (progress)
                        progress(static_cast<double>(done) / static_cast<double>(total), "Reading " + source.path);
                    if (!read_image(source.path, source_image, error))
                        return false;
                }
            }

            // Heights go into the pack's encoding, which is set from the first source or the generator that has a range.
            if (height && !pack.has_range && !options.dry_run)
            {
                if (use_source && source.has_range)
                {
                    pack.height_min = source.height_min;
                    pack.height_max = source.height_max;
                }
                else if (options.generate && get_world().applicable(layer_height))
                {
                    pack.height_min = get_world().lowest();
                    pack.height_max = get_world().highest();
                }
                pack.has_range = pack.height_max > pack.height_min;
            }
            auto to_pack = [&](double metres) -> std::uint16_t {
                if (!pack.has_range)
                    return 0;
                return static_cast<std::uint16_t>(std::clamp((metres - pack.height_min) / (pack.height_max - pack.height_min), 0.0, 1.0) * 65535.0 + 0.5);
            };
            auto from_fraction = [&](float f) -> std::uint16_t {
                if (source.has_range && pack.has_range)
                    return to_pack(source.height_min + f * (source.height_max - source.height_min));
                return static_cast<std::uint16_t>(std::clamp(f * 65535.0f + 0.5f, 0.0f, 65535.0f));
            };

            Image parent;
            std::string parent_path;
            // The tile or image covering a region, and where it came from (0 source, 1 coarser level, 2 generator); false when nothing can supply it.
            auto produce = [&](double u0, double v0, double u1, double v1, int w, int h, const TileRef *tile, Buffer &out, int &origin) -> bool {
                out.w = w;
                out.h = h;
                out.rgba.clear();
                out.gray.clear();
                if (use_source)
                {
                    std::vector<float> fraction;
                    sample_source(source_image, height, u0, v0, u1, v1, w, h, options.threads, out.rgba, fraction);
                    if (height)
                    {
                        out.gray.resize(fraction.size());
                        for (std::size_t i = 0; i < fraction.size(); ++i)
                            out.gray[i] = from_fraction(fraction[i]);
                    }
                    origin = 0;
                    return true;
                }
                if (tile && !new_layer && tile->level > 0)
                {
                    const std::string path = pack.tile_path(layer, tile->level - 1, tile->column / 2, tile->row / 2);
                    std::string err;
                    if (fs::exists(path, ec) && (path == parent_path || read_image(path, parent, err)))
                    {
                        parent_path = path;
                        upsample_parent(parent, tile->column % 2, tile->row % 2, w, h, out, height);
                        origin = 1;
                        return true;
                    }
                }
                if (options.generate && get_world().applicable(layer))
                {
                    LayerSet one;
                    one.on[layer] = true;
                    Rendered r;
                    if (!render_region(get_world(), Region{u0, v0, u1, v1, w, h}, one, r, options.threads, error))
                        return false;
                    if (height)
                    {
                        out.gray.resize(r.height.size());
                        const double lo = get_world().lowest(), span = get_world().highest() - lo;
                        for (std::size_t i = 0; i < r.height.size(); ++i)
                            out.gray[i] = to_pack(lo + r.height[i] / 65535.0 * span);
                    }
                    else
                        out.rgba = std::move(r.layer[layer]);
                    origin = 2;
                    return true;
                }
                origin = -1;
                return false;
            };
            auto count = [&](int origin) {
                if (origin == 0)
                    ++report.from_source[layer];
                else if (origin == 1)
                    ++report.from_parent;
                else if (origin == 2)
                    ++report.generated;
            };
            auto would_use = [&](int level) { return use_source ? 0 : (!new_layer && level > 0) ? 1 : (options.generate && get_world().applicable(layer)) ? 2 : -1; };

            if (tiled)
            {
                if (!options.dry_run)
                    for (int level = 0; level <= target_level; ++level)
                        fs::create_directories(fs::path(pack.dir) / layer_name(layer) / ("level" + std::to_string(level)), ec);
                for (int level = 0; level <= target_level; ++level)
                {
                    const int rows = 1 << level, cols = rows * 2;
                    for (int row = 0; row < rows; ++row)
                        for (int column = 0; column < cols; ++column)
                        {
                            ++done;
                            const TileRef tile{level, column, row};
                            const std::string path = pack.tile_path(layer, level, column, row, ext.c_str());
                            if (fs::exists(pack.tile_path(layer, level, column, row, ".png"), ec) || fs::exists(pack.tile_path(layer, level, column, row, ".dds"), ec))
                            {
                                ++report.kept;
                                continue;
                            }
                            if (is_locked(pack, layer, tile))
                            {
                                ++report.locked;
                                continue;
                            }
                            if (options.dry_run)
                            {
                                const int origin = would_use(level);
                                if (origin < 0)
                                    ++report.no_data;
                                else
                                    count(origin);
                                continue;
                            }
                            if (progress && !progress(static_cast<double>(done) / static_cast<double>(total), std::string("Filling ") + layer_name(layer) + " level " + std::to_string(level) + ", tile " + std::to_string(column) + "," + std::to_string(row)))
                            {
                                error = "stopped";
                                return false;
                            }
                            Buffer buffer;
                            int origin = -1;
                            const double u0 = 0.5 - static_cast<double>(column) / cols, u1 = 0.5 - static_cast<double>(column + 1) / cols;
                            if (!produce(u0, static_cast<double>(row) / rows, u1, static_cast<double>(row + 1) / rows, tile_size, tile_size, &tile, buffer, origin))
                            {
                                if (!error.empty())
                                    return false;
                                ++report.no_data;
                                continue;
                            }
                            if (!write_layer_image(path, layer, buffer.rgba.empty() ? nullptr : buffer.rgba.data(), buffer.gray.empty() ? nullptr : buffer.gray.data(), tile_size, tile_size, dds, kind))
                            {
                                error = "cannot write " + path + " (disk full?)";
                                return false;
                            }
                            count(origin);
                        }
                }
                if (!options.dry_run && (new_layer || pack.layer[layer].levels < target_level + 1))
                {
                    pack.layer[layer].present = true;
                    pack.layer[layer].ext = ext;
                    pack.layer[layer].levels = target_level + 1;
                    const fs::path ctx = fs::path(pack.dir) / layer_name(layer) / "tiles.ctx";
                    if (!fs::exists(ctx, ec))
                    {
                        std::ofstream c(ctx);
                        c << "VirtualTexture\n{\n    ImageDirectory \".\"\n    BaseSplit 0\n    TileSize " << tile_size << "\n    TileType \"" << ext.substr(1) << "\"\n}\n";
                    }
                }
            }
            else
            {
                ++done;
                if (!new_layer)
                {
                    ++report.kept;
                    continue;
                }
                if (is_locked(pack, layer, TileRef{}))
                {
                    ++report.locked;
                    continue;
                }
                const int w = std::clamp(pack.width > 0 ? pack.width : 4096, 256, max_image_width) & ~1, h = w / 2;
                if (static_cast<double>(w) * h * 6.0 > static_cast<double>(options.memory_mb) * 1048576.0)
                {
                    error = "an image this wide needs more memory than the limit (" + std::to_string(options.memory_mb) + " MB)";
                    return false;
                }
                if (options.dry_run)
                {
                    const int origin = would_use(0);
                    if (origin < 0)
                        ++report.no_data;
                    else
                        count(origin);
                    continue;
                }
                if (progress && !progress(static_cast<double>(done) / static_cast<double>(total), std::string("Making ") + layer_name(layer)))
                {
                    error = "stopped";
                    return false;
                }
                Buffer buffer;
                int origin = -1;
                if (!produce(0.0, 0.0, 1.0, 1.0, w, h, nullptr, buffer, origin))
                {
                    if (!error.empty())
                        return false;
                    ++report.no_data;
                    continue;
                }
                const std::string path = pack.image_path(layer, ext.c_str());
                if (!write_layer_image(path, layer, buffer.rgba.empty() ? nullptr : buffer.rgba.data(), buffer.gray.empty() ? nullptr : buffer.gray.data(), w, h, dds, kind))
                {
                    error = "cannot write " + path;
                    return false;
                }
                pack.layer[layer].present = true;
                pack.layer[layer].ext = ext;
                pack.layer[layer].file = path;
                pack.layer[layer].w = w;
                pack.layer[layer].h = h;
                count(origin);
            }
            if (use_source)
                provenance += std::string(provenance.empty() ? "" : ",\n") + "    \"" + layer_name(layer) + "\": \"" + source.path + "\"";
        }

        if (!options.dry_run && report.written() > 0)
        {
            std::ostringstream text;
            text.precision(9);
            text << "{\n  \"note\": \"written by planetbake --fill; info.json is left as it was\",\n";
            if (pack.has_range)
                text << "  \"height_min_m\": " << pack.height_min << ",\n  \"height_max_m\": " << pack.height_max << ",\n";
            text << "  \"sources\": {\n" << provenance << "\n  }\n}\n";
            std::ofstream f(fs::path(pack.dir) / "planetbake-fill.json");
            f << text.str();
        }
        if (progress)
            progress(1.0, "Done");
        return true;
    }
}
