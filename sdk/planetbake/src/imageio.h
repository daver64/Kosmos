#pragma once

// Reading images back: the PNG and DDS files planetbake itself writes, and ordinary PNGs from other tools.

#include <cstdint>
#include <string>
#include <vector>

namespace planetbake
{
    struct Image
    {
        int w = 0, h = 0;
        std::vector<std::uint8_t> rgba;  // w * h * 4
        std::vector<std::uint16_t> gray; // w * h, only for 16 bit greyscale files
        bool has_alpha = false;
        bool is_gray = false;
        int bits = 8;
    };

    /** Width and height only (cheap, for big files). */
    bool image_size(const std::string &path, int &w, int &h);

    /**
     * Reads a PNG (1-16 bit, grey, grey+alpha, RGB, RGBA, palette; not interlaced) or a DDS (BC1, BC3 or uncompressed 32 bit; the
     * largest mip level). Anything else (a JPEG, say) is an error: see convert_to_png.
     */
    bool read_image(const std::string &path, Image &out, std::string &error);

    /**
     * Turns another kind of image file (JPEG, TIFF...) into a PNG using ImageMagick when it is installed (limited to 4 threads).
     * Returns the PNG path, or an empty string.
     */
    std::string convert_to_png(const std::string &path, const std::string &cache_dir, std::string &error);

    /** True for a file extension read_image understands. */
    bool native_image(const std::string &path);
}
