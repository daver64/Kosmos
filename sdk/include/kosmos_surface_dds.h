#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace kosmos::surface_dds
{
    inline bool supported(const std::vector<std::uint8_t> &bytes, std::size_t *data_offset = nullptr,
                          bool *alpha_block = nullptr, std::size_t max_decoded_bytes = 0)
    {
        if (bytes.size() < 128 || std::memcmp(bytes.data(), "DDS ", 4) != 0)
            return false;
        std::uint32_t width = 0, height = 0, pixel_flags = 0;
        std::memcpy(&height, bytes.data() + 12, 4);
        std::memcpy(&width, bytes.data() + 16, 4);
        std::memcpy(&pixel_flags, bytes.data() + 80, 4);
        bool dxt1 = std::memcmp(bytes.data() + 84, "DXT1", 4) == 0;
        bool alpha = std::memcmp(bytes.data() + 84, "DXT3", 4) == 0 || std::memcmp(bytes.data() + 84, "DXT5", 4) == 0;
        std::size_t offset = 128;
        if (std::memcmp(bytes.data() + 84, "DX10", 4) == 0 && bytes.size() >= 148)
        {
            std::uint32_t format = 0, dimension = 0, array_size = 0;
            std::memcpy(&format, bytes.data() + 128, 4);
            std::memcpy(&dimension, bytes.data() + 132, 4);
            std::memcpy(&array_size, bytes.data() + 140, 4);
            dxt1 = format == 71 || format == 72;
            alpha = format == 74 || format == 75 || format == 77 || format == 78;
            if (dimension != 3 || array_size != 1)
                return false;
            offset = 148;
        }
        if (!(pixel_flags & 0x4) || (!dxt1 && !alpha) || width < 4 || height < 4 || width % 4 != 0 || height % 4 != 0)
            return false;
        if (max_decoded_bytes && static_cast<std::uint64_t>(width) * height > max_decoded_bytes / 4)
            return false;
        const std::uint64_t needed = static_cast<std::uint64_t>(width / 4) * (height / 4) * (alpha ? 16 : 8);
        if (needed > bytes.size() - offset)
            return false;
        if (data_offset)
            *data_offset = offset;
        if (alpha_block)
            *alpha_block = alpha;
        return true;
    }

    inline void texel(const std::vector<std::uint8_t> &blocks, int width, bool alpha_block, int x, int y, float rgb[3])
    {
        const std::size_t size = alpha_block ? 16 : 8;
        const std::uint8_t *block = blocks.data() + (static_cast<std::size_t>(y >> 2) * (width >> 2) + (x >> 2)) * size + (alpha_block ? 8 : 0);
        std::uint16_t c0, c1;
        std::memcpy(&c0, block, 2);
        std::memcpy(&c1, block + 2, 2);
        auto expand = [](std::uint16_t c, int out[3])
        {
            out[0] = ((c >> 11) & 31) * 255 / 31;
            out[1] = ((c >> 5) & 63) * 255 / 63;
            out[2] = (c & 31) * 255 / 31;
        };
        int p[4][3];
        expand(c0, p[0]);
        expand(c1, p[1]);
        for (int k = 0; k < 3; ++k)
        {
            if (c0 > c1 || alpha_block)
            {
                p[2][k] = (2 * p[0][k] + p[1][k]) / 3;
                p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
            }
            else
            {
                p[2][k] = (p[0][k] + p[1][k]) / 2;
                p[3][k] = 0;
            }
        }
        const int index = (block[4 + (y & 3)] >> (2 * (x & 3))) & 3;
        for (int k = 0; k < 3; ++k)
            rgb[k] = static_cast<float>(p[index][k]) / 255.0f;
    }
}
