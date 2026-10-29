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
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            sum += bitmap.pixels[(static_cast<std::size_t>(std::min(y * 2 + dy, bitmap.height - 1)) * bitmap.width + std::min(x * 2 + dx, bitmap.width - 1)) * 4 + c];
                    out[(static_cast<std::size_t>(y) * w + x) * 4 + c] = static_cast<std::uint8_t>(sum / 4);
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

    // A texture by name: data/textures-hires/ (optional, not in the repository) wins over the default set in data/textures/.
    std::string find_texture_file(const std::string &root, const std::string &name, std::initializer_list<const char *> extensions)
    {
        for (const char *directory : {"/data/textures-hires/", "/data/textures/"})
        {
            const std::string path = find_texture_in_directory(root, directory, name, extensions);
            if (!path.empty())
                return path;
        }
        return {};
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

    int generated_texture_width = 0;

    // The far-view image of an invented world, made the first time it is needed and then kept. Empty if it cannot be written.
    std::string generated_surface_image(const kosmos::Body &b)
    {
        const bool large = b.radius > 2.0e6;
        const int width = generated_texture_width > 0 ? generated_texture_width : large ? 2048 : 1024, height = width / 2;
        const std::filesystem::path path = kosmos::far_image_path(b.style, width);
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
            return path.string();
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
        const std::string override = find_texture_in_directory(root, "/data/textures-hires/", lower, {".dds", ".png", ".jpg", ".jpeg"});
        if (!override.empty())
            return override;
        const std::string celestia = find_image_in_folder(root + "/import/celestia/hires/" + lower, lower);
        if (!celestia.empty())
            return celestia;
        const std::string shipped = find_texture_in_directory(root, "/data/textures/", lower, {".dds", ".png", ".jpg", ".jpeg"});
        if (shipped.empty() && b.style.valid() && !b.minor)
            return generated_surface_image(b);
        return shipped;
    }
} // namespace kosmos::app
