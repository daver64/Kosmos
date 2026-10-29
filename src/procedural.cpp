#include "procedural.h"

#include "example_fx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
    } // namespace

    // A soft star disc: a bright core, a warm halo and a faint wide glow, as alpha for an additive billboard.
    sl::Bitmap *make_glow_texture(int size)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy);
                const double core = 1.0 - fx::smoothstep(0.05, 0.09, r);
                const double a = std::clamp(core + 0.8 * std::exp(-r * 6.0) + 0.4 * std::exp(-r * 2.6), 0.0, 1.0) * (1.0 - fx::smoothstep(0.8, 1.0, r));
                sl::putpixel(bitmap, x, y, {255, static_cast<std::uint8_t>(215 + 40 * core), static_cast<std::uint8_t>(110 + 140 * core), static_cast<std::uint8_t>(a * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    // A star's photosphere as an equirectangular map, grey so the star's colour can tint it. Layer 0 is the opaque surface:
    // domain-warped granulation, broad swirls and dark spots. Layer 1 is white filaments in the alpha channel, drawn
    // additively on a second, counter-rotating shell so the surface seems to churn.
    sl::Bitmap *make_star_surface(int layer, std::uint32_t seed, int width)
    {
        const int height = width / 2;
        sl::Bitmap *bitmap = sl::create_bitmap(width, height);
        if (!bitmap)
            return nullptr;
        using fx::detail::sphere_fbm;
        for (int y = 0; y < height; ++y)
        {
            const double lat = 1.5707963 - (y + 0.5) / height * 3.1415927;
            for (int x = 0; x < width; ++x)
            {
                const double lon = (x + 0.5) / width * 6.2831853;
                const sl::dvec3 d(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon));
                const sl::dvec3 warp(sphere_fbm(d, 1.7, 3, seed + 1) - 0.5, sphere_fbm(d, 1.7, 3, seed + 2) - 0.5, sphere_fbm(d, 1.7, 3, seed + 3) - 0.5);
                // A second warp on the first gives the curling, swirled look rather than plain clouds.
                const sl::dvec3 p1 = d + 1.1 * warp;
                const sl::dvec3 warp2(sphere_fbm(p1, 3.0, 2, seed + 4) - 0.5, sphere_fbm(p1, 3.0, 2, seed + 5) - 0.5, sphere_fbm(p1, 3.0, 2, seed + 6) - 0.5);
                const sl::dvec3 p = d + 1.1 * warp + 0.7 * warp2;
                if (layer == 0)
                {
                    const double cells = sphere_fbm(p, 6.0, 3, seed + 7);
                    const double swirl = sphere_fbm(p, 3.2, 3, seed + 8);
                    const double spots = fx::smoothstep(0.60, 0.70, sphere_fbm(d, 2.6, 3, seed + 9));
                    double v = 0.82 + 0.9 * (cells - 0.5) + 0.7 * (swirl - 0.5);
                    v *= 1.0 - 0.6 * spots;
                    // Lift the floor so the granulation stays bright; the additive pass in draw_photosphere supplies the HDR-style glow.
                    v = 0.6 + 0.4 * std::clamp(v, 0.0, 1.0);
                    const auto g = static_cast<std::uint8_t>(255.0 * v);
                    sl::putpixel(bitmap, x, y, {g, g, g, 255});
                }
                else
                {
                    const double ridge = 1.0 - std::fabs(2.0 * sphere_fbm(p, 2.5, 3, seed + 11) - 1.0);
                    const double f = std::pow(ridge, 4.0) * (0.5 + 0.8 * sphere_fbm(d, 2.0, 2, seed + 12));
                    sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(255.0 * std::clamp(f, 0.0, 1.0))});
                }
            }
        }
        bitmap->smooth = true;
        return bitmap;
    }

    // The corona: streamers that fall off with distance from the limb, as alpha. The disc edge sits at 1/scale of the half-width.
    sl::Bitmap *make_star_corona(std::uint32_t seed, double scale, int size)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        using fx::detail::value_noise3;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy) * scale; // in star radii
                const double theta = std::atan2(dy, dx);
                const double streak = 0.35 + 1.1 * (0.6 * value_noise3(std::cos(theta) * 2.6, std::sin(theta) * 2.6, r * 0.18, seed) +
                                                    0.4 * value_noise3(std::cos(theta) * 7.0, std::sin(theta) * 7.0, r * 0.3, seed + 3));
                const double fall = r < 1.0 ? 1.0 : std::exp(-(r - 1.0) * 1.15) * 0.8 + 0.2 * std::exp(-(r - 1.0) * 0.3);
                const double edge = 1.0 - fx::smoothstep(0.82, 1.0, r / scale);
                const double a = std::clamp(fall * streak * edge * 0.9, 0.0, 1.0);
                sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(a * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    // Diffraction spikes: a bright centre and thin rays along the axes, as alpha.
    sl::Bitmap *make_star_rays(int size)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy);
                const double theta = std::atan2(dy, dx);
                const double spike = std::pow(std::fabs(std::cos(2.0 * theta)), 90.0);
                const double a = (0.9 * spike * std::exp(-r * 4.2) + 0.5 * std::exp(-r * 14.0)) * (1.0 - fx::smoothstep(0.85, 1.0, r));
                sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(std::clamp(a, 0.0, 1.0) * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    // Limb darkening: black at the edge of the disc, clear at the centre, nothing beyond. Alpha-blended over the photosphere.
    sl::Bitmap *make_star_limb(int size)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy);
                const double a = r < 1.0 ? 0.8 * std::pow(r, 4.0) : 0.0;
                sl::putpixel(bitmap, x, y, {0, 0, 0, static_cast<std::uint8_t>(a * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    sl::Bitmap *make_puff_texture(int size)
    {
        sl::Bitmap *bitmap = sl::create_bitmap(size, size);
        if (!bitmap)
            return nullptr;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const double dx = (x + 0.5) / size * 2.0 - 1.0, dy = (y + 0.5) / size * 2.0 - 1.0;
                const double r = std::sqrt(dx * dx + dy * dy);
                const double a = std::pow(std::max(0.0, 1.0 - r), 2.0);
                sl::putpixel(bitmap, x, y, {255, 255, 255, static_cast<std::uint8_t>(a * 255.0)});
            }
        bitmap->smooth = true;
        return bitmap;
    }

    // Stars, a Milky Way band and a few nebulae as soft sprites on a unit sphere about the camera.
    sl::Mesh *make_starfield()
    {
        std::mt19937 rng(20240611);
        auto uniform = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
        std::vector<sl::Vertex3D> vertices;
        std::vector<std::uint32_t> indices;
        auto add = [&](const sl::dvec3 &dir, double radius, const glm::vec3 &colour, float alpha)
        {
            const sl::dvec3 helper = std::fabs(dir.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0);
            const sl::dvec3 u = glm::normalize(glm::cross(dir, helper)), v = glm::cross(dir, u);
            const float corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
            for (int i = 0; i < 4; ++i)
            {
                sl::Vertex3D vertex;
                const sl::dvec3 p = dir + u * (corner[i][0] * radius) + v * (corner[i][1] * radius);
                for (int c = 0; c < 3; ++c)
                {
                    vertex.position[c] = static_cast<float>(p[c]);
                    vertex.colour[c] = colour[c];
                }
                vertex.colour[3] = alpha;
                vertex.uv[0] = 0.5f + 0.5f * corner[i][0];
                vertex.uv[1] = 0.5f - 0.5f * corner[i][1];
                vertices.push_back(vertex);
            }
            for (const std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                indices.push_back(base + k);
        };
        auto random_dir = [&]()
        {
            const double z = uniform(-1.0, 1.0), a = uniform(0.0, 2.0 * pi), r = std::sqrt(1.0 - z * z);
            return sl::dvec3(r * std::cos(a), z, r * std::sin(a));
        };
        for (int i = 0; i < 4500; ++i)
        {
            const double brightness = std::pow(uniform(0.0, 1.0), 3.0), temperature = uniform(0.0, 1.0);
            const glm::vec3 colour = temperature < 0.2 ? glm::vec3(1.0f, 0.75f, 0.55f) : temperature > 0.75 ? glm::vec3(0.65f, 0.78f, 1.0f) : glm::vec3(1.0f, 0.97f, 0.9f);
            add(random_dir(), 0.0008 + 0.0017 * brightness, colour, static_cast<float>(0.3 + 0.4 * brightness));
        }
        return sl::create_mesh(vertices, indices);
    }

    // Thin, wispy nebulae for one star system as an equirectangular map for the inside-out sky sphere (drawn additively, so dim): ridged
    // filaments warped into tendrils, gathered into a few large drifts with clear sky between, in two or three hues the system picks.
    sl::Bitmap *make_nebula_texture(int system, int width)
    {
        const int height = width / 2;
        sl::Bitmap *bitmap = sl::create_bitmap(width, height);
        if (!bitmap)
            return nullptr;
        using fx::detail::sphere_fbm;
        std::mt19937 rng(0x9e3779b9u ^ (static_cast<std::uint32_t>(system) * 2654435761u));
        auto uniform = [&](double lo, double hi) { return static_cast<float>(std::uniform_real_distribution<double>(lo, hi)(rng)); };
        const std::uint32_t seed = static_cast<std::uint32_t>(rng() % 100000) + 300;
        // Hue pairs: emission red and blue, teal and violet, amber and rose.
        const glm::vec3 hues[4][2] = {{{0.95f, 0.32f, 0.42f}, {0.35f, 0.5f, 1.0f}}, {{0.25f, 0.8f, 0.75f}, {0.6f, 0.35f, 0.95f}},
                                      {{1.0f, 0.62f, 0.3f}, {0.9f, 0.35f, 0.6f}}, {{0.4f, 0.55f, 1.0f}, {0.85f, 0.45f, 0.75f}}};
        const glm::vec3 *pair = hues[rng() % 4];
        const float drift_scale = uniform(1.1f, 1.8f); // fewer, larger drifts at a low value
        for (int y = 0; y < height; ++y)
        {
            const double lat = 1.5707963 - (y + 0.5) / height * 3.1415927;
            for (int x = 0; x < width; ++x)
            {
                const double lon = (x + 0.5) / width * 6.2831853;
                const sl::dvec3 d(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon));
                // Where the nebula is at all: broad regions, soft at the edges.
                const double region = fx::smoothstep(0.5, 0.8, sphere_fbm(d, drift_scale, 3, seed));
                if (region <= 0.0)
                {
                    sl::putpixel(bitmap, x, y, {0, 0, 0, 255});
                    continue;
                }
                // Warp the lookup so the filaments curl and trail instead of forming blobs.
                const sl::dvec3 warp(sphere_fbm(d, 2.5, 3, seed + 1) - 0.5, sphere_fbm(d, 2.5, 3, seed + 2) - 0.5, sphere_fbm(d, 2.5, 3, seed + 3) - 0.5);
                const sl::dvec3 q = glm::normalize(d + warp * 0.9);
                const double ridge = 1.0 - std::fabs(2.0 * sphere_fbm(q, 3.0, 5, seed + 4) - 1.0);
                const double fine = 1.0 - std::fabs(2.0 * sphere_fbm(q, 9.0, 3, seed + 5) - 1.0);
                const double wisp = std::pow(ridge, 5.0) * (0.55 + 0.45 * std::pow(fine, 3.0));
                const double tint = sphere_fbm(d, 2.0, 2, seed + 6);
                const glm::vec3 hue = glm::mix(pair[0], pair[1], static_cast<float>(fx::smoothstep(0.35, 0.65, tint)));
                glm::vec3 colour = hue * static_cast<float>(wisp * region * 0.5);
                colour = glm::min(colour, glm::vec3(0.4f));
                sl::putpixel(bitmap, x, y, {static_cast<std::uint8_t>(colour.r * 255.0f), static_cast<std::uint8_t>(colour.g * 255.0f), static_cast<std::uint8_t>(colour.b * 255.0f), 255});
            }
        }
        bitmap->smooth = true;
        return bitmap;
    }

    // The Milky Way band and faint dust lanes as an equirectangular map for an inside-out sphere. Drawn additively, so
    // every channel is capped low: it should be a dim backdrop that never competes with the stars or the Sun.
    sl::Bitmap *make_sky_texture(int width)
    {
        const int height = width / 2;
        sl::Bitmap *bitmap = sl::create_bitmap(width, height);
        if (!bitmap)
            return nullptr;
        using fx::detail::sphere_fbm;
        const sl::dvec3 n = glm::normalize(sl::dvec3(0.35, 0.8, 0.5));
        const sl::dvec3 bu = glm::normalize(glm::cross(n, sl::dvec3(0, 0, 1))), bv = glm::cross(n, bu);
        for (int y = 0; y < height; ++y)
        {
            const double lat = 1.5707963 - (y + 0.5) / height * 3.1415927;
            for (int x = 0; x < width; ++x)
            {
                const double lon = (x + 0.5) / width * 6.2831853;
                const sl::dvec3 d(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon));
                const double b = glm::dot(d, n);
                const double a = std::atan2(glm::dot(d, bv), glm::dot(d, bu));
                const double core = 0.5 + 0.5 * std::cos(a - 1.0);
                const double band = std::exp(-(b * b) / (0.16 * 0.16)) * (0.3 + 0.7 * core) + 0.25 * std::exp(-(b * b) / (0.45 * 0.45));
                const double clumps = sphere_fbm(d, 6.0, 5, 101);
                const double lanes = fx::smoothstep(0.55, 0.8, sphere_fbm(d, 9.0, 4, 102));
                const double lum = band * (0.35 + 0.9 * clumps) * (1.0 - 0.35 * lanes);
                const glm::vec3 tone = glm::mix(glm::vec3(0.55f, 0.62f, 0.9f), glm::vec3(0.95f, 0.78f, 0.6f), static_cast<float>(core));
                glm::vec3 colour = tone * static_cast<float>(lum * 0.16);
                colour = glm::min(colour, glm::vec3(0.22f));
                sl::putpixel(bitmap, x, y, {static_cast<std::uint8_t>(colour.r * 255.0f), static_cast<std::uint8_t>(colour.g * 255.0f), static_cast<std::uint8_t>(colour.b * 255.0f), 255});
            }
        }
        bitmap->smooth = true;
        return bitmap;
    }

    // A camera-facing disc for an atmosphere limb: rings by closest-approach altitude, rebuilt each frame with update_limb_mesh().
    constexpr int limb_rings = 64, limb_segments = 160;
    sl::Mesh *make_limb_mesh()
    {
        std::vector<sl::Vertex3D> vertices((limb_rings + 1) * (limb_segments + 1));
        std::vector<std::uint32_t> indices;
        for (int i = 0; i < limb_rings; ++i)
            for (int j = 0; j < limb_segments; ++j)
            {
                const std::uint32_t a = i * (limb_segments + 1) + j, b = a + limb_segments + 1;
                for (const std::uint32_t k : {a, b, a + 1, a + 1, b, b + 1})
                    indices.push_back(k);
            }
        return sl::create_mesh(vertices, indices);
    }

    // The cloud deck over the flown ship: rings and segments of a spherical layer centred under the eye, fine near it. Cover comes
    // from world-fixed noise (so clouds stay put as you fly and drift with the wind). `centre` is the planet centre relative to the eye.
    constexpr int deck_rings = 56, deck_segments = 96;
    sl::Mesh *make_cloud_deck_mesh()
    {
        std::vector<sl::Vertex3D> vertices((deck_rings + 1) * (deck_segments + 1));
        std::vector<std::uint32_t> indices;
        for (int i = 0; i < deck_rings; ++i)
            for (int j = 0; j < deck_segments; ++j)
            {
                const std::uint32_t a = i * (deck_segments + 1) + j, b = a + deck_segments + 1;
                for (const std::uint32_t k : {a, b, a + 1, a + 1, b, b + 1})
                    indices.push_back(k);
            }
        return sl::create_mesh(vertices, indices);
    }

    void update_cloud_deck(sl::Mesh *mesh, const sl::dvec3 &centre, const sl::dvec3 &up, const glm::dquat &orientation, double radius, double layer_altitude,
                           double eye_altitude, double cover, double drift_east, double sun_up, bool raining)
    {
        const double layer_radius = radius + layer_altitude;
        const sl::dvec3 e1 = glm::normalize(glm::cross(std::fabs(up.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0), up));
        const sl::dvec3 e2 = glm::cross(up, e1);
        const glm::dquat inverse = glm::inverse(orientation);
        const double reach = 60.0e3; // metres along the layer
        const double lo = 0.62 - 0.5 * cover, hi = lo + 0.12;
        const double daylight = fx::smoothstep(-0.12, 0.3, sun_up);
        const double below = eye_altitude < layer_altitude ? 1.0 : 0.0;
        const double brightness = (0.12 + 0.88 * daylight) * (raining ? 0.62 : 0.92) * (below > 0.0 ? 0.82 : 1.0);
        // The sheet fades away as the eye passes through it, and thins out toward the top of the atmosphere.
        const double through = fx::smoothstep(0.0, 120.0, std::fabs(eye_altitude - layer_altitude));
        const double high = 1.0 - fx::smoothstep(60.0e3, 100.0e3, eye_altitude);
        const double shift = drift_east / layer_radius;
        std::vector<sl::Vertex3D> vertices;
        vertices.reserve((deck_rings + 1) * (deck_segments + 1));
        for (int i = 0; i <= deck_rings; ++i)
        {
            const double t = static_cast<double>(i) / deck_rings;
            const double theta = reach * t * t / layer_radius;
            const double edge = 1.0 - fx::smoothstep(0.55, 1.0, t);
            for (int j = 0; j <= deck_segments; ++j)
            {
                const double phi = 2.0 * pi * j / deck_segments;
                const sl::dvec3 dir = up * std::cos(theta) + (e1 * std::cos(phi) + e2 * std::sin(phi)) * std::sin(theta);
                const sl::dvec3 pos = centre + dir * layer_radius;
                sl::dvec3 b = inverse * dir;
                // Drifting with the wind is a turn about the spin axis.
                const double c = std::cos(shift), s = std::sin(shift);
                b = {b.x * c - b.z * s, b.y, b.x * s + b.z * c};
                const double density = fx::smoothstep(lo, hi, fx::detail::sphere_fbm(b, 2200.0, 5, 9001));
                sl::Vertex3D v;
                for (int k = 0; k < 3; ++k)
                {
                    v.position[k] = static_cast<float>(pos[k]);
                    v.normal[k] = static_cast<float>(up[k]);
                    v.colour[k] = static_cast<float>(brightness);
                }
                v.colour[3] = static_cast<float>(density * edge * through * high * 0.92);
                vertices.push_back(v);
            }
        }
        sl::update_mesh_vertices(mesh, vertices);
    }

    // Rain: a box of streaks around the eye. Each drop moves with the velocity of the air-borne water relative to the eye (fall plus wind
    // minus the ship's own motion), so it streaks past at the right angle and speed, with depth: near drops are big and fast.
    constexpr int rain_capacity = 4000;
    sl::Mesh *make_rain_mesh()
    {
        std::vector<sl::Vertex3D> vertices(rain_capacity * 4);
        std::vector<std::uint32_t> indices;
        for (int i = 0; i < rain_capacity; ++i)
            for (const std::uint32_t k : {0u, 1u, 2u, 2u, 1u, 3u})
                indices.push_back(static_cast<std::uint32_t>(i * 4) + k);
        return sl::create_mesh(vertices, indices);
    }

    void update_rain_mesh(sl::Mesh *mesh, const sl::dvec3 &up, const sl::dvec3 &relative_velocity, sl::dvec3 &offset, double dt, double intensity, double daylight)
    {
        const sl::dvec3 e1 = glm::normalize(glm::cross(std::fabs(up.y) < 0.9 ? sl::dvec3(0, 1, 0) : sl::dvec3(1, 0, 0), up));
        const sl::dvec3 e2 = glm::cross(up, e1);
        // Express the motion in the (e1, e2, up) frame so the drop field keeps a fixed orientation about the local vertical.
        const sl::dvec3 v(glm::dot(relative_velocity, e1), glm::dot(relative_velocity, e2), glm::dot(relative_velocity, up));
        constexpr double half = 15.0;
        offset += v * dt;
        for (int k = 0; k < 3; ++k)
            offset[k] = std::fmod(offset[k], 2.0 * half);
        const int count = static_cast<int>(rain_capacity * std::clamp(intensity, 0.0, 1.0));
        const auto hash = [](int i, double salt) { const double x = std::sin(i * 12.9898 + salt * 78.233) * 43758.5453; return x - std::floor(x); };
        const double speed = glm::length(relative_velocity);
        const double streak = std::clamp(speed * 0.03, 0.25, 5.0);
        const sl::dvec3 trail = speed > 1.0e-3 ? -relative_velocity / speed * streak : sl::dvec3(0.0, 0.0, 0.0);
        const float shade = static_cast<float>(0.35 + 0.55 * daylight);
        std::vector<sl::Vertex3D> vertices(rain_capacity * 4);
        for (int i = 0; i < rain_capacity; ++i)
        {
            sl::Vertex3D *quad = &vertices[static_cast<std::size_t>(i) * 4];
            if (i >= count)
                continue;
            sl::dvec3 q;
            for (int k = 0; k < 3; ++k)
            {
                double c = hash(i, k + 1.0) * 2.0 * half + offset[k];
                c = std::fmod(c + 4.0 * half, 2.0 * half) - half;
                q[k] = c;
            }
            const sl::dvec3 head = e1 * q.x + e2 * q.y + up * q.z;
            const double distance = glm::length(head);
            if (distance < 0.6)
                continue;
            const sl::dvec3 tail = head + trail * (0.6 + 0.8 * hash(i, 5.0));
            const sl::dvec3 side = glm::normalize(glm::cross(tail - head, head)) * std::max(0.006, distance * 0.0016);
            const float fade = static_cast<float>(std::clamp(1.0 - distance / (half * 1.2), 0.0, 1.0) * std::clamp((distance - 0.6) / 2.0, 0.0, 1.0));
            const sl::dvec3 corners[4] = {head - side, head + side, tail - side, tail + side};
            for (int c = 0; c < 4; ++c)
            {
                for (int k = 0; k < 3; ++k)
                {
                    quad[c].position[k] = static_cast<float>(corners[c][k]);
                    quad[c].normal[k] = static_cast<float>(up[k]);
                }
                quad[c].colour[0] = 0.78f * shade;
                quad[c].colour[1] = 0.84f * shade;
                quad[c].colour[2] = 0.95f * shade;
                // The tail end fades out so a streak reads as motion rather than a rod.
                quad[c].colour[3] = fade * (c < 2 ? 0.8f : 0.1f);
            }
        }
        sl::update_mesh_vertices(mesh, vertices);
    }

    // Fills the limb mesh. rel is the planet centre relative to the eye. Each ring is a ray grazing the planet at a given
    // altitude; the glow falls off exponentially with that altitude, and is strongest on the sunlit side and reddened near
    // the terminator.
    void update_limb_mesh(sl::Mesh *mesh, const glm::vec3 &rel, double radius, double thickness, double scale_height, const glm::vec3 &to_sun, const glm::vec3 &colour)
    {
        const double d = glm::length(glm::dvec3(rel));
        const glm::vec3 view = rel / static_cast<float>(d);
        const glm::vec3 e1 = glm::normalize(glm::cross(std::fabs(view.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), view));
        const glm::vec3 e2 = glm::cross(view, e1);
        const double height_scale = std::max(scale_height * 2.5, thickness * 0.12);
        std::vector<sl::Vertex3D> vertices;
        vertices.reserve((limb_rings + 1) * (limb_segments + 1));
        for (int i = 0; i <= limb_rings; ++i)
        {
            const double t = static_cast<double>(i) / limb_rings;
            const double h = thickness * t * t;
            // Start a little below the surface so the glow fills the gaps along the planet's tessellated edge.
            const double p = std::min(radius * 0.996 + h, d * 0.999);
            const double r = p * d / std::sqrt(std::max(d * d - p * p, 1.0));
            const double density = std::exp(-h / height_scale) * (1.0 - fx::smoothstep(0.55, 1.0, t));
            for (int j = 0; j <= limb_segments; ++j)
            {
                const double phi = 2.0 * pi * j / limb_segments;
                const glm::vec3 u = static_cast<float>(std::cos(phi)) * e1 + static_cast<float>(std::sin(phi)) * e2;
                const double sun = glm::dot(u, to_sun);
                const double lit = fx::smoothstep(-0.25, 0.55, sun);
                const double dusk = std::exp(-(sun - 0.05) * (sun - 0.05) / 0.03);
                const glm::vec3 warm(1.0f, 0.55f, 0.25f);
                const glm::vec3 c = glm::mix(colour, warm, static_cast<float>(0.6 * dusk * (1.0 - t)));
                sl::Vertex3D v;
                const glm::vec3 pos = rel + static_cast<float>(r) * u;
                for (int k = 0; k < 3; ++k)
                {
                    v.position[k] = pos[k];
                    v.normal[k] = view[k];
                }
                for (int k = 0; k < 3; ++k)
                    v.colour[k] = c[k];
                v.colour[3] = static_cast<float>(0.5 * density * lit);
                vertices.push_back(v);
            }
        }
        sl::update_mesh_vertices(mesh, vertices);
    }

    // A flat annulus in the XZ plane with outer radius 1: banded opacity, a gap and soft edges.
    sl::Mesh *make_ring_mesh(double inner_ratio, const float colour[3])
    {
        const int bands = 96, segments = 128;
        auto hash = [](double x) { return 0.5 + 0.5 * std::sin(x * 91.7 + std::sin(x * 37.1) * 3.0); };
        std::vector<sl::Vertex3D> vertices;
        std::vector<std::uint32_t> indices;
        for (int i = 0; i <= bands; ++i)
        {
            const double t = static_cast<double>(i) / bands, radius = inner_ratio + (1.0 - inner_ratio) * t;
            double alpha = 0.35 + 0.55 * hash(t * 7.0) * hash(t * 23.0 + 1.0);
            alpha *= fx::smoothstep(0.0, 0.06, t) * (1.0 - fx::smoothstep(0.94, 1.0, t));
            alpha *= 1.0 - 0.92 * (fx::smoothstep(0.60, 0.62, t) - fx::smoothstep(0.66, 0.68, t));
            const float shade = static_cast<float>(0.75 + 0.25 * hash(t * 41.0));
            for (int j = 0; j <= segments; ++j)
            {
                const double a = 2.0 * pi * j / segments;
                sl::Vertex3D v;
                v.position[0] = static_cast<float>(radius * std::cos(a));
                v.position[1] = 0.0f;
                v.position[2] = static_cast<float>(radius * std::sin(a));
                for (int c = 0; c < 3; ++c)
                    v.colour[c] = colour[c] * shade;
                v.colour[3] = static_cast<float>(alpha);
                v.uv[0] = static_cast<float>(t);
                v.uv[1] = static_cast<float>(j) / segments;
                vertices.push_back(v);
            }
        }
        for (int i = 0; i < bands; ++i)
            for (int j = 0; j < segments; ++j)
            {
                const std::uint32_t a = i * (segments + 1) + j, b = a + segments + 1;
                for (const std::uint32_t k : {a, b, a + 1, a + 1, b, b + 1})
                    indices.push_back(k);
            }
        return sl::create_mesh(vertices, indices);
    }

    // A tapering cone along +Z, opaque at the base and fading to nothing at the tip, for a comet's tail.
    sl::Mesh *make_tail_mesh()
    {
        const int segments = 16;
        std::vector<sl::Vertex3D> vertices;
        std::vector<std::uint32_t> indices;
        for (int ring = 0; ring <= 8; ++ring)
        {
            const double t = ring / 8.0;
            for (int j = 0; j < segments; ++j)
            {
                const double a = 2.0 * pi * j / segments, r = 0.01 + 0.09 * t;
                sl::Vertex3D v;
                v.position[0] = static_cast<float>(r * std::cos(a));
                v.position[1] = static_cast<float>(r * std::sin(a));
                v.position[2] = static_cast<float>(t);
                v.colour[0] = static_cast<float>(0.7 + 0.3 * t);
                v.colour[1] = 0.85f;
                v.colour[2] = static_cast<float>(1.0 - 0.3 * t);
                v.colour[3] = static_cast<float>((1.0 - t) * (1.0 - t));
                vertices.push_back(v);
            }
        }
        for (int ring = 0; ring < 8; ++ring)
            for (int j = 0; j < segments; ++j)
            {
                const std::uint32_t a = ring * segments + j, b = ring * segments + (j + 1) % segments;
                for (const std::uint32_t k : {a, a + segments, b, b, a + segments, b + segments})
                    indices.push_back(k);
            }
        return sl::create_mesh(vertices, indices);
    }
} // namespace kosmos
