#include "surface_textures.h"
#include "loading_screen.h"

namespace kosmos::app
{
    // A box-filtered halving, used when an image is larger than the GPU accepts.
    void halve_bitmap(sl::Bitmap &bitmap)
    {
        const int w = std::max(1, bitmap.width / 2), h = std::max(1, bitmap.height / 2);
        std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
        {
            if (loading.started && y % 32 == 0)
                loading.pump();
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            sum += bitmap.pixels[(static_cast<std::size_t>(std::min(y * 2 + dy, bitmap.height - 1)) * bitmap.width + std::min(x * 2 + dx, bitmap.width - 1)) * 4 + c];
                    out[(static_cast<std::size_t>(y) * w + x) * 4 + c] = static_cast<std::uint8_t>(sum / 4);
                }
        }
        bitmap.pixels = std::move(out);
        bitmap.width = w;
        bitmap.height = h;
    }

    // Loads a hi-res surface image onto the GPU, halving it until the backend accepts it; RAM is released afterwards.
    sl::Bitmap *finish_cpu_texture(sl::Bitmap *bitmap, const std::string &path, int max_size)
    {
        const int original_w = bitmap->width, original_h = bitmap->height;
        while (std::max(bitmap->width, bitmap->height) > max_size && bitmap->width > 1 && bitmap->height > 1)
            halve_bitmap(*bitmap);
        bitmap->smooth = true;
        bitmap->mipmaps = true;
        bitmap->anisotropy = 8.0f;
        while (!sl::upload_bitmap(bitmap))
        {
            if (bitmap->width <= 512)
            {
                std::fprintf(stderr, "[kosmos] could not upload %s\n", path.c_str());
                sl::destroy_bitmap(bitmap);
                return nullptr;
            }
            halve_bitmap(*bitmap);
        }
        std::fprintf(stderr, "[kosmos] surface texture %s: %dx%d (file %dx%d)\n", path.c_str(), bitmap->width, bitmap->height, original_w, original_h);
        bitmap->pixels.clear();
        bitmap->pixels.shrink_to_fit();
        bitmap->ram_dirty = false;
        return bitmap;
    }

    sl::Bitmap *load_surface_texture(const std::string &path, int max_size)
    {
        const bool is_dds = path.size() > 4 && !std::strcmp(path.c_str() + path.size() - 4, ".dds");
        if (is_dds)
        {
            sl::DdsOptions options;
            options.max_size = max_size;
            options.anisotropy = 8.0f;
            sl::Bitmap *bitmap = sl::load_dds(path, options);
            if (bitmap && bitmap->compressed)
            {
                std::fprintf(stderr, "[kosmos] surface texture %s: %dx%d compressed on the GPU\n", path.c_str(), bitmap->width, bitmap->height);
                return bitmap;
            }
            if (!bitmap)
            {
                std::fprintf(stderr, "[kosmos] could not load %s\n", path.c_str());
                return nullptr;
            }
            return finish_cpu_texture(bitmap, path, max_size);
        }
        sl::Bitmap *bitmap = sl::load_bitmap(path);
        if (!bitmap)
        {
            std::fprintf(stderr, "[kosmos] could not load %s (%s)\n", path.c_str(), sl::last_error().c_str());
            return nullptr;
        }
        return finish_cpu_texture(bitmap, path, max_size);
    }

    // Night lights are a few bright pixels on black, and mip averaging leaves almost nothing of them from orbit. The image is read at full
    // size, then reduced by `factor` keeping the brightest texel of each block (lifted by the block's mean), so cities survive the reduction.
    sl::Bitmap *load_night_lights(const std::string &path, int max_size)
    {
        if (!std::filesystem::exists(path))
            return nullptr;
        sl::DdsOptions options;
        options.allow_compressed_upload = false;
        sl::Bitmap *bitmap = path.size() > 4 && !std::strcmp(path.c_str() + path.size() - 4, ".dds") ? sl::load_dds(path, options) : sl::load_bitmap(path);
        if (!bitmap || bitmap->pixels.empty())
        {
            if (bitmap)
                sl::destroy_bitmap(bitmap);
            return load_optional_texture(path, max_size);
        }
        const int factor = std::max(1, bitmap->width / std::max(1, std::min(max_size, 4096)));
        if (factor > 1)
        {
            const int w = bitmap->width / factor, h = bitmap->height / factor;
            std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
            for (int y = 0; y < h; ++y)
            {
                if (loading.started && y % 32 == 0)
                    loading.pump();
                for (int x = 0; x < w; ++x)
                {
                    int peak[3] = {0, 0, 0}, floor_[3] = {255, 255, 255}, sum[3] = {0, 0, 0};
                    for (int dy = 0; dy < factor; ++dy)
                        for (int dx = 0; dx < factor; ++dx)
                        {
                            const std::uint8_t *px = &bitmap->pixels[(static_cast<std::size_t>(y * factor + dy) * bitmap->width + x * factor + dx) * 4];
                            for (int c = 0; c < 3; ++c)
                            {
                                peak[c] = std::max<int>(peak[c], px[c]);
                                floor_[c] = std::min<int>(floor_[c], px[c]);
                                sum[c] += px[c];
                            }
                        }
                    std::uint8_t *dst = &out[(static_cast<std::size_t>(y) * w + x) * 4];
                    // Flat non-black cells are damaged blocks in the source (real lights are never uniform), so they are dropped.
                    if (peak[0] == floor_[0] && peak[1] == floor_[1] && peak[2] == floor_[2])
                    {
                        dst[0] = dst[1] = dst[2] = 0;
                        dst[3] = 255;
                        continue;
                    }
                    for (int c = 0; c < 3; ++c)
                        dst[c] = static_cast<std::uint8_t>(std::min(255, peak[c] / 2 + sum[c] * 2 / (factor * factor)));
                    dst[3] = 255;
                }
            }
            bitmap->pixels = std::move(out);
            bitmap->width = w;
            bitmap->height = h;
        }
        bitmap->smooth = true;
        bitmap->mipmaps = true;
        bitmap->anisotropy = 4.0f;
        if (!sl::upload_bitmap(bitmap))
        {
            sl::destroy_bitmap(bitmap);
            return nullptr;
        }
        bitmap->pixels.clear();
        bitmap->pixels.shrink_to_fit();
        bitmap->ram_dirty = false;
        return bitmap;
    }

    sl::Bitmap *load_optional_texture(const std::string &path, int max_size)
    {
        return std::filesystem::exists(path) ? load_surface_texture(path, max_size) : nullptr;
    }

    std::string find_texture_in_directory(const std::string &root, const std::string &directory, const std::string &name,
                                          std::initializer_list<const char *> extensions)
    {
        namespace fs = std::filesystem;
        for (const char *extension : extensions)
        {
            const std::string path = root + directory + name + extension;
            if (fs::exists(path))
                return path;
        }
        return {};
    }

    std::string find_optional_texture(const std::string &root, const std::string &directory, const std::string &name,
                                      std::initializer_list<const char *> extensions)
    {
        const std::string user = kosmos::Sim::user_dir();
        if (!user.empty())
        {
            const std::string path = find_texture_in_directory(user + "/scenery", directory, name, extensions);
            if (!path.empty())
                return path;
        }
        return find_texture_in_directory(root, directory, name, extensions);
    }

    // A texture by name: data/textures-hires/ (optional, not in the repository) wins over the default set in data/textures/.
    std::string find_texture_file(const std::string &root, const std::string &name, std::initializer_list<const char *> extensions)
    {
        const std::string optional = find_optional_texture(root, "/data/textures-hires/", name, extensions);
        return optional.empty() ? find_texture_in_directory(root, "/data/textures/", name, extensions) : optional;
    }

    // The first .ctx in a Celestia virtual-texture folder, or the folder itself when it has none. Empty if the folder is missing.
    std::string find_tile_source(const std::string &directory)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(directory, ec))
            return {};
        for (const fs::directory_entry &entry : fs::directory_iterator(directory, ec))
            if (entry.path().extension() == ".ctx")
                return entry.path().string();
        return directory;
    }

    std::string find_image_in_folder(const std::string &directory, const std::string &prefer)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(directory, ec))
            return {};
        std::vector<fs::path> images;
        for (const fs::directory_entry &entry : fs::directory_iterator(directory, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;
            const std::string extension = entry.path().extension().string();
            std::string filename = entry.path().filename().string();
            for (char &c : filename)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            // Skip bump maps, cloud and ring layers, and "_b" variants; they are not the colour map.
            if ((extension == ".dds" || extension == ".png" || extension == ".jpg" || extension == ".jpeg") &&
                filename.find("bump") == std::string::npos && filename.find("cloud") == std::string::npos &&
                filename.find("ring") == std::string::npos && filename.find("_b.") == std::string::npos)
                images.push_back(entry.path());
        }
        std::sort(images.begin(), images.end());
        // A folder may hold several bodies (Pluto and Charon); favour the file named for this one.
        std::stable_partition(images.begin(), images.end(), [&](const fs::path &path) {
            std::string name = path.filename().string();
            for (char &c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return !prefer.empty() && name.find(prefer) != std::string::npos;
        });
        return images.empty() ? std::string{} : images.front().string();
    }

    std::string find_optional_tile_source(const std::string &root, const std::string &directory)
    {
        const std::string user = kosmos::Sim::user_dir();
        if (!user.empty())
        {
            const std::string path = find_tile_source(user + "/scenery" + directory);
            if (!path.empty())
            {
                std::error_code ec;
                const bool empty_directory = std::filesystem::is_directory(path, ec) && std::filesystem::is_empty(path, ec);
                if (ec)
                    std::fprintf(stderr, "[kosmos] could not inspect scenery %s: %s\n", path.c_str(), ec.message().c_str());
                if (!empty_directory)
                    return path;
            }
        }
        return find_tile_source(root + directory);
    }

    std::string find_optional_image_folder(const std::string &root, const std::string &directory, const std::string &prefer)
    {
        const std::string user = kosmos::Sim::user_dir();
        if (!user.empty())
        {
            const std::string path = find_image_in_folder(user + "/scenery" + directory, prefer);
            if (!path.empty())
                return path;
        }
        return find_image_in_folder(root + directory, prefer);
    }

    int generated_texture_width = 0;

    int generated_target_width(const kosmos::Body &b)
    {
        const int width = generated_texture_width > 0 ? generated_texture_width : b.radius > 2.0e6 ? 2048 : 1024;
        return std::max(width, 512) & ~1;
    }

    std::vector<int> generated_missing_widths(const kosmos::Body &b)
    {
        const int target = generated_target_width(b);
        std::vector<int> missing;
        for (const int width : {std::min(2048, target), target})
        {
            std::error_code ec;
            if (width > 512 && std::find(missing.begin(), missing.end(), width) == missing.end() &&
                !std::filesystem::exists(kosmos::far_image_path(b.style, width), ec))
                missing.push_back(width);
        }
        return missing;
    }

    // The best far-view image of an invented world that is already cached, up to the chosen width. If there is none, a small one
    // is made on the spot so there is always something to show; the larger ones are filled in later in the background.
    std::string generated_surface_image(const kosmos::Body &b)
    {
        const int target = generated_target_width(b);
        std::error_code ec;
        for (const int width : {target, 16384, 8192, 4096, 2048, 1024, 512})
        {
            if (width > target)
                continue;
            const std::filesystem::path cached = kosmos::far_image_path(b.style, width);
            if (std::filesystem::exists(cached, ec))
                return cached.string();
        }
        const int width = 512, height = width / 2;
        const std::filesystem::path path = kosmos::far_image_path(b.style, width);
        loading.note("Generating the surface of " + b.name);
        const kosmos::PlanetSurface surface(b.style, b.radius);
        sl::Bitmap *bitmap = surface.make_texture(width, height);
        if (!bitmap)
            return {};
        // Written under a temporary name first so a crash never leaves a half-written image in the cache.
        const std::string partial = path.string() + ".part.png";
        const bool saved = sl::save_bitmap(bitmap, partial);
        sl::destroy_bitmap(bitmap);
        if (!saved)
            return {};
        std::filesystem::rename(partial, path, ec);
        return ec ? std::string{} : path.string();
    }

    std::string find_surface_image(const std::string &root, const kosmos::Body &b)
    {
        if (!b.texture.empty())
            return root + "/data/" + b.texture;
        std::string lower = b.name;
        for (char &c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string override = find_optional_texture(root, "/data/textures-hires/", lower, {".dds", ".png", ".jpg", ".jpeg"});
        if (!override.empty())
            return override;
        const std::string celestia = find_optional_image_folder(root, "/import/celestia/hires/" + lower, lower);
        if (!celestia.empty())
            return celestia;
        const std::string generated = find_texture_in_directory(root, "/data/textures-generated/", lower, {".dds", ".png", ".jpg", ".jpeg"});
        if (!generated.empty())
            return generated;
        const std::string shipped = find_texture_in_directory(root, "/data/textures/", lower, {".dds", ".png", ".jpg", ".jpeg"});
        if (shipped.empty() && b.style.valid() && !b.minor)
            return generated_surface_image(b);
        return shipped;
    }
} // namespace kosmos::app
