#include "layers.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <thread>

#include "FastNoiseLite.h"
#include "draw.h"

namespace planetbake
{
    namespace
    {
        float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

        float smooth(float lo, float hi, float v)
        {
            const float t = clamp01((v - lo) / (hi - lo));
            return t * t * (3.0f - 2.0f * t);
        }

        std::uint8_t byte(float v) { return static_cast<std::uint8_t>(clamp01(v) * 255.0f + 0.5f); }

        const char *const names[layer_count] = {"colour", "height", "normal", "specular", "night", "clouds"};

        // One region is rendered at a time in the whole process, so the thread cap holds even if a preview and an export overlap.
        std::mutex render_mutex;

        void parallel_rows(int rows, unsigned threads, const std::function<void(int)> &row)
        {
            std::vector<std::thread> pool;
            for (unsigned t = 0; t < threads; ++t)
                pool.emplace_back([&, t]
                {
                    for (int y = static_cast<int>(t); y < rows; y += static_cast<int>(threads))
                        row(y);
                });
            for (std::thread &thread : pool)
                thread.join();
        }

        unsigned usable_threads(unsigned wanted)
        {
            const unsigned hardware = std::thread::hardware_concurrency();
            return std::max(1u, std::min({wanted, max_threads, hardware ? hardware : max_threads}));
        }
    }

    const char *layer_name(int layer) { return layer >= 0 && layer < layer_count ? names[layer] : ""; }

    int layer_from_name(const std::string &name)
    {
        for (int i = 0; i < layer_count; ++i)
            if (name == names[i])
                return i;
        return -1;
    }

    bool LayerSet::any() const { return std::any_of(std::begin(on), std::end(on), [](bool b) { return b; }); }

    LayerSet LayerSet::all()
    {
        LayerSet set;
        std::fill(std::begin(set.on), std::end(set.on), true);
        return set;
    }

    struct World::Night
    {
        FastNoiseLite regions, towns, speckle;
        explicit Night(int seed)
        {
            auto setup = [&](FastNoiseLite &n, int offset, float frequency, int octaves)
            {
                n.SetSeed(seed * 17 + offset);
                n.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
                n.SetFractalType(FastNoiseLite::FractalType_FBm);
                n.SetFractalOctaves(octaves);
                n.SetFrequency(frequency);
            };
            setup(regions, 901, 6.0f, 4);  // populated regions, about a thousand kilometres across on an Earth-sized world
            setup(towns, 902, 90.0f, 3);   // conurbations
            setup(speckle, 903, 1500.0f, 2); // individual towns
        }
    };

    struct World::Clouds
    {
        FastNoiseLite body, detail, warp, weather;
        explicit Clouds(int seed)
        {
            auto setup = [&](FastNoiseLite &n, int offset, float frequency, int octaves)
            {
                n.SetSeed(seed * 31 + offset);
                n.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
                n.SetFractalType(FastNoiseLite::FractalType_FBm);
                n.SetFractalOctaves(octaves);
                n.SetFrequency(frequency);
            };
            setup(body, 1101, 3.0f, 9);    // the cloud masses
            setup(detail, 1102, 40.0f, 3); // fine breakup of the edges
            setup(warp, 1103, 2.0f, 3);    // swirls the storm belts
            setup(weather, 1104, 1.2f, 2); // large scale wet and dry regions
        }
    };

    struct World::Detail
    {
        static constexpr int count = 6;
        static constexpr double wavelength_m[count] = {40e3, 10e3, 2.5e3, 600.0, 150.0, 40.0};
        static constexpr float amplitude[count] = {0.07f, 0.07f, 0.065f, 0.055f, 0.045f, 0.035f};
        FastNoiseLite noise[count], patches;
        Detail(int seed, double radius)
        {
            for (int i = 0; i < count; ++i)
            {
                noise[i].SetSeed(seed * 37 + 1201 + i);
                noise[i].SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
                noise[i].SetFractalType(FastNoiseLite::FractalType_FBm);
                noise[i].SetFractalOctaves(2);
                noise[i].SetFrequency(static_cast<float>(radius / wavelength_m[i]));
            }
            patches.SetSeed(seed * 37 + 1299);
            patches.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            patches.SetFractalType(FastNoiseLite::FractalType_FBm);
            patches.SetFractalOctaves(3);
            patches.SetFrequency(static_cast<float>(radius / 25e3));
        }
    };

    void World::add_detail(const sl::dvec3 &d, double pixel_m, float rgb[3]) const
    {
        if (!detail_noise_ || detail_ <= 0.0f)
            return;
        const float x = static_cast<float>(d.x), y = static_cast<float>(d.y), z = static_cast<float>(d.z);
        float luma = 0.0f;
        for (int i = 0; i < Detail::count; ++i)
        {
            // Only scales the pixels can resolve: full strength from 6 pixels a wavelength, none below 2.
            const float fade = smooth(static_cast<float>(pixel_m * 2.0), static_cast<float>(pixel_m * 6.0), static_cast<float>(Detail::wavelength_m[i]));
            if (fade > 0.0f)
                luma += fade * Detail::amplitude[i] * detail_noise_->noise[i].GetNoise(x, y, z) * 2.0f;
        }
        // Field and woodland style patches: a warm or cool shift of the hue, resolved at a coarser scale.
        const float fade = smooth(static_cast<float>(pixel_m * 2.0), static_cast<float>(pixel_m * 6.0), 25.0e3f);
        const float patch = fade * detail_noise_->patches.GetNoise(x, y, z) * 0.12f;
        const float k = detail_;
        rgb[0] = clamp01(rgb[0] * (1.0f + k * (luma + patch)));
        rgb[1] = clamp01(rgb[1] * (1.0f + k * (luma - 0.3f * patch)));
        rgb[2] = clamp01(rgb[2] * (1.0f + k * (0.8f * luma - patch)));
    }

    World::World(const kosmos::PlanetStyle &style, double radius_m, float normal_strength, float detail)
        : surface_(style, radius_m), radius_(radius_m), normal_strength_(normal_strength), detail_(detail)
    {
        if (style.has_ground())
            detail_noise_ = std::make_shared<Detail>(style.seed, radius_m);
        if (style.type == kosmos::Archetype::earth)
            night_ = std::make_shared<Night>(style.seed);
        if (style.type == kosmos::Archetype::earth || style.type == kosmos::Archetype::venus || style.type == kosmos::Archetype::mars)
            clouds_ = std::make_shared<Clouds>(style.seed);
    }

    float World::cloud(const sl::dvec3 &d, float tint[3]) const
    {
        tint[0] = tint[1] = tint[2] = 1.0f;
        if (!clouds_)
            return 0.0f;
        const float x = static_cast<float>(d.x), y = static_cast<float>(d.y), z = static_cast<float>(d.z);
        const float lat = std::fabs(y);
        auto fbm = [](const FastNoiseLite &n, float a, float b, float c) { return 0.5f + 0.5f * n.GetNoise(a, b, c); };
        switch (style().type)
        {
        case kosmos::Archetype::earth:
        {
            // Convergence near the equator, dry subtropics, then storm belts at mid latitudes whose cloud is drawn out along the
            // flow and swirled; polar regions are patchy.
            const float itcz = std::exp(-std::pow((lat - 0.06f) / 0.14f, 2.0f));
            const float storms = std::exp(-std::pow((lat - 0.78f) / 0.2f, 2.0f));
            const float dry = std::exp(-std::pow((lat - 0.42f) / 0.14f, 2.0f));
            const float wet = fbm(clouds_->weather, x, y, z);
            const float wx = clouds_->warp.GetNoise(x, y, z) * 0.35f, wz = clouds_->warp.GetNoise(z, x, y) * 0.35f;
            // Stretching the sample along latitude makes east-west streaks (fronts) where the storm weight is high.
            const float stretch = 1.0f + 2.5f * storms;
            const float n = fbm(clouds_->body, x * 1.0f + wx * storms * 1.2f, y * stretch, z + wz * storms * 1.2f);
            const float edge = fbm(clouds_->detail, x, y, z);
            const float cover = std::clamp(0.34f + 0.3f * itcz + 0.26f * storms - 0.2f * dry + 0.28f * (wet - 0.5f), 0.1f, 0.85f);
            const float threshold = 1.0f - cover;
            const float a = smooth(threshold - 0.04f, threshold + 0.14f, n + 0.06f * (edge - 0.5f));
            const float thick = smooth(threshold, threshold + 0.3f, n);
            const float shade = 1.0f - 0.12f * (1.0f - thick);
            tint[0] = tint[1] = tint[2] = shade;
            return clamp01(a * (0.55f + 0.45f * thick) * 0.97f);
        }
        case kosmos::Archetype::venus:
        {
            // An unbroken, streaky deck: the sample is stretched hard along latitude so the cloud forms long bands.
            const float band = fbm(clouds_->body, x * 0.4f, y * 6.0f, z * 0.4f);
            const float eddy = fbm(clouds_->warp, x * 2.0f, y * 3.0f, z * 2.0f);
            const float fine = fbm(clouds_->detail, x * 0.5f, y * 5.0f, z * 0.5f);
            const float v = 0.5f * band + 0.3f * eddy + 0.2f * fine;
            tint[0] = 0.96f + 0.04f * v;
            tint[1] = 0.88f + 0.08f * v;
            tint[2] = 0.62f + 0.14f * v;
            return clamp01(0.72f + 0.28f * smooth(0.3f, 0.7f, v));
        }
        default:
        {
            // Mars: thin water-ice cloud, a polar hood, and sparse streaks of high wisps.
            const float hood = smooth(0.62f, 0.9f, lat);
            const float wisps = fbm(clouds_->body, x * 0.7f, y * 2.2f, z * 0.7f);
            const float patches = fbm(clouds_->weather, x, y, z);
            const float n = 0.7f * wisps + 0.3f * fbm(clouds_->detail, x, y, z);
            const float cover = std::clamp(0.1f + 0.32f * hood + 0.1f * (patches - 0.5f), 0.04f, 0.5f);
            const float a = smooth(1.0f - cover - 0.05f, 1.0f - cover + 0.12f, n);
            tint[0] = 0.95f;
            tint[1] = 0.86f;
            tint[2] = 0.78f;
            return clamp01(a * 0.7f);
        }
        }
    }

    bool World::applicable(int layer) const
    {
        if (layer == layer_colour)
            return style().valid();
        if (layer == layer_clouds)
            return clouds_ != nullptr;
        if (!style().has_ground())
            return false;
        return layer != layer_night || style().type == kosmos::Archetype::earth;
    }

    float World::lights(const sl::dvec3 &d, double height, const float rgb[3]) const
    {
        if (!night_ || height <= 0.0 || height > 2200.0 || std::fabs(d.y) > 0.93)
            return 0.0f;
        const float luma = 0.3f * rgb[0] + 0.59f * rgb[1] + 0.11f * rgb[2];
        if (luma > 0.75f) // snow and ice
            return 0.0f;
        const float x = static_cast<float>(d.x), y = static_cast<float>(d.y), z = static_cast<float>(d.z);
        const float region = smooth(0.52f, 0.8f, 0.5f + 0.5f * night_->regions.GetNoise(x, y, z));
        if (region <= 0.0f)
            return 0.0f;
        const float town = smooth(0.55f, 0.85f, 0.5f + 0.5f * night_->towns.GetNoise(x, y, z));
        const float speck = smooth(0.45f, 0.75f, 0.5f + 0.5f * night_->speckle.GetNoise(x, y, z));
        const float lowland = 1.0f - smooth(800.0f, 2200.0f, static_cast<float>(height));
        return clamp01(region * (0.12f + 0.88f * town) * (0.3f + 0.7f * speck) * lowland * 1.6f);
    }

    bool render_region(const World &world, const Region &r, const LayerSet &want, Rendered &out, unsigned threads, std::string &error)
    {
        if (r.w <= 0 || r.h <= 0)
        {
            error = "empty region";
            return false;
        }
        std::lock_guard<std::mutex> lock(render_mutex);
        threads = usable_threads(threads);
        const kosmos::PlanetStyle &style = world.style();
        const kosmos::PlanetSurface &surface = world.surface();
        const int w = r.w, h = r.h;
        const std::size_t pixels = static_cast<std::size_t>(w) * h;

        LayerSet use;
        for (int i = 0; i < layer_count; ++i)
            use.on[i] = want.on[i] && world.applicable(i);
        out.w = w;
        out.h = h;
        for (int i = 0; i < layer_count; ++i)
            out.layer[i].clear();
        out.height.clear();
        for (int i = 0; i < layer_count; ++i)
            if (use.on[i] && i != layer_height)
                out.layer[i].assign(pixels * 4, 255);
        if (use.on[layer_height])
            out.height.assign(pixels, 0);

        // A hazy world shows its cloud tops, which the generator already draws; the ground layers below still work from the terrain.
        const bool own_colour = use.on[layer_colour] && style.type != kosmos::Archetype::venus && style.type != kosmos::Archetype::gas;
        if (use.on[layer_colour] && !own_colour)
        {
            sl::Bitmap *bitmap = surface.make_region(r.u0, r.v0, r.u1, r.v1, w, h, threads);
            if (!bitmap)
            {
                error = "out of memory";
                return false;
            }
            out.layer[layer_colour].assign(bitmap->pixels.begin(), bitmap->pixels.begin() + static_cast<std::ptrdiff_t>(pixels * 4));
            sl::destroy_bitmap(bitmap);
        }

        if (use.on[layer_clouds])
        {
            std::vector<std::uint8_t> &cloud = out.layer[layer_clouds];
            parallel_rows(h, threads, [&](int y)
            {
                for (int x = 0; x < w; ++x)
                {
                    float tint[3];
                    const float a = world.cloud(kosmos::texture_direction(r.u0 + (x + 0.5) / w * (r.u1 - r.u0), std::clamp(r.v0 + (y + 0.5) / h * (r.v1 - r.v0), 0.0, 1.0)), tint);
                    std::uint8_t *p = &cloud[(static_cast<std::size_t>(y) * w + x) * 4];
                    p[0] = byte(tint[0]);
                    p[1] = byte(tint[1]);
                    p[2] = byte(tint[2]);
                    p[3] = byte(a);
                }
            });
        }

        const bool ground = style.has_ground();
        const bool need_height = ground && (own_colour || use.on[layer_height] || use.on[layer_normal] || use.on[layer_specular] || use.on[layer_night]);
        if (!need_height)
            return true;

        auto direction_at = [&](double x, double y)
        {
            return kosmos::texture_direction(r.u0 + (x + 0.5) / w * (r.u1 - r.u0), std::clamp(r.v0 + (y + 0.5) / h * (r.v1 - r.v0), 0.0, 1.0));
        };
        const int stride = w + 2;
        std::vector<float> heights(static_cast<std::size_t>(stride) * (h + 2));
        parallel_rows(h + 2, threads, [&](int y)
        {
            for (int x = 0; x < stride; ++x)
                heights[static_cast<std::size_t>(y) * stride + x] = static_cast<float>(surface.height(direction_at(x - 1, y - 1)));
        });

        const double lowest = world.lowest(), highest = world.highest();
        const float range = static_cast<float>(std::max(1.0, highest - lowest));
        const bool earth = style.type == kosmos::Archetype::earth;
        // The same relief shading as the generator's own far-view texture (PlanetSurface::make_region).
        const float relief_gain = earth ? 1.0f : 2.5f;
        // Metres between neighbouring pixels along the parallel (scaled by cos latitude) and along the meridian.
        const double metres_x = 2.0 * 3.14159265358979323846 * world.radius() * (r.u1 - r.u0) / w;
        const double metres_y = 3.14159265358979323846 * world.radius() * (r.v1 - r.v0) / h;
        const bool need_rgb = own_colour || use.on[layer_specular] || use.on[layer_night];

        parallel_rows(h, threads, [&](int y)
        {
            for (int x = 0; x < w; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * w + x;
                const std::size_t at = static_cast<std::size_t>(y + 1) * stride + x + 1;
                const float hg = heights[at];
                const sl::dvec3 d = direction_at(x, y);
                float rgb[3] = {0.0f, 0.0f, 0.0f};
                if (need_rgb)
                {
                    surface.colour(d, hg, 0.0, rgb);
                    if (own_colour && (hg > 0.0f || !earth))
                        world.add_detail(d, metres_y, rgb);
                }
                const float luma = 0.3f * rgb[0] + 0.59f * rgb[1] + 0.11f * rgb[2];

                if (own_colour)
                {
                    float shaded[3] = {rgb[0], rgb[1], rgb[2]};
                    if (hg > 0.0f || !earth)
                    {
                        const float curvature = hg - 0.25f * (heights[at - 1] + heights[at + 1] + heights[at - stride] + heights[at + stride]);
                        const float shade = 1.0f + std::max(-0.3f, std::min(0.3f, relief_gain * curvature / range * 6.0f * std::sqrt(std::max(0.0f, 1.0f - static_cast<float>(d.y * d.y)))));
                        for (float &c : shaded)
                            c = clamp01(c * shade);
                    }
                    std::uint8_t *p = &out.layer[layer_colour][index * 4];
                    for (int i = 0; i < 3; ++i)
                        p[i] = byte(shaded[i]);
                }
                if (use.on[layer_height])
                    out.height[index] = static_cast<std::uint16_t>(clamp01(static_cast<float>((hg - lowest) / (highest - lowest))) * 65535.0f + 0.5f);
                if (use.on[layer_normal])
                {
                    // Earth's sea floor is not shown, as in the colour layer.
                    auto h_at = [&](std::size_t i) { return earth ? std::max(0.0f, heights[i]) : heights[i]; };
                    const double dx = std::max(1.0, metres_x * std::max(0.02, std::sqrt(std::max(0.0, 1.0 - d.y * d.y))));
                    const double dy = std::max(1.0, metres_y);
                    const double east = (h_at(at + 1) - h_at(at - 1)) / (2.0 * dx);
                    const double north = (h_at(at - stride) - h_at(at + stride)) / (2.0 * dy);
                    const double s = world.normal_strength();
                    double nx = -east * s, ny = -north * s, nz = 1.0;
                    const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
                    std::uint8_t *p = &out.layer[layer_normal][index * 4];
                    p[0] = byte(static_cast<float>(0.5 + 0.5 * nx / length));
                    p[1] = byte(static_cast<float>(0.5 + 0.5 * ny / length));
                    p[2] = byte(static_cast<float>(0.5 + 0.5 * nz / length));
                }
                if (use.on[layer_specular])
                {
                    float gloss;
                    switch (style.type)
                    {
                    case kosmos::Archetype::earth:
                        gloss = hg <= 0.0f ? (luma > 0.8f ? 0.3f : 0.9f) : (luma > 0.8f ? 0.3f : 0.07f);
                        break;
                    case kosmos::Archetype::venus:
                        gloss = 0.02f;
                        break;
                    default:
                        gloss = luma > 0.75f ? 0.25f : 0.03f;
                        break;
                    }
                    std::uint8_t *p = &out.layer[layer_specular][index * 4];
                    p[0] = p[1] = p[2] = byte(gloss);
                }
                if (use.on[layer_night])
                {
                    const float lit = world.lights(d, hg, rgb);
                    std::uint8_t *p = &out.layer[layer_night][index * 4];
                    p[0] = byte(lit);
                    p[1] = byte(lit * 0.78f);
                    p[2] = byte(lit * 0.42f);
                }
            }
        });
        return true;
    }
}
