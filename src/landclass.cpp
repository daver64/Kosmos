#include "landclass.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include <zlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace kosmos
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr char kMagic[8] = {'K', 'A', 'V', 'L', 'C', '0', '0', '1'};
        constexpr std::size_t kHeaderSize = 64;

        struct Header
        {
            char magic[8];
            std::uint32_t width, height, version, reserved;
            double lon0, lat_top, cell;
            std::uint8_t pad[16];
        };
        static_assert(sizeof(Header) == kHeaderSize);

        struct FileCloser
        {
            void operator()(void *f) const
            {
                if (f)
                    std::fclose(static_cast<std::FILE *>(f));
            }
        };

        const float kGroups[lc_count][group_count] = {
            // water snow urban farm forest barren grass scrub
            {0, 0, 0, 0, 0, 0, 0, 0},       // nodata
            {0, 0, 0, 0, 1, 0, 0, 0},       // needleleaf
            {0, 0, 0, 0, 1, 0, 0, 0},       // evergreen broadleaf
            {0, 0, 0, 0, 1, 0, 0, 0},       // deciduous broadleaf
            {0, 0, 0, 0, 1, 0, 0, 0},       // mixed trees
            {0, 0, 0, 0, 0, 0.25f, 0, 0.75f}, // shrubs
            {0, 0, 0, 0, 0, 0, 1, 0},       // herbaceous
            {0, 0, 0, 1, 0, 0, 0, 0},       // cultivated
            {0, 0, 0, 0, 0, 0, 1, 0},       // flooded
            {0, 0, 1, 0, 0, 0, 0, 0},       // urban
            {0, 1, 0, 0, 0, 0, 0, 0},       // snow
            {0, 0, 0, 0, 0, 1, 0, 0},       // barren
            {1, 0, 0, 0, 0, 0, 0, 0},       // water
        };

        std::uint16_t rd16(const std::uint8_t *p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
        std::uint32_t rd32(const std::uint8_t *p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                   (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }
    } // namespace

    const float *class_to_groups(int land_class)
    {
        return kGroups[std::clamp(land_class, 0, static_cast<int>(lc_count) - 1)];
    }

    const char *group_name(int group)
    {
        static const char *names[group_count] = {"water", "snow", "urban", "farm", "forest", "barren", "grass", "scrub"};
        return names[std::clamp(group, 0, static_cast<int>(group_count) - 1)];
    }

    std::size_t lzw_decode(const std::uint8_t *in, std::size_t in_size, std::uint8_t *out, std::size_t out_size)
    {
        // Each table entry is (prefix code, appended byte, length); strings are written back to front.
        struct Entry
        {
            std::uint16_t prefix;
            std::uint8_t byte;
            std::uint16_t length;
        };
        std::vector<Entry> table(4096);
        for (int i = 0; i < 256; ++i)
            table[i] = {0xFFFF, static_cast<std::uint8_t>(i), 1};
        int next = 258, bits = 9, old = -1;
        std::uint32_t buffer = 0;
        int have = 0;
        std::size_t pos = 0, produced = 0;
        while (true)
        {
            while (have < bits && pos < in_size)
            {
                buffer = (buffer << 8) | in[pos++];
                have += 8;
            }
            if (have < bits)
                break;
            const int code = static_cast<int>((buffer >> (have - bits)) & ((1u << bits) - 1));
            have -= bits;
            buffer &= (1u << have) - 1;
            if (code == 257)
                break;
            if (code == 256)
            {
                next = 258;
                bits = 9;
                old = -1;
                continue;
            }
            int first_source;
            int length;
            if (code < next && (code < 256 || code >= 258))
            {
                first_source = code;
                length = table[code].length;
            }
            else if (old >= 0)
            {
                first_source = old;
                length = table[old].length + 1;
            }
            else
                break;
            if (produced + length > out_size)
                break;
            std::uint8_t *dst = out + produced;
            std::uint8_t first_byte = 0;
            {
                int c = first_source;
                int n = table[c].length;
                if (code >= next || code == 257)
                {
                    // KwKwK case: string(old) + first byte of string(old)
                    int o = old;
                    while (table[o].prefix != 0xFFFF)
                        o = table[o].prefix;
                    first_byte = table[o].byte;
                    dst[length - 1] = first_byte;
                }
                int idx = n - 1;
                while (true)
                {
                    dst[idx] = table[c].byte;
                    if (table[c].prefix == 0xFFFF)
                        break;
                    c = table[c].prefix;
                    --idx;
                }
                first_byte = dst[0];
            }
            if (old >= 0 && next < 4096)
            {
                table[next] = {static_cast<std::uint16_t>(old), first_byte,
                               static_cast<std::uint16_t>(table[old].length + 1)};
                ++next;
            }
            produced += length;
            old = code;
            if (next + 1 >= (1 << bits) && bits < 12)
                ++bits;
        }
        return produced;
    }

    bool TiledTiff::open(const std::string &path, std::string &error)
    {
        path_ = path;
        std::FILE *f = std::fopen(path.c_str(), "rb");
        if (!f)
        {
            error = "cannot open " + path;
            return false;
        }
        file_ = std::shared_ptr<void>(f, FileCloser());
        std::uint8_t head[8];
        if (std::fread(head, 1, 8, f) != 8 || head[0] != 'I' || head[1] != 'I' || rd16(head + 2) != 42)
        {
            error = path + ": not a little-endian classic TIFF";
            return false;
        }
        std::fseek(f, static_cast<long>(rd32(head + 4)), SEEK_SET);
        std::uint8_t cnt[2];
        if (std::fread(cnt, 1, 2, f) != 2)
        {
            error = path + ": truncated";
            return false;
        }
        const int entries = rd16(cnt);
        std::vector<std::uint8_t> dir(static_cast<std::size_t>(entries) * 12);
        if (std::fread(dir.data(), 1, dir.size(), f) != dir.size())
        {
            error = path + ": truncated directory";
            return false;
        }
        auto read_values = [&](const std::uint8_t *e, std::vector<std::uint64_t> &out)
        {
            const int type = rd16(e + 2);
            const std::uint32_t count = rd32(e + 4);
            const int size = type == 3 ? 2 : 4;
            std::vector<std::uint8_t> raw(static_cast<std::size_t>(count) * size);
            if (raw.size() <= 4)
                std::memcpy(raw.data(), e + 8, raw.size());
            else
            {
                std::fseek(f, static_cast<long>(rd32(e + 8)), SEEK_SET);
                if (std::fread(raw.data(), 1, raw.size(), f) != raw.size())
                    return;
            }
            out.resize(count);
            for (std::uint32_t i = 0; i < count; ++i)
                out[i] = size == 2 ? rd16(raw.data() + i * 2) : rd32(raw.data() + i * 4);
        };
        for (int i = 0; i < entries; ++i)
        {
            const std::uint8_t *e = dir.data() + i * 12;
            const int tag = rd16(e);
            std::vector<std::uint64_t> v;
            switch (tag)
            {
            case 256: read_values(e, v); width_ = v.empty() ? 0 : static_cast<int>(v[0]); break;
            case 257: read_values(e, v); height_ = v.empty() ? 0 : static_cast<int>(v[0]); break;
            case 259: read_values(e, v); compression_ = v.empty() ? 1 : static_cast<int>(v[0]); break;
            case 322: read_values(e, v); tile_w_ = v.empty() ? 0 : static_cast<int>(v[0]); break;
            case 323: read_values(e, v); tile_h_ = v.empty() ? 0 : static_cast<int>(v[0]); break;
            case 324: read_values(e, offsets_); break;
            case 325: read_values(e, counts_); break;
            default: break;
            }
        }
        if (width_ <= 0 || height_ <= 0 || tile_w_ <= 0 || tile_h_ <= 0 ||
            offsets_.size() != static_cast<std::size_t>(tiles_across()) * tiles_down() || counts_.size() != offsets_.size())
        {
            error = path + ": not a tiled 8-bit TIFF";
            return false;
        }
        if (compression_ != 1 && compression_ != 5)
        {
            error = path + ": unsupported compression";
            return false;
        }
        return true;
    }

    bool TiledTiff::read_tile(int tile_x, int tile_y, std::vector<std::uint8_t> &out)
    {
        const std::size_t index = static_cast<std::size_t>(tile_y) * tiles_across() + tile_x;
        if (index >= offsets_.size())
            return false;
        std::FILE *f = static_cast<std::FILE *>(file_.get());
        scratch_.resize(static_cast<std::size_t>(counts_[index]));
#ifdef _WIN32
        if (_fseeki64(f, static_cast<__int64>(offsets_[index]), SEEK_SET) != 0 ||
#else
        if (fseeko(f, static_cast<off_t>(offsets_[index]), SEEK_SET) != 0 ||
#endif
            std::fread(scratch_.data(), 1, scratch_.size(), f) != scratch_.size())
            return false;
        const std::size_t size = static_cast<std::size_t>(tile_w_) * tile_h_;
        out.assign(size, 0);
        if (compression_ == 1)
            std::memcpy(out.data(), scratch_.data(), std::min(size, scratch_.size()));
        else
            lzw_decode(scratch_.data(), scratch_.size(), out.data(), size);
        return true;
    }

    struct Landclass::Mapping
    {
        void *address = nullptr;
        std::size_t size = 0;
#ifdef _WIN32
        HANDLE file = INVALID_HANDLE_VALUE, view = nullptr;
        ~Mapping()
        {
            if (address)
                UnmapViewOfFile(address);
            if (view)
                CloseHandle(view);
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
        }
#else
        ~Mapping()
        {
            if (address)
                munmap(address, size);
        }
#endif
    };

    Landclass::Landclass() = default;
    Landclass::~Landclass() = default;

    void Landclass::close()
    {
        mapping_.reset();
        owned_.clear();
        data_ = nullptr;
        width_ = height_ = 0;
    }

    void Landclass::set_cells(int width, int height, double lon0, double lat_top, double cell_degrees,
                              const std::vector<std::uint8_t> &cells)
    {
        close();
        width_ = width;
        height_ = height;
        lon0_ = lon0;
        lat_top_ = lat_top;
        cell_ = cell_degrees;
        stride_ = (static_cast<std::size_t>(width) + 1) / 2;
        owned_.assign(stride_ * height, 0);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const std::uint8_t v = cells[static_cast<std::size_t>(y) * width + x] & 15;
                owned_[y * stride_ + x / 2] |= (x & 1) ? static_cast<std::uint8_t>(v << 4) : v;
            }
        data_ = owned_.data();
    }

    namespace
    {
        bool build_cache(const std::string &directory, const std::string &cache_path, std::string &error,
                         const std::function<void(double, const std::string &)> &progress)
        {
            std::vector<std::string> files(lc_count);
            for (int c = 1; c < lc_count; ++c)
            {
                files[c] = directory + "/consensus_full_class_" + std::to_string(c) + ".tif";
                if (!fs::exists(files[c]))
                {
                    error = "missing " + files[c];
                    return false;
                }
            }
            TiledTiff probe;
            if (!probe.open(files[1], error))
                return false;
            const int width = probe.width(), height = probe.height();
            const int tw = probe.tile_width(), th = probe.tile_height();
            const int across = probe.tiles_across(), down = probe.tiles_down();
            const std::size_t stride = (static_cast<std::size_t>(width) + 1) / 2;

            std::vector<std::uint8_t> packed(stride * height, 0);
            std::atomic<int> next_row{0}, done_rows{0}, failed{0};
            std::mutex message_mutex;
            std::string failure;

            auto worker = [&]()
            {
                std::vector<TiledTiff> tiffs(lc_count);
                for (int c = 1; c < lc_count; ++c)
                {
                    std::string e;
                    if (!tiffs[c].open(files[c], e))
                    {
                        std::lock_guard<std::mutex> lock(message_mutex);
                        failure = e;
                        failed = 1;
                        return;
                    }
                }
                const std::size_t band = static_cast<std::size_t>(across) * tw;
                std::vector<std::uint16_t> best(band * th);
                std::vector<std::uint8_t> winner(band * th), tile;
                while (!failed)
                {
                    const int ty = next_row++;
                    if (ty >= down)
                        break;
                    std::fill(best.begin(), best.end(), 0);
                    std::fill(winner.begin(), winner.end(), 0);
                    for (int c = 1; c < lc_count; ++c)
                    {
                        const int weight = c == lc_urban ? 2 : 1;
                        for (int tx = 0; tx < across; ++tx)
                        {
                            if (!tiffs[c].read_tile(tx, ty, tile))
                            {
                                std::lock_guard<std::mutex> lock(message_mutex);
                                failure = "read error in " + files[c];
                                failed = 1;
                                return;
                            }
                            for (int y = 0; y < th; ++y)
                                for (int x = 0; x < tw; ++x)
                                {
                                    const std::uint8_t v = tile[static_cast<std::size_t>(y) * tw + x];
                                    if (v > 100)
                                        continue; // nodata (255)
                                    const std::uint16_t score = static_cast<std::uint16_t>(v * weight);
                                    const std::size_t i = static_cast<std::size_t>(y) * band + tx * tw + x;
                                    if (score > best[i])
                                    {
                                        best[i] = score;
                                        winner[i] = static_cast<std::uint8_t>(c);
                                    }
                                }
                        }
                    }
                    for (int y = 0; y < th && ty * th + y < height; ++y)
                    {
                        std::uint8_t *row = packed.data() + static_cast<std::size_t>(ty * th + y) * stride;
                        for (int x = 0; x < width; ++x)
                        {
                            const std::uint8_t v = winner[static_cast<std::size_t>(y) * band + x];
                            row[x / 2] |= (x & 1) ? static_cast<std::uint8_t>(v << 4) : v;
                        }
                    }
                    const int finished = ++done_rows;
                    if (progress)
                    {
                        std::lock_guard<std::mutex> lock(message_mutex);
                        progress(static_cast<double>(finished) / down, "condensing land cover");
                    }
                }
            };
            const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
            std::vector<std::thread> pool;
            for (unsigned i = 0; i < threads; ++i)
                pool.emplace_back(worker);
            for (auto &t : pool)
                t.join();
            if (failed)
            {
                error = failure;
                return false;
            }

            Header h{};
            std::memcpy(h.magic, kMagic, 8);
            h.width = static_cast<std::uint32_t>(width);
            h.height = static_cast<std::uint32_t>(height);
            h.version = Landclass::kCacheVersion;
            h.lon0 = -180.0;
            h.lat_top = 90.0;
            h.cell = 360.0 / width;
            const std::string temp = cache_path + ".tmp";
            std::FILE *f = std::fopen(temp.c_str(), "wb");
            if (!f)
            {
                error = "cannot write " + temp;
                return false;
            }
            const bool ok = std::fwrite(&h, sizeof h, 1, f) == 1 &&
                            std::fwrite(packed.data(), 1, packed.size(), f) == packed.size();
            std::fclose(f);
            if (!ok)
            {
                error = "write failed for " + temp;
                return false;
            }
            std::error_code ec;
            fs::rename(temp, cache_path, ec);
            if (ec)
            {
                error = "rename failed: " + ec.message();
                return false;
            }
            return true;
        }

        bool cache_is_fresh(const std::string &directory, const std::string &cache_path)
        {
            std::error_code ec;
            if (!fs::exists(cache_path, ec))
                return false;
            const auto cache_time = fs::last_write_time(cache_path, ec);
            for (int c = 1; c < lc_count; ++c)
            {
                const auto src = directory + "/consensus_full_class_" + std::to_string(c) + ".tif";
                if (fs::exists(src, ec) && fs::last_write_time(src, ec) > cache_time)
                    return false;
            }
            return true;
        }
    } // namespace

    namespace
    {
        /** The shipped cache is gzipped (about 30 MB for 380 MB); unpack it beside itself on first use so it can be memory mapped. */
        bool unpack_cache(const std::string &gz_path, const std::string &cache_path, std::string &error)
        {
            gzFile in = gzopen(gz_path.c_str(), "rb");
            if (!in)
            {
                error = "cannot open " + gz_path;
                return false;
            }
            const std::string partial = cache_path + ".part";
            std::FILE *out = std::fopen(partial.c_str(), "wb");
            bool ok = out != nullptr;
            if (!ok)
                error = "cannot write " + partial;
            std::vector<char> buffer(1 << 20);
            for (int n; ok && (n = gzread(in, buffer.data(), static_cast<unsigned>(buffer.size()))) > 0;)
                ok = std::fwrite(buffer.data(), 1, static_cast<std::size_t>(n), out) == static_cast<std::size_t>(n);
            int zerr = 0;
            gzerror(in, &zerr);
            if (ok && zerr != Z_OK && zerr != Z_STREAM_END)
            {
                ok = false;
                error = "cannot unpack " + gz_path;
            }
            gzclose(in);
            if (out)
                std::fclose(out);
            std::error_code ec;
            if (ok)
                fs::rename(partial, cache_path, ec);
            if (!ok || ec)
            {
                fs::remove(partial, ec);
                return false;
            }
            return true;
        }
    } // namespace

    bool Landclass::open(const std::string &source_directory, const std::string &cache_path, std::string &error,
                         const std::function<void(double, const std::string &)> &progress, bool force_rebuild)
    {
        close();
        if (!force_rebuild && !fs::exists(cache_path) && fs::exists(cache_path + ".gz"))
        {
            if (progress)
                progress(0.0, "Unpacking the land cover data");
            if (!unpack_cache(cache_path + ".gz", cache_path, error))
                return false;
        }
        if (force_rebuild || !cache_is_fresh(source_directory, cache_path))
        {
            if (!build_cache(source_directory, cache_path, error, progress))
            {
                // Fall back to a stale cache if the sources have gone.
                if (!fs::exists(cache_path))
                    return false;
            }
        }
        auto mapping = std::make_unique<Mapping>();
#ifdef _WIN32
        mapping->file = CreateFileA(cache_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (mapping->file == INVALID_HANDLE_VALUE)
        {
            error = "cannot open " + cache_path;
            return false;
        }
        LARGE_INTEGER file_size{};
        GetFileSizeEx(mapping->file, &file_size);
        mapping->size = static_cast<std::size_t>(file_size.QuadPart);
        mapping->view = CreateFileMappingA(mapping->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        void *address = mapping->view ? MapViewOfFile(mapping->view, FILE_MAP_READ, 0, 0, 0) : nullptr;
        if (!address || mapping->size < kHeaderSize)
        {
            error = "cannot map " + cache_path;
            return false;
        }
        mapping->address = address;
#else
        const int fd = ::open(cache_path.c_str(), O_RDONLY);
        if (fd < 0)
        {
            error = "cannot open " + cache_path;
            return false;
        }
        struct stat st{};
        fstat(fd, &st);
        void *address = mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);
        if (address == MAP_FAILED || static_cast<std::size_t>(st.st_size) < kHeaderSize)
        {
            error = "cannot map " + cache_path;
            return false;
        }
        mapping->address = address;
        mapping->size = static_cast<std::size_t>(st.st_size);
#endif
        Header h;
        std::memcpy(&h, address, sizeof h);
        const std::size_t stride = (static_cast<std::size_t>(h.width) + 1) / 2;
        if (std::memcmp(h.magic, kMagic, 8) != 0 || h.version != kCacheVersion ||
            mapping->size < kHeaderSize + stride * h.height)
        {
            error = cache_path + ": bad or outdated cache (use --rebuild-landclass)";
            return false;
        }
        mapping_ = std::move(mapping);
        width_ = static_cast<int>(h.width);
        height_ = static_cast<int>(h.height);
        lon0_ = h.lon0;
        lat_top_ = h.lat_top;
        cell_ = h.cell;
        stride_ = stride;
        data_ = static_cast<const std::uint8_t *>(address) + kHeaderSize;
        return true;
    }

    int Landclass::cell(int x, int y) const
    {
        if (y < 0 || y >= height_)
            return 0;
        x %= width_;
        if (x < 0)
            x += width_;
        const std::uint8_t b = data_[static_cast<std::size_t>(y) * stride_ + x / 2];
        return (x & 1) ? (b >> 4) : (b & 15);
    }

    int Landclass::class_at(double lon_deg, double lat_deg) const
    {
        if (!valid())
            return 0;
        return cell(static_cast<int>(std::floor((lon_deg - lon0_) / cell_)),
                    static_cast<int>(std::floor((lat_top_ - lat_deg) / cell_)));
    }

    LandSample Landclass::sample(double lon_deg, double lat_deg) const
    {
        LandSample s;
        if (!valid())
            return s;
        const double fx = (lon_deg - lon0_) / cell_ - 0.5;
        const double fy = (lat_top_ - lat_deg) / cell_ - 0.5;
        const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
        const float tx = static_cast<float>(fx - x0), ty = static_cast<float>(fy - y0);
        float total = 0;
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i)
            {
                const int c = cell(x0 + i, y0 + j);
                if (c == 0 || c >= lc_count)
                    continue;
                const float w = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
                s.weight[c] += w;
                total += w;
            }
        if (total <= 1e-6f)
            return s;
        for (float &w : s.weight)
            w /= total;
        s.valid = true;
        return s;
    }
} // namespace kosmos
