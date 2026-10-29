#include "imageio.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <zlib.h>

namespace planetbake
{
    namespace fs = std::filesystem;

    namespace
    {
        std::uint32_t be32(const std::uint8_t *p) { return (static_cast<std::uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
        std::uint32_t le32(const std::uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }

        bool read_file(const std::string &path, std::vector<std::uint8_t> &data)
        {
            FILE *f = std::fopen(path.c_str(), "rb");
            if (!f)
                return false;
            std::fseek(f, 0, SEEK_END);
            const long size = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            data.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
            const bool ok = size <= 0 || std::fread(data.data(), 1, data.size(), f) == data.size();
            std::fclose(f);
            return ok;
        }

        std::string extension(const std::string &path)
        {
            std::string e = fs::path(path).extension().string();
            for (char &c : e)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return e;
        }

        int paeth(int a, int b, int c)
        {
            const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
            return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
        }

        bool read_png(const std::vector<std::uint8_t> &file, Image &out, std::string &error)
        {
            static const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
            if (file.size() < 33 || std::memcmp(file.data(), signature, 8) != 0)
            {
                error = "not a PNG file";
                return false;
            }
            int w = 0, h = 0, depth = 0, color = 0, interlace = 0;
            std::vector<std::uint8_t> idat, palette, trns;
            for (std::size_t at = 8; at + 12 <= file.size();)
            {
                const std::uint32_t length = be32(&file[at]);
                if (at + 12 + length > file.size())
                    break;
                const char *type = reinterpret_cast<const char *>(&file[at + 4]);
                const std::uint8_t *data = &file[at + 8];
                if (!std::memcmp(type, "IHDR", 4) && length >= 13)
                {
                    w = static_cast<int>(be32(data));
                    h = static_cast<int>(be32(data + 4));
                    depth = data[8];
                    color = data[9];
                    interlace = data[12];
                }
                else if (!std::memcmp(type, "PLTE", 4))
                    palette.assign(data, data + length);
                else if (!std::memcmp(type, "tRNS", 4))
                    trns.assign(data, data + length);
                else if (!std::memcmp(type, "IDAT", 4))
                    idat.insert(idat.end(), data, data + length);
                else if (!std::memcmp(type, "IEND", 4))
                    break;
                at += 12 + length;
            }
            if (w <= 0 || h <= 0 || w > 1 << 20 || h > 1 << 20)
            {
                error = "bad PNG header";
                return false;
            }
            if (interlace)
            {
                error = "interlaced PNGs are not supported";
                return false;
            }
            int channels;
            switch (color)
            {
            case 0: channels = 1; break;
            case 2: channels = 3; break;
            case 3: channels = 1; break;
            case 4: channels = 2; break;
            case 6: channels = 4; break;
            default: error = "bad PNG colour type"; return false;
            }
            const int bits_per_pixel = channels * depth;
            const std::size_t row_bytes = (static_cast<std::size_t>(w) * bits_per_pixel + 7) / 8;
            const int bpp = std::max(1, bits_per_pixel / 8);
            if (color == 3 && palette.empty())
            {
                error = "PNG palette missing";
                return false;
            }

            out = Image();
            out.w = w;
            out.h = h;
            out.bits = depth;
            out.is_gray = color == 0 || color == 4;
            out.has_alpha = color == 4 || color == 6 || !trns.empty();
            out.rgba.assign(static_cast<std::size_t>(w) * h * 4, 255);
            if (color == 0 && depth == 16)
                out.gray.assign(static_cast<std::size_t>(w) * h, 0);

            z_stream z{};
            if (inflateInit(&z) != Z_OK)
            {
                error = "zlib failed";
                return false;
            }
            z.next_in = idat.data();
            z.avail_in = static_cast<uInt>(idat.size());
            std::vector<std::uint8_t> row(row_bytes + 1), previous(row_bytes, 0), current(row_bytes);
            bool ok = true;
            for (int y = 0; y < h && ok; ++y)
            {
                z.next_out = row.data();
                z.avail_out = static_cast<uInt>(row.size());
                const int r = inflate(&z, Z_NO_FLUSH);
                if ((r != Z_OK && r != Z_STREAM_END) || z.avail_out != 0)
                {
                    error = "PNG data is damaged";
                    ok = false;
                    break;
                }
                const int filter = row[0];
                const std::uint8_t *src = row.data() + 1;
                for (std::size_t i = 0; i < row_bytes; ++i)
                {
                    const int a = i >= static_cast<std::size_t>(bpp) ? current[i - bpp] : 0;
                    const int b = previous[i];
                    const int c = i >= static_cast<std::size_t>(bpp) ? previous[i - bpp] : 0;
                    int v = src[i];
                    switch (filter)
                    {
                    case 1: v += a; break;
                    case 2: v += b; break;
                    case 3: v += (a + b) / 2; break;
                    case 4: v += paeth(a, b, c); break;
                    default: break;
                    }
                    current[i] = static_cast<std::uint8_t>(v);
                }
                std::uint8_t *dst = out.rgba.data() + static_cast<std::size_t>(y) * w * 4;
                auto sample = [&](int index) -> int { // the index-th sample of the row, scaled to 8 bits
                    if (depth == 8)
                        return current[index];
                    if (depth == 16)
                        return current[index * 2];
                    const int per_byte = 8 / depth;
                    const int shift = 8 - depth * (index % per_byte + 1);
                    const int v = (current[index / per_byte] >> shift) & ((1 << depth) - 1);
                    return color == 3 ? v : v * 255 / ((1 << depth) - 1);
                };
                for (int x = 0; x < w; ++x)
                {
                    std::uint8_t *p = dst + x * 4;
                    if (color == 0)
                    {
                        p[0] = p[1] = p[2] = static_cast<std::uint8_t>(sample(x));
                        if (depth == 16)
                            out.gray[static_cast<std::size_t>(y) * w + x] = static_cast<std::uint16_t>((current[x * 2] << 8) | current[x * 2 + 1]);
                    }
                    else if (color == 2)
                        for (int c = 0; c < 3; ++c)
                            p[c] = static_cast<std::uint8_t>(sample(x * 3 + c));
                    else if (color == 3)
                    {
                        const int index = sample(x);
                        for (int c = 0; c < 3; ++c)
                            p[c] = static_cast<std::size_t>(index * 3 + c) < palette.size() ? palette[index * 3 + c] : 0;
                        p[3] = static_cast<std::size_t>(index) < trns.size() ? trns[index] : 255;
                    }
                    else if (color == 4)
                    {
                        p[0] = p[1] = p[2] = static_cast<std::uint8_t>(sample(x * 2));
                        p[3] = static_cast<std::uint8_t>(sample(x * 2 + 1));
                    }
                    else
                        for (int c = 0; c < 4; ++c)
                            p[c] = static_cast<std::uint8_t>(sample(x * 4 + c));
                }
                previous.swap(current);
            }
            inflateEnd(&z);
            return ok;
        }

        void bc_colors(const std::uint8_t *block, std::uint8_t c[4][4], bool four_always)
        {
            const std::uint16_t c0 = static_cast<std::uint16_t>(block[0] | (block[1] << 8)), c1 = static_cast<std::uint16_t>(block[2] | (block[3] << 8));
            auto expand = [](std::uint16_t v, std::uint8_t *o) {
                o[0] = static_cast<std::uint8_t>(((v >> 11) & 31) * 255 / 31);
                o[1] = static_cast<std::uint8_t>(((v >> 5) & 63) * 255 / 63);
                o[2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
                o[3] = 255;
            };
            expand(c0, c[0]);
            expand(c1, c[1]);
            if (c0 > c1 || four_always)
                for (int i = 0; i < 3; ++i)
                {
                    c[2][i] = static_cast<std::uint8_t>((2 * c[0][i] + c[1][i]) / 3);
                    c[3][i] = static_cast<std::uint8_t>((c[0][i] + 2 * c[1][i]) / 3);
                }
            else
                for (int i = 0; i < 3; ++i)
                {
                    c[2][i] = static_cast<std::uint8_t>((c[0][i] + c[1][i]) / 2);
                    c[3][i] = 0;
                }
            c[2][3] = 255;
            c[3][3] = (c0 > c1 || four_always) ? 255 : 0;
        }

        bool read_dds(const std::vector<std::uint8_t> &file, Image &out, std::string &error)
        {
            if (file.size() < 128 || le32(file.data()) != 0x20534444)
            {
                error = "not a DDS file";
                return false;
            }
            const std::uint8_t *h = file.data();
            const int height = static_cast<int>(le32(h + 12)), width = static_cast<int>(le32(h + 16));
            const std::uint32_t pf_flags = le32(h + 80), four_cc = le32(h + 84), bits = le32(h + 88);
            if (width <= 0 || height <= 0 || width > 1 << 20 || height > 1 << 20)
            {
                error = "bad DDS header";
                return false;
            }
            out = Image();
            out.w = width;
            out.h = height;
            out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);
            const std::uint8_t *data = h + 128;
            const std::size_t available = file.size() - 128;
            if (pf_flags & 0x4)
            {
                const bool dxt1 = four_cc == 0x31545844, dxt5 = four_cc == 0x35545844;
                if (!dxt1 && !dxt5)
                {
                    error = "this DDS compression is not supported (BC1, BC3 and uncompressed are)";
                    return false;
                }
                const int bw = (width + 3) / 4, bh = (height + 3) / 4, block = dxt1 ? 8 : 16;
                if (available < static_cast<std::size_t>(bw) * bh * block)
                {
                    error = "DDS file is truncated";
                    return false;
                }
                out.has_alpha = dxt5;
                for (int by = 0; by < bh; ++by)
                    for (int bx = 0; bx < bw; ++bx)
                    {
                        const std::uint8_t *b = data + (static_cast<std::size_t>(by) * bw + bx) * block;
                        std::uint8_t c[4][4];
                        bc_colors(b + (dxt1 ? 0 : 8), c, dxt5);
                        std::uint8_t alpha[8] = {};
                        if (dxt5)
                        {
                            alpha[0] = b[0];
                            alpha[1] = b[1];
                            if (alpha[0] > alpha[1])
                                for (int i = 1; i < 7; ++i)
                                    alpha[i + 1] = static_cast<std::uint8_t>(((7 - i) * alpha[0] + i * alpha[1]) / 7);
                            else
                            {
                                for (int i = 1; i < 5; ++i)
                                    alpha[i + 1] = static_cast<std::uint8_t>(((5 - i) * alpha[0] + i * alpha[1]) / 5);
                                alpha[6] = 0;
                                alpha[7] = 255;
                            }
                        }
                        std::uint64_t alpha_bits = 0;
                        if (dxt5)
                            for (int i = 0; i < 6; ++i)
                                alpha_bits |= static_cast<std::uint64_t>(b[2 + i]) << (8 * i);
                        const std::uint8_t *codes = b + (dxt1 ? 4 : 12);
                        for (int py = 0; py < 4; ++py)
                            for (int px = 0; px < 4; ++px)
                            {
                                const int x = bx * 4 + px, y = by * 4 + py;
                                if (x >= width || y >= height)
                                    continue;
                                const int index = (codes[py] >> (px * 2)) & 3;
                                std::uint8_t *p = out.rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
                                std::memcpy(p, c[index], 3);
                                p[3] = dxt5 ? alpha[(alpha_bits >> (3 * (py * 4 + px))) & 7] : c[index][3];
                                if (dxt1 && c[index][3] == 0)
                                    out.has_alpha = true;
                            }
                    }
                return true;
            }
            if ((pf_flags & 0x40) && bits == 32)
            {
                if (available < static_cast<std::size_t>(width) * height * 4)
                {
                    error = "DDS file is truncated";
                    return false;
                }
                const std::uint32_t masks[4] = {le32(h + 92), le32(h + 96), le32(h + 100), (pf_flags & 0x1) ? le32(h + 104) : 0};
                out.has_alpha = masks[3] != 0;
                for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i)
                {
                    const std::uint32_t v = le32(data + i * 4);
                    for (int c = 0; c < 4; ++c)
                    {
                        if (!masks[c])
                            continue;
                        int shift = 0;
                        while (!((masks[c] >> shift) & 1))
                            ++shift;
                        out.rgba[i * 4 + c] = static_cast<std::uint8_t>((v & masks[c]) >> shift);
                    }
                }
                return true;
            }
            error = "this DDS pixel format is not supported (BC1, BC3 and uncompressed 32 bit are)";
            return false;
        }
    }

    bool native_image(const std::string &path)
    {
        const std::string e = extension(path);
        return e == ".png" || e == ".dds";
    }

    bool image_size(const std::string &path, int &w, int &h)
    {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f)
            return false;
        std::uint8_t head[128] = {};
        const std::size_t n = std::fread(head, 1, sizeof head, f);
        std::fclose(f);
        if (n >= 24 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G')
        {
            w = static_cast<int>(be32(head + 16));
            h = static_cast<int>(be32(head + 20));
            return w > 0 && h > 0;
        }
        if (n >= 20 && le32(head) == 0x20534444)
        {
            h = static_cast<int>(le32(head + 12));
            w = static_cast<int>(le32(head + 16));
            return w > 0 && h > 0;
        }
        return false;
    }

    bool read_image(const std::string &path, Image &out, std::string &error)
    {
        std::vector<std::uint8_t> file;
        if (!read_file(path, file))
        {
            error = "cannot read " + path;
            return false;
        }
        const std::string e = extension(path);
        bool ok;
        if (e == ".png")
            ok = read_png(file, out, error);
        else if (e == ".dds")
            ok = read_dds(file, out, error);
        else
        {
            error = path + ": only PNG and DDS images can be read directly";
            return false;
        }
        if (!ok)
            error = path + ": " + error;
        return ok;
    }

    std::string convert_to_png(const std::string &path, const std::string &cache_dir, std::string &error)
    {
        std::error_code ec;
        for (const std::string *p : {&path, &cache_dir})
            if (p->find_first_of("\"$`\\\n") != std::string::npos)
            {
                error = "the path " + *p + " has characters (\" $ ` \\) that cannot be passed to ImageMagick safely";
                return {};
            }
        fs::create_directories(cache_dir, ec);
        const fs::path out = fs::path(cache_dir) / (fs::path(path).stem().string() + ".png");
        if (fs::exists(out, ec) && fs::last_write_time(out, ec) >= fs::last_write_time(path, ec))
            return out.string();
        const fs::path part = out.string() + ".part.png";
        for (const char *tool : {"magick", "convert"})
        {
            const std::string command = std::string(tool) + " -limit thread 4 \"" + path + "[0]\" \"" + part.string() + "\"";
            if (std::system(command.c_str()) == 0 && fs::exists(part, ec))
            {
                fs::rename(part, out, ec);
                return out.string();
            }
        }
        error = path + ": cannot convert to PNG (install ImageMagick, or convert it yourself)";
        return std::string();
    }
}
