#include "encode.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace planetbake
{
    namespace
    {
        void put32(std::uint8_t *p, std::uint32_t v)
        {
            p[0] = static_cast<std::uint8_t>(v >> 24);
            p[1] = static_cast<std::uint8_t>(v >> 16);
            p[2] = static_cast<std::uint8_t>(v >> 8);
            p[3] = static_cast<std::uint8_t>(v);
        }

        void le32(std::vector<std::uint8_t> &out, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }

    PngWriter::~PngWriter()
    {
        if (file_)
        {
            if (started_)
                deflateEnd(&stream_);
            std::fclose(file_);
        }
    }

    bool PngWriter::chunk(const char *type, const std::uint8_t *data, std::uint32_t length)
    {
        std::uint8_t head[8];
        put32(head, length);
        std::memcpy(head + 4, type, 4);
        std::uint8_t tail[4];
        uLong crc = crc32(0L, reinterpret_cast<const Bytef *>(type), 4);
        if (length)
            crc = crc32(crc, data, length);
        put32(tail, static_cast<std::uint32_t>(crc));
        return std::fwrite(head, 1, 8, file_) == 8 && (!length || std::fwrite(data, 1, length, file_) == length) && std::fwrite(tail, 1, 4, file_) == 4;
    }

    bool PngWriter::open(const std::string &path, int width, int height, PngKind kind)
    {
        file_ = std::fopen(path.c_str(), "wb");
        if (!file_)
            return false;
        width_ = width;
        height_ = height;
        pixel_bytes_ = kind == PngKind::gray8 ? 1 : kind == PngKind::gray16 ? 2 : kind == PngKind::rgba8 ? 4 : 3;
        row_bytes_ = width * pixel_bytes_;
        previous_.assign(static_cast<std::size_t>(row_bytes_), 0);
        filtered_.assign(static_cast<std::size_t>(row_bytes_) + 1, 0);
        out_.resize(1 << 16);
        static const std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        std::uint8_t ihdr[13];
        put32(ihdr, static_cast<std::uint32_t>(width));
        put32(ihdr + 4, static_cast<std::uint32_t>(height));
        ihdr[8] = kind == PngKind::gray16 ? 16 : 8;
        ihdr[9] = kind == PngKind::rgba8 ? 6 : kind == PngKind::rgb8 ? 2 : 0;
        ihdr[10] = ihdr[11] = ihdr[12] = 0;
        if (std::fwrite(signature, 1, 8, file_) != 8 || !chunk("IHDR", ihdr, 13))
            return false;
        std::memset(&stream_, 0, sizeof(stream_));
        if (deflateInit(&stream_, 5) != Z_OK)
            return false;
        started_ = true;
        return true;
    }

    bool PngWriter::flush_deflate(int mode)
    {
        for (;;)
        {
            stream_.next_out = out_.data();
            stream_.avail_out = static_cast<uInt>(out_.size());
            const int result = deflate(&stream_, mode);
            const std::size_t produced = out_.size() - stream_.avail_out;
            if (produced && !chunk("IDAT", out_.data(), static_cast<std::uint32_t>(produced)))
                return false;
            if (result == Z_STREAM_END || (stream_.avail_out != 0 && stream_.avail_in == 0))
                return result == Z_OK || result == Z_STREAM_END || result == Z_BUF_ERROR;
            if (result != Z_OK && result != Z_BUF_ERROR)
                return false;
        }
    }

    bool PngWriter::row(const std::uint8_t *pixels)
    {
        if (!file_ || failed_ || rows_done_ >= height_)
            return false;
        // Sub filter: each byte minus the one a pixel to the left.
        filtered_[0] = 1;
        for (int i = 0; i < row_bytes_; ++i)
            filtered_[static_cast<std::size_t>(i) + 1] = static_cast<std::uint8_t>(pixels[i] - (i >= pixel_bytes_ ? pixels[i - pixel_bytes_] : 0));
        stream_.next_in = filtered_.data();
        stream_.avail_in = static_cast<uInt>(filtered_.size());
        ++rows_done_;
        if (!flush_deflate(Z_NO_FLUSH))
            failed_ = true;
        return !failed_;
    }

    bool PngWriter::close()
    {
        if (!file_)
            return false;
        bool ok = !failed_ && rows_done_ == height_;
        if (ok)
        {
            stream_.avail_in = 0;
            ok = flush_deflate(Z_FINISH) && chunk("IEND", nullptr, 0);
        }
        deflateEnd(&stream_);
        started_ = false;
        ok = (std::fclose(file_) == 0) && ok;
        file_ = nullptr;
        return ok;
    }

    namespace
    {
        bool write_rows(const std::string &path, int width, int height, PngKind kind, const std::function<void(int, std::uint8_t *)> &fill)
        {
            PngWriter writer;
            if (!writer.open(path, width, height, kind))
                return false;
            std::vector<std::uint8_t> line(static_cast<std::size_t>(writer.bytes_per_row()));
            for (int y = 0; y < height; ++y)
            {
                fill(y, line.data());
                if (!writer.row(line.data()))
                    return false;
            }
            return writer.close();
        }
    }

    bool write_png_rgb(const std::string &path, const std::uint8_t *rgba, int width, int height)
    {
        return write_rows(path, width, height, PngKind::rgb8, [&](int y, std::uint8_t *line)
        {
            const std::uint8_t *source = rgba + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x)
                std::memcpy(line + x * 3, source + x * 4, 3);
        });
    }

    bool write_png_rgba(const std::string &path, const std::uint8_t *rgba, int width, int height)
    {
        return write_rows(path, width, height, PngKind::rgba8, [&](int y, std::uint8_t *line)
        {
            std::memcpy(line, rgba + static_cast<std::size_t>(y) * width * 4, static_cast<std::size_t>(width) * 4);
        });
    }

    bool write_png_gray(const std::string &path, const std::uint8_t *rgba, int width, int height)
    {
        return write_rows(path, width, height, PngKind::gray8, [&](int y, std::uint8_t *line)
        {
            const std::uint8_t *source = rgba + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x)
                line[x] = source[x * 4];
        });
    }

    bool write_png_gray16(const std::string &path, const std::uint16_t *values, int width, int height)
    {
        return write_rows(path, width, height, PngKind::gray16, [&](int y, std::uint8_t *line)
        {
            const std::uint16_t *source = values + static_cast<std::size_t>(y) * width;
            for (int x = 0; x < width; ++x)
            {
                line[x * 2] = static_cast<std::uint8_t>(source[x] >> 8);
                line[x * 2 + 1] = static_cast<std::uint8_t>(source[x]);
            }
        });
    }

    const char *dds_kind_name(DdsKind kind)
    {
        return kind == DdsKind::bc1 ? "bc1" : kind == DdsKind::bc3 ? "bc3" : "rgba";
    }

    double dds_bytes_per_pixel(DdsKind kind)
    {
        const double base = kind == DdsKind::bc1 ? 0.5 : kind == DdsKind::bc3 ? 1.0 : 4.0;
        return base * 4.0 / 3.0; // the mip chain adds a third
    }

    namespace
    {
        std::uint16_t pack565(const int c[3]) { return static_cast<std::uint16_t>(((c[0] * 31 + 127) / 255) << 11 | ((c[1] * 63 + 127) / 255) << 5 | ((c[2] * 31 + 127) / 255)); }

        void unpack565(std::uint16_t v, int c[3])
        {
            const int r = v >> 11, g = (v >> 5) & 63, b = v & 31;
            c[0] = (r << 3) | (r >> 2);
            c[1] = (g << 2) | (g >> 4);
            c[2] = (b << 3) | (b >> 2);
        }

        // The 4 x 4 block at (bx, by), edge pixels repeated where the image ends.
        void fetch_block(const std::uint8_t *rgba, int width, int height, int bx, int by, std::uint8_t block[16][4])
        {
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i)
                {
                    const int x = std::min(width - 1, bx * 4 + i), y = std::min(height - 1, by * 4 + j);
                    std::memcpy(block[j * 4 + i], rgba + (static_cast<std::size_t>(y) * width + x) * 4, 4);
                }
        }

        // BC1 colour block: endpoints from the block's bounding box (pulled in a little), four colours along the line between them.
        void encode_colour(const std::uint8_t block[16][4], std::uint8_t *out)
        {
            int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
            for (int p = 0; p < 16; ++p)
                for (int c = 0; c < 3; ++c)
                {
                    lo[c] = std::min<int>(lo[c], block[p][c]);
                    hi[c] = std::max<int>(hi[c], block[p][c]);
                }
            for (int c = 0; c < 3; ++c)
            {
                const int inset = (hi[c] - lo[c]) / 16;
                lo[c] += inset;
                hi[c] -= inset;
            }
            std::uint16_t c0 = pack565(hi), c1 = pack565(lo);
            if (c0 < c1)
                std::swap(c0, c1);
            std::uint32_t indices = 0;
            if (c0 != c1)
            {
                int palette[4][3];
                unpack565(c0, palette[0]);
                unpack565(c1, palette[1]);
                for (int c = 0; c < 3; ++c)
                {
                    palette[2][c] = (2 * palette[0][c] + palette[1][c]) / 3;
                    palette[3][c] = (palette[0][c] + 2 * palette[1][c]) / 3;
                }
                for (int p = 0; p < 16; ++p)
                {
                    int best = 0, best_distance = 1 << 30;
                    for (int k = 0; k < 4; ++k)
                    {
                        int distance = 0;
                        for (int c = 0; c < 3; ++c)
                            distance += (block[p][c] - palette[k][c]) * (block[p][c] - palette[k][c]);
                        if (distance < best_distance)
                        {
                            best_distance = distance;
                            best = k;
                        }
                    }
                    indices |= static_cast<std::uint32_t>(best) << (2 * p);
                }
            }
            out[0] = static_cast<std::uint8_t>(c0);
            out[1] = static_cast<std::uint8_t>(c0 >> 8);
            out[2] = static_cast<std::uint8_t>(c1);
            out[3] = static_cast<std::uint8_t>(c1 >> 8);
            for (int i = 0; i < 4; ++i)
                out[4 + i] = static_cast<std::uint8_t>(indices >> (8 * i));
        }

        // BC3 alpha block: the extremes, with six values between them.
        void encode_alpha(const std::uint8_t block[16][4], std::uint8_t *out)
        {
            int lo = 255, hi = 0;
            for (int p = 0; p < 16; ++p)
            {
                lo = std::min<int>(lo, block[p][3]);
                hi = std::max<int>(hi, block[p][3]);
            }
            int palette[8];
            palette[0] = hi;
            palette[1] = lo;
            for (int k = 1; k < 7; ++k)
                palette[k + 1] = ((7 - k) * hi + k * lo) / 7;
            std::uint64_t indices = 0;
            if (hi != lo)
                for (int p = 0; p < 16; ++p)
                {
                    int best = 0, best_distance = 1 << 30;
                    for (int k = 0; k < 8; ++k)
                    {
                        const int distance = std::abs(block[p][3] - palette[k]);
                        if (distance < best_distance)
                        {
                            best_distance = distance;
                            best = k;
                        }
                    }
                    indices |= static_cast<std::uint64_t>(best) << (3 * p);
                }
            out[0] = static_cast<std::uint8_t>(hi);
            out[1] = static_cast<std::uint8_t>(lo);
            for (int i = 0; i < 6; ++i)
                out[2 + i] = static_cast<std::uint8_t>(indices >> (8 * i));
        }

        bool write_level(FILE *file, const std::uint8_t *rgba, int width, int height, DdsKind kind)
        {
            if (kind == DdsKind::rgba)
            {
                std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 4);
                for (int y = 0; y < height; ++y)
                {
                    const std::uint8_t *source = rgba + static_cast<std::size_t>(y) * width * 4;
                    for (int x = 0; x < width; ++x)
                    {
                        row[x * 4 + 0] = source[x * 4 + 2];
                        row[x * 4 + 1] = source[x * 4 + 1];
                        row[x * 4 + 2] = source[x * 4 + 0];
                        row[x * 4 + 3] = source[x * 4 + 3];
                    }
                    if (std::fwrite(row.data(), 1, row.size(), file) != row.size())
                        return false;
                }
                return true;
            }
            const int block_size = kind == DdsKind::bc1 ? 8 : 16;
            const int blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
            std::vector<std::uint8_t> row(static_cast<std::size_t>(blocks_x) * block_size);
            std::uint8_t block[16][4];
            for (int by = 0; by < blocks_y; ++by)
            {
                for (int bx = 0; bx < blocks_x; ++bx)
                {
                    fetch_block(rgba, width, height, bx, by, block);
                    std::uint8_t *out = row.data() + static_cast<std::size_t>(bx) * block_size;
                    if (kind == DdsKind::bc3)
                    {
                        encode_alpha(block, out);
                        encode_colour(block, out + 8);
                    }
                    else
                        encode_colour(block, out);
                }
                if (std::fwrite(row.data(), 1, row.size(), file) != row.size())
                    return false;
            }
            return true;
        }

        std::vector<std::uint8_t> half_size(const std::uint8_t *rgba, int width, int height, int &new_width, int &new_height)
        {
            new_width = std::max(1, width / 2);
            new_height = std::max(1, height / 2);
            std::vector<std::uint8_t> out(static_cast<std::size_t>(new_width) * new_height * 4);
            for (int y = 0; y < new_height; ++y)
                for (int x = 0; x < new_width; ++x)
                {
                    const int x0 = std::min(width - 1, x * 2), x1 = std::min(width - 1, x * 2 + 1);
                    const int y0 = std::min(height - 1, y * 2), y1 = std::min(height - 1, y * 2 + 1);
                    for (int c = 0; c < 4; ++c)
                    {
                        const int sum = rgba[(static_cast<std::size_t>(y0) * width + x0) * 4 + c] + rgba[(static_cast<std::size_t>(y0) * width + x1) * 4 + c] +
                                        rgba[(static_cast<std::size_t>(y1) * width + x0) * 4 + c] + rgba[(static_cast<std::size_t>(y1) * width + x1) * 4 + c];
                        out[(static_cast<std::size_t>(y) * new_width + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                    }
                }
            return out;
        }
    }

    bool write_dds(const std::string &path, const std::uint8_t *rgba, int width, int height, DdsKind kind, bool mipmaps)
    {
        int levels = 1;
        if (mipmaps)
            for (int w = width, h = height; w > 1 || h > 1; w = std::max(1, w / 2), h = std::max(1, h / 2))
                ++levels;
        std::vector<std::uint8_t> header;
        le32(header, 0x20534444); // "DDS "
        le32(header, 124);
        const bool compressed = kind != DdsKind::rgba;
        le32(header, 0x1 | 0x2 | 0x4 | 0x1000 | (levels > 1 ? 0x20000 : 0) | (compressed ? 0x80000 : 0x8));
        le32(header, static_cast<std::uint32_t>(height));
        le32(header, static_cast<std::uint32_t>(width));
        const int block_size = kind == DdsKind::bc1 ? 8 : 16;
        le32(header, compressed ? static_cast<std::uint32_t>(((width + 3) / 4) * ((height + 3) / 4) * block_size) : static_cast<std::uint32_t>(width * 4));
        le32(header, 0);
        le32(header, static_cast<std::uint32_t>(levels));
        for (int i = 0; i < 11; ++i)
            le32(header, 0);
        le32(header, 32); // pixel format
        if (compressed)
        {
            le32(header, 0x4); // FOURCC
            le32(header, kind == DdsKind::bc1 ? 0x31545844 : 0x35545844); // DXT1, DXT5
            for (int i = 0; i < 5; ++i)
                le32(header, 0);
        }
        else
        {
            le32(header, 0x41); // RGB + ALPHAPIXELS
            le32(header, 0);
            le32(header, 32);
            le32(header, 0x00ff0000);
            le32(header, 0x0000ff00);
            le32(header, 0x000000ff);
            le32(header, 0xff000000);
        }
        le32(header, 0x1000 | (levels > 1 ? 0x400008 : 0)); // TEXTURE, plus COMPLEX | MIPMAP
        for (int i = 0; i < 4; ++i)
            le32(header, 0);

        FILE *file = std::fopen(path.c_str(), "wb");
        if (!file)
            return false;
        bool ok = std::fwrite(header.data(), 1, header.size(), file) == header.size();
        std::vector<std::uint8_t> level_pixels;
        const std::uint8_t *current = rgba;
        int w = width, h = height;
        for (int level = 0; ok && level < levels; ++level)
        {
            ok = write_level(file, current, w, h, kind);
            if (level + 1 < levels)
            {
                int nw, nh;
                std::vector<std::uint8_t> next = half_size(current, w, h, nw, nh);
                level_pixels = std::move(next);
                current = level_pixels.data();
                w = nw;
                h = nh;
            }
        }
        ok = (std::fclose(file) == 0) && ok;
        return ok;
    }
}
