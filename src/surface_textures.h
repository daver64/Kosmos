#pragma once

#include "app.h"

namespace kosmos::app
{
    // A box-filtered halving, used when an image is larger than the GPU accepts.
    void halve_bitmap(sl::Bitmap &bitmap);

    // Loads a hi-res surface image onto the GPU, halving it until the backend accepts it; RAM is released afterwards.
    sl::Bitmap *finish_cpu_texture(sl::Bitmap *bitmap, const std::string &path, int max_size);

    sl::Bitmap *load_surface_texture(const std::string &path, int max_size);

    sl::Bitmap *load_optional_texture(const std::string &path, int max_size);

    std::string find_texture_in_directory(const std::string &root, const std::string &directory, const std::string &name,
                                          std::initializer_list<const char *> extensions);

    // A texture by name: data/textures-hires/ (optional, not in the repository) wins over the default set in data/textures/.
    std::string find_texture_file(const std::string &root, const std::string &name, std::initializer_list<const char *> extensions);

    // The first .ctx in a Celestia virtual-texture folder, or the folder itself when it has none. Empty if the folder is missing.
    std::string find_tile_source(const std::string &directory);

    std::string find_image_in_folder(const std::string &directory, const std::string &prefer = {});

extern int generated_texture_width;

    std::string generated_surface_image(const kosmos::Body &b);

    std::string find_surface_image(const std::string &root, const kosmos::Body &b);
} // namespace kosmos::app
