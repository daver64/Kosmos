#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <zlib.h>

namespace planetbake
{
    enum class PngKind
    {
        gray8,  // 1 byte a pixel
        gray16, // 2 bytes a pixel, big endian
        rgb8    // 3 bytes a pixel
    };

    /** Writes a PNG one row at a time (so a huge image needs only a few rows of memory). Needs only zlib. */
    class PngWriter
    {
    public:
        PngWriter() = default;
        ~PngWriter();
        PngWriter(const PngWriter &) = delete;
        PngWriter &operator=(const PngWriter &) = delete;

        bool open(const std::string &path, int width, int height, PngKind kind);
        /** One row of pixels in the writer's kind. */
        bool row(const std::uint8_t *pixels);
        /** Finishes the file. False if any write failed. */
        bool close();
        int bytes_per_row() const { return row_bytes_; }

    private:
        bool chunk(const char *type, const std::uint8_t *data, std::uint32_t length);
        bool flush_deflate(int mode);

        FILE *file_ = nullptr;
        z_stream stream_{};
        bool started_ = false, failed_ = false;
        int width_ = 0, height_ = 0, rows_done_ = 0, row_bytes_ = 0, pixel_bytes_ = 1;
        std::vector<std::uint8_t> previous_, filtered_, out_;
    };

    /** The whole image at once. rgba is width * height * 4 bytes; the alpha is dropped. */
    bool write_png_rgb(const std::string &path, const std::uint8_t *rgba, int width, int height);
    /** As above, keeping only the red channel as an 8 bit grey image. */
    bool write_png_gray(const std::string &path, const std::uint8_t *rgba, int width, int height);
    bool write_png_gray16(const std::string &path, const std::uint16_t *values, int width, int height);

    enum class DdsKind
    {
        bc1,  // 4 bits a pixel, no alpha
        bc3,  // 8 bits a pixel, with alpha
        rgba  // uncompressed 32 bits a pixel
    };

    const char *dds_kind_name(DdsKind kind);
    /** Bytes the file takes, mip levels included (an estimate for planning disk use). */
    double dds_bytes_per_pixel(DdsKind kind);

    /** Writes a .dds of an RGBA8 image, with a full mip chain (box filtered) so it minifies cleanly. */
    bool write_dds(const std::string &path, const std::uint8_t *rgba, int width, int height, DdsKind kind, bool mipmaps);
}
