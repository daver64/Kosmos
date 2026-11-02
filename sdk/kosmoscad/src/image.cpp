#include "image.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace cad
{
    namespace
    {
        uint32_t rd32(const std::vector<uint8_t> &d, size_t o)
        {
            return o + 4 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24 : 0;
        }
        uint32_t rd16(const std::vector<uint8_t> &d, size_t o) { return o + 2 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 : 0; }

        void rgb565(uint32_t c, uint8_t out[3])
        {
            out[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
            out[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
            out[2] = uint8_t((c & 31) * 255 / 31);
        }

        // One 4x4 colour block (the 8 bytes DXT1 and the colour half of DXT3/5 share). `opaque_only` is set for DXT3/5, which never use
        // the 1-bit-alpha 3-colour mode.
        void colour_block(const std::vector<uint8_t> &d, size_t o, uint8_t px[16][4], bool opaque_only)
        {
            const uint32_t c0 = rd16(d, o), c1 = rd16(d, o + 2);
            uint8_t pal[4][4];
            rgb565(c0, pal[0]);
            rgb565(c1, pal[1]);
            pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
            if (c0 > c1 || opaque_only)
                for (int k = 0; k < 3; ++k)
                {
                    pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
                    pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
                }
            else
            {
                for (int k = 0; k < 3; ++k)
                {
                    pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
                    pal[3][k] = 0;
                }
                pal[3][3] = 0;
            }
            const uint32_t bits = rd32(d, o + 4);
            for (int i = 0; i < 16; ++i)
                std::memcpy(px[i], pal[(bits >> (2 * i)) & 3], 4);
        }

        void alpha_block_dxt5(const std::vector<uint8_t> &d, size_t o, uint8_t a[16])
        {
            const int a0 = d[o], a1 = d[o + 1];
            int pal[8] = {a0, a1};
            if (a0 > a1)
                for (int k = 1; k < 7; ++k)
                    pal[k + 1] = ((7 - k) * a0 + k * a1) / 7;
            else
            {
                for (int k = 1; k < 5; ++k)
                    pal[k + 1] = ((5 - k) * a0 + k * a1) / 5;
                pal[6] = 0;
                pal[7] = 255;
            }
            uint64_t bits = 0;
            for (int i = 0; i < 6; ++i)
                bits |= uint64_t(d[o + 2 + i]) << (8 * i);
            for (int i = 0; i < 16; ++i)
                a[i] = uint8_t(pal[(bits >> (3 * i)) & 7]);
        }
    }

    bool decode_dds(const std::vector<uint8_t> &d, Image &out)
    {
        if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0)
            return false;
        const int h = int(rd32(d, 12)), w = int(rd32(d, 16));
        if (w <= 0 || h <= 0 || w > 16384 || h > 16384)
            return false;
        const uint32_t pf_flags = rd32(d, 80), four_cc = rd32(d, 84), bpp = rd32(d, 88);
        const uint32_t rmask = rd32(d, 92), gmask = rd32(d, 96), bmask = rd32(d, 100), amask = rd32(d, 104);
        size_t data_at = 128;
        if (pf_flags & 4 && std::memcmp(&four_cc, "DX10", 4) == 0)
            return false;
        out.w = w;
        out.h = h;
        out.rgba.assign(size_t(w) * h * 4, 255);

        if (pf_flags & 4) // FourCC
        {
            const bool dxt1 = std::memcmp(&four_cc, "DXT1", 4) == 0, dxt3 = std::memcmp(&four_cc, "DXT3", 4) == 0, dxt5 = std::memcmp(&four_cc, "DXT5", 4) == 0;
            if (!dxt1 && !dxt3 && !dxt5)
                return false;
            const size_t block = dxt1 ? 8 : 16;
            const int bw = (w + 3) / 4, bh = (h + 3) / 4;
            if (d.size() < data_at + block * size_t(bw) * bh)
                return false;
            for (int by = 0; by < bh; ++by)
                for (int bx = 0; bx < bw; ++bx)
                {
                    const size_t o = data_at + (size_t(by) * bw + bx) * block;
                    uint8_t px[16][4];
                    colour_block(d, dxt1 ? o : o + 8, px, !dxt1);
                    if (dxt3)
                        for (int i = 0; i < 16; ++i)
                        {
                            const int n = (d[o + i / 2] >> ((i & 1) * 4)) & 15;
                            px[i][3] = uint8_t(n * 17);
                        }
                    else if (dxt5)
                    {
                        uint8_t a[16];
                        alpha_block_dxt5(d, o, a);
                        for (int i = 0; i < 16; ++i)
                            px[i][3] = a[i];
                    }
                    for (int i = 0; i < 16; ++i)
                    {
                        const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                        if (x < w && y < h)
                            std::memcpy(&out.rgba[(size_t(y) * w + x) * 4], px[i], 4);
                    }
                }
            return true;
        }

        if ((pf_flags & 0x40) && (bpp == 32 || bpp == 24) && rmask && gmask && bmask) // uncompressed RGB(A)
        {
            const size_t bytes = bpp / 8;
            if (d.size() < data_at + bytes * size_t(w) * h)
                return false;
            auto shift = [](uint32_t m) { int s = 0; while (m && !(m & 1)) { m >>= 1; ++s; } return s; };
            auto scale8 = [](uint32_t v, uint32_t m) { while (m && !(m & 1)) m >>= 1; return m ? uint8_t(v * 255 / m) : uint8_t(255); };
            for (size_t i = 0; i < size_t(w) * h; ++i)
            {
                uint32_t v = 0;
                for (size_t k = 0; k < bytes; ++k)
                    v |= uint32_t(d[data_at + i * bytes + k]) << (8 * k);
                uint8_t *p = &out.rgba[i * 4];
                p[0] = scale8((v & rmask) >> shift(rmask), rmask);
                p[1] = scale8((v & gmask) >> shift(gmask), gmask);
                p[2] = scale8((v & bmask) >> shift(bmask), bmask);
                p[3] = (pf_flags & 1) && amask ? scale8((v & amask) >> shift(amask), amask) : 255;
            }
            return true;
        }
        out = Image();
        return false;
    }

    bool load_dds_file(const std::string &path, Image &out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return decode_dds(d, out);
    }
}
