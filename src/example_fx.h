#pragma once

#include "sl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

// Shared pieces for the exflight and exorbit examples: procedural textures, lathe-turned models and synthesised
// engine sounds. Everything is generated at start-up, so the examples need no asset files.
namespace fx
{
    inline double smoothstep(double a, double b, double t)
    {
        const double x = std::clamp((t - a) / (b - a), 0.0, 1.0);
        return x * x * (3.0 - 2.0 * x);
    }

    // ---------------------------------------------------------------------------------------------------------
    // Models
    // ---------------------------------------------------------------------------------------------------------

    /** Solid of revolution about the Z axis from (z, radius) profile points; the caller destroys the mesh. */
    inline sl::Mesh *make_lathe_mesh(const std::vector<std::pair<float, float>> &profile, int segments = 24)
    {
        std::vector<sl::Vertex3D> vertices;
        std::vector<std::uint32_t> indices;
        const int rows = static_cast<int>(profile.size());
        for (int i = 0; i < rows; ++i)
        {
            const int a = std::max(i - 1, 0), b = std::min(i + 1, rows - 1);
            const float dz = profile[b].first - profile[a].first;
            const float dr = profile[b].second - profile[a].second;
            const float len = std::max(std::sqrt(dz * dz + dr * dr), 1e-6f);
            for (int s = 0; s <= segments; ++s)
            {
                const float angle = 6.2831853f * static_cast<float>(s) / static_cast<float>(segments);
                const float cs = std::cos(angle), sn = std::sin(angle);
                sl::Vertex3D v;
                v.position[0] = cs * profile[i].second;
                v.position[1] = sn * profile[i].second;
                v.position[2] = profile[i].first;
                v.normal[0] = cs * dz / len;
                v.normal[1] = sn * dz / len;
                v.normal[2] = -dr / len;
                v.uv[0] = static_cast<float>(s) / static_cast<float>(segments);
                v.uv[1] = static_cast<float>(i) / static_cast<float>(rows - 1);
                vertices.push_back(v);
            }
        }
        for (int i = 0; i + 1 < rows; ++i)
            for (int s = 0; s < segments; ++s)
            {
                const std::uint32_t p = static_cast<std::uint32_t>(i * (segments + 1) + s), q = p + segments + 1;
                indices.insert(indices.end(), {p, p + 1, q, p + 1, q + 1, q});
            }
        return sl::create_mesh(vertices, indices);
    }

    /** A small single-engine jet, 9 m long with the nose along -Z, built from lathe meshes and boxes. */
    class Jet
    {
    public:
        bool create()
        {
            cube = sl::create_cube_mesh(1.0f, {255, 255, 255, 255});
            sphere = sl::create_sphere_mesh(1.0f, 16, 10, {255, 255, 255, 255});
            body = make_lathe_mesh({{-4.0f, 0.0f}, {-3.8f, 0.28f}, {-3.0f, 0.55f}, {-1.5f, 0.7f}, {1.5f, 0.68f}, {3.4f, 0.45f}, {4.2f, 0.36f}, {4.2f, 0.0f}});
            nose = make_lathe_mesh({{-5.0f, 0.0f}, {-4.9f, 0.1f}, {-4.4f, 0.24f}, {-4.0f, 0.3f}, {-4.0f, 0.0f}});
            nozzle = make_lathe_mesh({{4.2f, 0.0f}, {4.2f, 0.37f}, {4.7f, 0.32f}, {4.7f, 0.0f}});
            return cube && sphere && body && nose && nozzle;
        }

        void destroy()
        {
            for (sl::Mesh **m : {&cube, &sphere, &body, &nose, &nozzle})
            {
                if (*m)
                    sl::destroy_mesh(*m);
                *m = nullptr;
            }
        }

        /** Draws the aircraft with `base` as its model matrix; `wheels` are the gear positions in the model frame. */
        void draw(const glm::mat4 &base, const std::vector<glm::vec3> &wheels) const
        {
            sl::Material paint;
            const auto place = [&](sl::Mesh *mesh, const glm::mat4 &local, sl::Colour colour)
            {
                paint.tint = colour;
                sl::draw_mesh(mesh, base * local, paint);
            };
            const auto box = [&](glm::vec3 position, glm::vec3 size, sl::Colour colour)
            { place(cube, glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), size), colour); };
            const auto angled = [&](glm::vec3 position, glm::vec3 size, glm::vec3 axis, float degrees, sl::Colour colour)
            {
                place(cube, glm::translate(glm::mat4(1.0f), position) * glm::rotate(glm::mat4(1.0f), glm::radians(degrees), axis) *
                                glm::scale(glm::mat4(1.0f), size),
                      colour);
            };
            const sl::Colour white{225, 228, 235, 255}, red{200, 60, 50, 255}, grey{205, 208, 218, 255}, dark{40, 40, 45, 255};
            place(body, glm::mat4(1.0f), white);
            place(nozzle, glm::mat4(1.0f), {55, 55, 60, 255});
            place(nose, glm::mat4(1.0f), red);
            place(sphere, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, -2.3f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 0.38f, 1.2f)),
                  {40, 70, 110, 255});
            angled({2.9f, -0.15f, 0.7f}, {5.2f, 0.1f, 1.7f}, {0, 1, 0}, -32.0f, grey);
            angled({-2.9f, -0.15f, 0.7f}, {5.2f, 0.1f, 1.7f}, {0, 1, 0}, 32.0f, grey);
            angled({1.3f, 0.05f, 3.5f}, {2.4f, 0.08f, 1.0f}, {0, 1, 0}, -30.0f, grey);
            angled({-1.3f, 0.05f, 3.5f}, {2.4f, 0.08f, 1.0f}, {0, 1, 0}, 30.0f, grey);
            angled({0.0f, 1.2f, 3.3f}, {0.1f, 1.9f, 1.5f}, {1, 0, 0}, 22.0f, red);
            box({0.75f, -0.05f, -1.7f}, {0.35f, 0.5f, 1.0f}, dark);
            box({-0.75f, -0.05f, -1.7f}, {0.35f, 0.5f, 1.0f}, dark);
            box({5.4f, -0.15f, 2.2f}, {0.12f, 0.3f, 0.9f}, red);
            box({-5.4f, -0.15f, 2.2f}, {0.12f, 0.3f, 0.9f}, red);
            for (const glm::vec3 &w : wheels)
                box(w, {0.25f, 0.5f, 0.5f}, {30, 30, 30, 255});
        }

    private:
        sl::Mesh *cube = nullptr, *sphere = nullptr, *body = nullptr, *nose = nullptr, *nozzle = nullptr;
    };

    /** An Apollo-style stack: a cone command module on a cylindrical service module with an engine bell, nose along -Z. */
    class Capsule
    {
    public:
        bool create()
        {
            cube = sl::create_cube_mesh(1.0f, {255, 255, 255, 255});
            service = make_lathe_mesh({{-1.2f, 0.0f}, {-1.2f, 1.9f}, {4.0f, 1.9f}, {4.0f, 0.0f}});
            band = make_lathe_mesh({{-1.5f, 1.95f}, {-1.5f, 1.95f}, {-1.1f, 1.95f}, {-1.1f, 1.95f}}, 24);
            command = make_lathe_mesh({{-4.8f, 0.0f}, {-4.7f, 0.4f}, {-1.5f, 1.9f}, {-1.5f, 0.0f}});
            bell = make_lathe_mesh({{4.0f, 0.5f}, {4.6f, 0.55f}, {5.8f, 1.3f}, {5.6f, 1.3f}, {4.4f, 0.4f}});
            return cube && service && band && command && bell;
        }

        void destroy()
        {
            for (sl::Mesh **m : {&cube, &service, &band, &command, &bell})
            {
                if (*m)
                    sl::destroy_mesh(*m);
                *m = nullptr;
            }
        }

        void draw(const glm::mat4 &hull) const
        {
            sl::Material metal;
            const auto piece = [&](sl::Mesh *mesh, sl::Colour colour)
            {
                metal.tint = colour;
                sl::draw_mesh(mesh, hull, metal);
            };
            piece(service, {205, 205, 212, 255});
            piece(band, {215, 170, 60, 255});
            piece(command, {235, 235, 240, 255});
            piece(bell, {60, 60, 66, 255});
            metal.tint = {60, 60, 66, 255};
            for (int i = 0; i < 4; ++i)
                sl::draw_mesh(cube, hull * glm::rotate(glm::mat4(1.0f), glm::radians(45.0f + 90.0f * i), glm::vec3(0, 0, 1)) *
                                        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 3.0f)) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(0.35f, 0.7f, 0.7f)),
                              metal);
        }

    private:
        sl::Mesh *cube = nullptr, *service = nullptr, *band = nullptr, *command = nullptr, *bell = nullptr;
    };

    // ---------------------------------------------------------------------------------------------------------
    // Terrain detail textures
    // ---------------------------------------------------------------------------------------------------------

    namespace detail
    {
        inline float lattice(int x, int y, int period, int seed)
        {
            x = ((x % period) + period) % period;
            y = ((y % period) + period) % period;
            std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u +
                              static_cast<std::uint32_t>(seed) * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return static_cast<float>((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
        }

        // Value noise on a lattice that wraps every `period` cells, so the result tiles.
        inline float tiling_noise(float u, float v, int period, int seed)
        {
            const float x = u * period, y = v * period;
            const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
            const float fx = x - ix, fy = y - iy;
            const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
            const float a = lattice(ix, iy, period, seed), b = lattice(ix + 1, iy, period, seed);
            const float c = lattice(ix, iy + 1, period, seed), d = lattice(ix + 1, iy + 1, period, seed);
            return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
        }
    }

    /** A tileable noise texture with the given average colour: a detail layer that makes terrain look less flat. */
    inline sl::Bitmap *make_detail_texture(sl::Colour average, int seed, float contrast, float fine, int size = 256)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        std::vector<float> values(static_cast<std::size_t>(size) * size);
        double sum = 0.0;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const float u = static_cast<float>(x) / size, v = static_cast<float>(y) / size;
                float n = 0.0f, amplitude = 0.5f, total = 0.0f;
                for (int octave = 0; octave < 5; ++octave)
                {
                    n += amplitude * detail::tiling_noise(u, v, 8 << octave, seed + octave * 17);
                    total += amplitude;
                    amplitude *= 0.55f;
                }
                n /= total;
                const float speckle = detail::lattice(x, y, size, seed + 99) - 0.5f;
                const float value = (n - 0.5f) * contrast + speckle * fine;
                values[static_cast<std::size_t>(y) * size + x] = value;
                sum += value;
            }
        const float mean = static_cast<float>(sum / values.size());
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const float k = 1.0f + values[static_cast<std::size_t>(y) * size + x] - mean;
                const auto channel = [&](std::uint8_t c) { return static_cast<std::uint8_t>(std::clamp(c * k, 0.0f, 255.0f)); };
                sl::putpixel(bitmap, x, y, {channel(average.red), channel(average.green), channel(average.blue), 255});
            }
        return bitmap;
    }

    /**
     * The default sand, grass, rock and snow layers with a detail texture each, in the same tone as the layer colour.
     * `textures` receives every bitmap created; the last one is a large-scale brightness texture for
     * TerrainTextureOptions::macro_texture. Destroy them after Terrain::set_layer_textures().
     */
    inline std::vector<sl::TerrainLayer> detailed_layers(double low, double high, std::vector<sl::Bitmap *> &textures)
    {
        std::vector<sl::TerrainLayer> layers = sl::default_terrain_layers(low, high);
        // Contrast, speckle and tile size per layer.
        const float contrast[] = {0.45f, 0.75f, 0.9f, 0.25f};
        const float fine[] = {0.30f, 0.40f, 0.35f, 0.12f};
        const double tile[] = {7.0, 9.0, 14.0, 10.0};
        for (std::size_t i = 0; i < layers.size() && i < 4; ++i)
        {
            sl::Bitmap *bitmap = make_detail_texture(layers[i].colour, 31 + static_cast<int>(i) * 7, contrast[i], fine[i]);
            textures.push_back(bitmap);
            layers[i].albedo = bitmap;
            layers[i].tile_metres = tile[i];
        }
        textures.push_back(make_detail_texture({200, 200, 200, 255}, 77, 0.9f, 0.0f, 128));
        return layers;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Planet and moon textures
    // ---------------------------------------------------------------------------------------------------------

    namespace detail
    {
        inline double lattice3(int x, int y, int z, std::uint32_t seed)
        {
            std::uint32_t h = static_cast<std::uint32_t>(x) * 73856093u ^ static_cast<std::uint32_t>(y) * 19349663u ^
                              static_cast<std::uint32_t>(z) * 83492791u ^ (0x9e3779b9u + seed * 2654435761u);
            h ^= h >> 16;
            h *= 0x7feb352du;
            h ^= h >> 15;
            h *= 0x846ca68bu;
            h ^= h >> 16;
            return static_cast<double>(h >> 8) / 16777215.0;
        }

        inline double value_noise3(double x, double y, double z, std::uint32_t seed)
        {
            const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y)), zi = static_cast<int>(std::floor(z));
            const double fx = x - xi, fy = y - yi, fz = z - zi;
            const double ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy), uz = fz * fz * (3 - 2 * fz);
            const auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
            double row[2][2];
            for (int dy = 0; dy < 2; ++dy)
                for (int dz = 0; dz < 2; ++dz)
                    row[dy][dz] = lerp(lattice3(xi, yi + dy, zi + dz, seed), lattice3(xi + 1, yi + dy, zi + dz, seed), ux);
            return lerp(lerp(row[0][0], row[1][0], uy), lerp(row[0][1], row[1][1], uy), uz);
        }

        // Fractal noise by 3D direction, so an equirectangular map is continuous at the wrap and the poles.
        inline double sphere_fbm(const sl::dvec3 &d, double base_frequency, int octaves, std::uint32_t seed)
        {
            double sum = 0.0, amplitude = 0.5, frequency = base_frequency, total = 0.0;
            for (int i = 0; i < octaves; ++i)
            {
                sum += amplitude * value_noise3(d.x * frequency + 17.0 * i, d.y * frequency, d.z * frequency, seed);
                total += amplitude;
                amplitude *= 0.5;
                frequency *= 2.0;
            }
            return sum / total;
        }
    }

    /** Seamless equirectangular cloud cover: white with noise-driven alpha. The caller destroys the bitmap. */
    inline sl::Bitmap *make_cloud_texture(int w = 1024, int h = 512, double cover_amount = 0.5, std::uint32_t seed = 0)
    {
        // Less cover raises the threshold at which the noise turns to cloud; Earth's default is 0.5.
        const double lo = 0.36 + (0.5 - cover_amount) * 0.5, hi = lo + 0.26 + (0.5 - cover_amount) * 0.9;
        const double opacity = 215.0 * std::min(1.0, 0.45 + cover_amount);
        sl::Bitmap *bitmap = sl::create_bitmap(w, h);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < h; ++y)
        {
            const double latitude = (0.5 - (y + 0.5) / h) * 3.14159265358979;
            for (int x = 0; x < w; ++x)
            {
                const double longitude = ((x + 0.5) / w) * 2.0 * 3.14159265358979;
                const sl::dvec3 direction{std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                                          std::cos(latitude) * std::sin(longitude)};
                const double cover = smoothstep(lo, hi, detail::sphere_fbm(direction, 3.0, 6, seed));
                sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(cover * opacity)});
            }
        }
        bitmap->smooth = true;
        return bitmap;
    }

    /** Equirectangular grey mottling for a moon: maria, highlands and fine grain. The caller destroys the bitmap. */
    inline sl::Bitmap *make_moon_texture(int w = 1024, int h = 512)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(w, h);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < h; ++y)
        {
            const double latitude = (0.5 - (y + 0.5) / h) * 3.14159265358979;
            for (int x = 0; x < w; ++x)
            {
                const double longitude = ((x + 0.5) / w) * 2.0 * 3.14159265358979;
                const sl::dvec3 d{std::cos(latitude) * std::cos(longitude), std::sin(latitude), std::cos(latitude) * std::sin(longitude)};
                const double maria = smoothstep(0.42, 0.58, detail::sphere_fbm(d, 2.0, 4, 5));
                const double grain = detail::sphere_fbm(d, 24.0, 4, 9) - 0.5;
                const double v = std::clamp(0.42 + 0.30 * maria + 0.35 * grain, 0.0, 1.0);
                const auto c = static_cast<std::uint8_t>(v * 255.0);
                sl::putpixel(bitmap, x, y, {c, c, static_cast<std::uint8_t>(c * 0.96), 255});
            }
        }
        bitmap->smooth = true;
        return bitmap;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Engine sounds: short seamless loops, played with set_voice_volume() following the throttle
    // ---------------------------------------------------------------------------------------------------------

    namespace detail
    {
        constexpr int sound_rate = 44100;
        constexpr int loop_frames = sound_rate * 2;

        struct Noise
        {
            std::uint32_t state;
            float next()
            {
                state = state * 1664525u + 1013904223u;
                return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
            }
        };

        // Calls `shape(i, noise)` for loop_frames + crossfade samples and folds the tail into the head, so the loop has
        // no click. Tones are added afterwards at integer numbers of cycles per loop, which also keeps them seamless.
        template <typename Shape, typename Tone>
        inline sl::Sample *build_loop(Shape shape, Tone tone, std::uint32_t seed)
        {
            constexpr int fade = sound_rate / 4;
            Noise noise{seed};
            std::vector<float> raw(loop_frames + fade);
            for (int i = 0; i < loop_frames + fade; ++i)
                raw[i] = shape(i, noise);
            std::vector<std::int16_t> pcm(loop_frames);
            for (int i = 0; i < loop_frames; ++i)
            {
                float v = raw[i];
                if (i < fade)
                {
                    const float k = 1.5707963f * static_cast<float>(i) / fade;
                    v = raw[i] * std::sin(k) + raw[loop_frames + i] * std::cos(k);
                }
                v += tone(static_cast<double>(i) / loop_frames);
                pcm[i] = static_cast<std::int16_t>(std::clamp(v, -1.0f, 1.0f) * 30000.0f);
            }
            return sl::create_sample_from_pcm(pcm.data(), pcm.size(), sound_rate, 1);
        }

        inline float cycles(double phase, int count) { return static_cast<float>(std::sin(6.283185307179586 * count * phase)); }
    }

    /** Idle jet: a low, throaty rumble with a faint turbine whine. */
    inline sl::Sample *make_jet_idle_sound()
    {
        float low = 0.0f, rumble = 0.0f;
        return detail::build_loop(
            [&](int, detail::Noise &n)
            {
                const float v = n.next();
                low += 0.05f * (v - low);
                rumble += 0.006f * (v - rumble);
                return low * 0.9f + rumble * 6.0f;
            },
            [](double phase) { return 0.018f * detail::cycles(phase, 300) + 0.008f * detail::cycles(phase, 601); }, 0x777u);
    }

    /** Full-power jet: a broadband roar with a high whine. */
    inline sl::Sample *make_jet_power_sound()
    {
        float a = 0.0f, b = 0.0f, c = 0.0f;
        return detail::build_loop(
            [&](int, detail::Noise &n)
            {
                const float v = n.next();
                a += 0.2f * (v - a);
                b += 0.04f * (v - b);
                c += 0.2f * (a - c);
                return c * 1.5f + b * 2.2f;
            },
            [](double phase)
            { return 0.06f * (detail::cycles(phase, 700) + 0.45f * detail::cycles(phase, 1403) + 0.2f * detail::cycles(phase, 2101)); },
            0x999u);
    }

    /** Wind: low-passed noise with slow gusts. */
    inline sl::Sample *make_wind_sound()
    {
        float low = 0.0f, slow = 0.0f;
        return detail::build_loop(
            [&](int i, detail::Noise &n)
            {
                const float v = n.next();
                low += 0.03f * (v - low);
                slow += 0.004f * (v - slow);
                const double t = static_cast<double>(i) / detail::loop_frames;
                const float gust = 0.7f + 0.3f * static_cast<float>(std::sin(6.2831853 * 2.0 * t));
                return (low * 2.0f + slow * 5.0f) * gust * 0.5f;
            },
            [](double) { return 0.0f; }, 0x1234u);
    }

    /** Rocket engine: a deep, crackling, low-passed rumble. */
    inline sl::Sample *make_rocket_sound()
    {
        float deep = 0.0f, mid = 0.0f;
        return detail::build_loop(
            [&](int, detail::Noise &n)
            {
                const float v = n.next();
                deep += 0.012f * (v - deep);
                mid += 0.12f * (v - mid);
                return (deep * 9.0f + mid * 1.1f * (0.6f + 0.4f * n.next())) * 0.5f;
            },
            [](double phase) { return 0.12f * detail::cycles(phase, 76); }, 0x555u);
    }

    /** Thruster hiss for the reaction control system. */
    inline sl::Sample *make_hiss_sound()
    {
        float low = 0.0f;
        return detail::build_loop(
            [&](int, detail::Noise &n)
            {
                const float v = n.next();
                low += 0.35f * (v - low);
                return (v - low) * 0.35f;
            },
            [](double) { return 0.0f; }, 0x31u);
    }
}
