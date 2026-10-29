#pragma once

// A stand-in for simlib's draw.h: just the RGBA bitmap the planet generator fills in (make_region / make_texture).

#include <cstdint>
#include <vector>

namespace sl
{
    struct Bitmap
    {
        int width = 0, height = 0;
        std::vector<std::uint8_t> pixels; // RGBA8
        bool ram_dirty = false, smooth = false, mipmaps = false;
    };

    inline Bitmap *create_bitmap(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return nullptr;
        Bitmap *bitmap = new Bitmap;
        bitmap->width = width;
        bitmap->height = height;
        bitmap->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
        return bitmap;
    }

    inline void destroy_bitmap(Bitmap *bitmap) { delete bitmap; }
}
