// A tiny headless image type and a DDS decoder (DXT1/3/5 and uncompressed 24/32-bit), because Orbiter textures are DDS and
// gdk-pixbuf cannot read them. Other formats (PNG, JPG, ...) are loaded by the GTK app through gdk-pixbuf.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cad
{
    struct Image
    {
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;      // w * h * 4, top row first
    };

    /** Decodes the top mip level of a DDS file held in memory. */
    bool decode_dds(const std::vector<uint8_t> &data, Image &out);
    /** Reads a DDS file from disk. */
    bool load_dds_file(const std::string &path, Image &out);
}
