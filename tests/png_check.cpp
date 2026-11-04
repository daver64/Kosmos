// Reads every PNG under the given files/directories with libpng, the same library the game loads textures with, and reports
// each one it warns about or cannot decode (for example "iCCP: known incorrect sRGB profile"). PNG images embedded in .glb
// models are checked too, which is where most textures in Kosmos live.
//   cmake --build build --target png_check && build/png_check [--fix] [PATH...]
// --fix turns a bad iCCP chunk into an unused private chunk of the same length, in place (the pixels and the file size are
// untouched, so it works inside a .glb as well). With no PATH it scans ".".
// build*, .git, import and shipedit-backups directories are skipped.
#include <png.h>
#include <zlib.h>

#include "json.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    struct Report
    {
        std::vector<std::string> warnings;
        std::string error;
    };

    void on_warning(png_structp png, png_const_charp msg)
    {
        static_cast<Report *>(png_get_error_ptr(png))->warnings.emplace_back(msg);
    }

    void on_error(png_structp png, png_const_charp msg)
    {
        static_cast<Report *>(png_get_error_ptr(png))->error = msg;
        png_longjmp(png, 1);
    }

    struct Reader
    {
        const unsigned char *data;
        std::size_t size, pos = 0;
    };

    void read_memory(png_structp png, png_bytep out, png_size_t n)
    {
        Reader *r = static_cast<Reader *>(png_get_io_ptr(png));
        if (r->pos + n > r->size)
            png_error(png, "unexpected end of data");
        std::memcpy(out, r->data + r->pos, n);
        r->pos += n;
    }

    Report check(const unsigned char *data, std::size_t size)
    {
        Report report;
        if (size < 8 || png_sig_cmp(data, 0, 8) != 0)
        {
            report.error = "Not a PNG file";
            return report;
        }
        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &report, on_error, on_warning);
        png_infop info = png ? png_create_info_struct(png) : nullptr;
        if (!png || !info)
        {
            report.error = "libpng setup failed";
            return report;
        }
        Reader reader{data, size};
        std::vector<png_byte> pixels;
        std::vector<png_bytep> rows;
        if (setjmp(png_jmpbuf(png)))
        {
            png_destroy_read_struct(&png, &info, nullptr);
            return report;
        }
        png_set_read_fn(png, &reader, read_memory);
        png_read_info(png, info);
        const png_uint_32 h = png_get_image_height(png, info);
        png_read_update_info(png, info);
        const std::size_t stride = png_get_rowbytes(png, info);
        pixels.resize(stride * h);
        rows.resize(h);
        for (png_uint_32 y = 0; y < h; ++y)
            rows[y] = pixels.data() + y * stride;
        png_read_image(png, rows.data());
        png_read_end(png, info);
        png_destroy_read_struct(&png, &info, nullptr);
        return report;
    }

    // Renames every iCCP chunk to the private ancillary type "kcCp" and fixes its CRC; readers ignore chunks they do not know.
    int neutralise_iccp(unsigned char *data, std::size_t size)
    {
        int n = 0;
        std::size_t i = 8;
        while (i + 12 <= size)
        {
            const std::uint32_t len = (std::uint32_t(data[i]) << 24) | (std::uint32_t(data[i + 1]) << 16) | (std::uint32_t(data[i + 2]) << 8) | data[i + 3];
            if (i + 12 + len > size)
                break;
            if (std::memcmp(&data[i + 4], "iCCP", 4) == 0)
            {
                std::memcpy(&data[i + 4], "kcCp", 4);
                const uLong crc = crc32(crc32(0L, Z_NULL, 0), &data[i + 4], len + 4);
                for (int k = 0; k < 4; ++k)
                    data[i + 8 + len + k] = static_cast<unsigned char>(crc >> (24 - 8 * k));
                ++n;
            }
            i += 12 + len;
        }
        return n;
    }

    std::uint32_t le32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); }

    struct Image
    {
        std::string name;
        std::size_t offset, size;
    };

    // The PNG images stored in a .glb's binary chunk, as byte ranges within the file.
    std::vector<Image> glb_images(const std::vector<unsigned char> &d)
    {
        std::vector<Image> out;
        if (d.size() < 20 || std::memcmp(d.data(), "glTF", 4) != 0)
            return out;
        std::size_t bin_at = 0;
        nlohmann::json doc;
        for (std::size_t i = 12; i + 8 <= d.size();)
        {
            const std::uint32_t len = le32(&d[i]);
            if (i + 8 + len > d.size())
                break;
            if (std::memcmp(&d[i + 4], "JSON", 4) == 0)
                doc = nlohmann::json::parse(d.begin() + i + 8, d.begin() + i + 8 + len, nullptr, false);
            else if (std::memcmp(&d[i + 4], "BIN\0", 4) == 0 && !bin_at)
                bin_at = i + 8;
            i += 8 + len;
        }
        if (!bin_at || !doc.is_object() || !doc.contains("images") || !doc.contains("bufferViews"))
            return out;
        int n = 0;
        for (const nlohmann::json &img : doc["images"])
        {
            const int index = n++;
            if (!img.contains("bufferView"))
                continue;
            const nlohmann::json &bv = doc["bufferViews"][img["bufferView"].get<int>()];
            const std::size_t off = bin_at + bv.value("byteOffset", 0), len = bv.value("byteLength", 0);
            if (off + len <= d.size())
                out.push_back({img.value("name", "image " + std::to_string(index)), off, len});
        }
        return out;
    }

    bool skipped(const fs::path &dir)
    {
        const std::string name = dir.filename().string();
        return name.rfind("build", 0) == 0 || name == ".git" || name == "import" || name == "shipedit-backups";
    }
} // namespace

int main(int argc, char **argv)
{
    bool fix = false;
    std::vector<fs::path> roots;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--fix") == 0)
            fix = true;
        else
            roots.emplace_back(argv[i]);
    }
    if (roots.empty())
        roots.emplace_back(".");

    std::vector<fs::path> files;
    for (const fs::path &root : roots)
    {
        if (fs::is_regular_file(root))
        {
            files.push_back(root);
            continue;
        }
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied); it != fs::recursive_directory_iterator(); ++it)
        {
            if (it->is_directory() && skipped(it->path()))
                it.disable_recursion_pending();
            else if (it->is_regular_file())
            {
                std::string ext = it->path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".png" || ext == ".glb")
                    files.push_back(it->path());
            }
        }
    }
    std::sort(files.begin(), files.end());

    int flagged = 0, fixed = 0, images = 0;
    auto report = [&](const std::string &label, const Report &r) {
        ++flagged;
        for (const std::string &w : r.warnings)
            std::printf("%s: warning: %s\n", label.c_str(), w.c_str());
        if (!r.error.empty())
            std::printf("%s: ERROR: %s\n", label.c_str(), r.error.c_str());
    };
    for (const fs::path &file : files)
    {
        std::ifstream in(file, std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bool changed = false;
        auto examine = [&](const std::string &label, std::size_t offset, std::size_t size) {
            // glTF files also carry JPEGs, which libpng has no say over.
            if (label.back() == ']' && (size < 8 || png_sig_cmp(data.data() + offset, 0, 8) != 0))
                return;
            ++images;
            Report r = check(data.data() + offset, size);
            if (r.warnings.empty() && r.error.empty())
                return;
            report(label, r);
            const bool iccp = std::any_of(r.warnings.begin(), r.warnings.end(), [](const std::string &w) { return w.find("iCCP") != std::string::npos; });
            if (fix && iccp && r.error.empty() && neutralise_iccp(data.data() + offset, size) > 0)
            {
                changed = true;
                ++fixed;
                std::printf("  fixed: iCCP chunk disabled%s\n", check(data.data() + offset, size).warnings.empty() ? "" : " (other warnings remain)");
            }
        };
        std::string ext = file.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".glb")
        {
            for (const Image &img : glb_images(data))
                examine(file.string() + " [" + img.name + "]", img.offset, img.size);
        }
        else
            examine(file.string(), 0, data.size());
        if (changed)
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        }
    }

    std::printf("%zu files, %d images checked, %d flagged", files.size(), images, flagged);
    if (fix)
        std::printf(", %d fixed", fixed);
    std::printf("\n");
    return flagged > fixed ? 1 : 0;
}
