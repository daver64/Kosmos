#include "planet_gen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <thread>
#include <vector>

#include "FastNoiseLite.h"
#include "draw.h"

namespace kosmos
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
        float mixf(float a, float b, float t) { return a + (b - a) * t; }
        float smooth(float lo, float hi, float v)
        {
            const float t = clamp01((v - lo) / (hi - lo));
            return t * t * (3.0f - 2.0f * t);
        }

        void mix_rgb(const float *a, const float *b, float t, float *out)
        {
            for (int i = 0; i < 3; ++i)
                out[i] = mixf(a[i], b[i], t);
        }

        void scaled(const float *c, float k, float *out)
        {
            for (int i = 0; i < 3; ++i)
                out[i] = clamp01(c[i] * k);
        }

        // Approximate inverse of the normal distribution, via the logistic curve: enough to choose a sea level.
        float probit(float p)
        {
            p = std::min(0.995f, std::max(0.005f, p));
            return std::log(p / (1.0f - p)) / 1.702f;
        }

        struct Storm
        {
            float lat, lon, width, height;
        };
    }

    Archetype archetype_from_name(const std::string &name)
    {
        if (name == "mercury")
            return Archetype::mercury;
        if (name == "venus")
            return Archetype::venus;
        if (name == "earth")
            return Archetype::earth;
        if (name == "mars")
            return Archetype::mars;
        if (name == "gas")
            return Archetype::gas;
        return Archetype::none;
    }

    const char *archetype_name(Archetype type)
    {
        switch (type)
        {
        case Archetype::mercury:
            return "mercury";
        case Archetype::venus:
            return "venus";
        case Archetype::earth:
            return "earth";
        case Archetype::mars:
            return "mars";
        case Archetype::gas:
            return "gas";
        default:
            return "none";
        }
    }

    std::string PlanetStyle::key() const
    {
        char text[160];
        std::snprintf(text, sizeof(text), "%s-%d-o%.3f-i%.3f-r%.3f-t%.3f_%.3f_%.3f-b%d-c%.3f", archetype_name(type), seed, ocean, ice, relief,
                      tint[0], tint[1], tint[2], bands, contrast);
        return text;
    }

    sl::dvec3 texture_direction(double u, double v)
    {
        const double latitude = (0.5 - v) * pi, longitude = u * 2.0 * pi;
        return {std::cos(latitude) * std::cos(longitude), std::sin(latitude), std::cos(latitude) * std::sin(longitude)};
    }

    struct PlanetSurface::Impl
    {
        double radius = 1.0;
        PlanetStyle style;
        // Continents or large-scale relief, a second large-scale field (volcanoes, highlands), moisture and albedo.
        FastNoiseLite continents, highlands, moisture, albedo;
        // Small relief from kilometres down to tens of metres, ridged mountains, and canyon lines.
        FastNoiseLite hills, ridges, canyons;
        // Crater fields from large basins to small bowls.
        FastNoiseLite craters[4];
        float crater_wavelength_km[4] = {500.0f, 120.0f, 30.0f, 7.0f};
        float crater_depth_m[4] = {3500.0f, 2200.0f, 1000.0f, 350.0f};
        int crater_scales = 0;
        float crater_strength = 1.0f;
        // Gas giants: band warp, streaks and storms.
        FastNoiseLite warp, streak;
        std::vector<Storm> storms;
        float sea = 0.0f;
        double lowest = 0.0, highest = 0.0;

        Impl(const PlanetStyle &s, double r) : radius(r), style(s)
        {
            const int seed = s.seed;
            auto per_metre = [&](double wavelength_m) { return static_cast<float>(radius / wavelength_m); };
            auto fbm = [&](FastNoiseLite &n, int offset, float frequency, int octaves, float gain)
            {
                n.SetSeed(seed * 31 + offset);
                n.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
                n.SetFractalType(FastNoiseLite::FractalType_FBm);
                n.SetFractalOctaves(octaves);
                n.SetFractalGain(gain);
                n.SetFrequency(frequency);
            };
            fbm(continents, 1, 1.15f, 5, 0.5f);
            fbm(highlands, 2, 1.8f, 3, 0.5f);
            fbm(moisture, 3, 2.4f, 4, 0.5f);
            fbm(albedo, 4, per_metre(2000e3), 6, 0.55f);
            fbm(hills, 5, per_metre(300e3), 14, 0.55f);
            fbm(ridges, 6, per_metre(160e3), 8, 0.5f);
            ridges.SetFractalType(FastNoiseLite::FractalType_Ridged);
            fbm(canyons, 7, 3.0f, 2, 0.5f);
            fbm(warp, 8, 3.0f, 4, 0.5f);
            fbm(streak, 9, 2.0f, 5, 0.55f);
            for (int i = 0; i < 4; ++i)
            {
                craters[i].SetSeed(seed * 31 + 20 + i);
                craters[i].SetNoiseType(FastNoiseLite::NoiseType_Cellular);
                craters[i].SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
                craters[i].SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
                craters[i].SetCellularJitter(1.0f);
                craters[i].SetFrequency(per_metre(crater_wavelength_km[i] * 1000.0));
            }

            const double relief = s.relief;
            switch (s.type)
            {
            case Archetype::mercury:
                crater_scales = 4;
                crater_strength = s.relief;
                lowest = -(1500.0 + 7050.0) * relief - 500.0;
                highest = (1500.0 + 0.25 * 7050.0) * relief + 500.0;
                break;
            case Archetype::venus:
                lowest = -1800.0 * relief - 400.0;
                highest = (1800.0 + 2500.0) * relief + 400.0;
                break;
            case Archetype::earth:
                sea = 0.35f * probit(s.ocean);
                lowest = 0.0;
                highest = 1500.0 + 5500.0 * relief + 500.0;
                break;
            case Archetype::mars:
                crater_scales = 3;
                crater_strength = 0.5f * s.relief;
                lowest = -(2500.0 + 1200.0 + 3500.0 + 3350.0) * relief;
                highest = (2500.0 + 1200.0 + 9000.0 + 840.0) * relief;
                break;
            default:
                break;
            }

            std::mt19937 rng(static_cast<unsigned>(seed) * 2654435761u + 7u);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            const int count = 1 + static_cast<int>(unit(rng) * 3.0f);
            for (int i = 0; i < count; ++i)
                storms.push_back({(unit(rng) - 0.5f) * 1.4f, unit(rng) * 6.2831853f, 0.2f + 0.4f * unit(rng), 0.08f + 0.12f * unit(rng)});
        }

        // Craters from the larger scales to the small: a bowl with a raised rim, in metres.
        double crater_height(const sl::dvec3 &d) const
        {
            if (crater_scales == 0)
                return 0.0;
            const float x = static_cast<float>(d.x), y = static_cast<float>(d.y), z = static_cast<float>(d.z);
            double sum = 0.0;
            for (int i = 0; i < crater_scales; ++i)
            {
                const float distance = craters[i].GetNoise(x, y, z) + 1.0f;
                // A slowly varying field sets each cell's crater size, from nothing to a bowl filling the cell.
                const float spread = 2.3f + 1.7f * static_cast<float>(i);
                const float size_mask = 0.5f + 0.5f * highlands.GetNoise(x * spread, y * spread, z * spread);
                const float radius_cells = 0.03f + 0.36f * size_mask * size_mask;
                float profile = 0.0f;
                if (distance < radius_cells)
                {
                    const float t = distance / radius_cells;
                    profile = -(1.0f - t * t);
                }
                const float rim = (distance - radius_cells) / (0.22f * radius_cells);
                profile += 0.25f * std::exp(-rim * rim);
                sum += profile * crater_depth_m[i] * crater_strength;
            }
            return sum;
        }

        double height(const sl::dvec3 &d) const
        {
            const float x = static_cast<float>(d.x), y = static_cast<float>(d.y), z = static_cast<float>(d.z);
            const double relief = style.relief;
            switch (style.type)
            {
            case Archetype::mercury:
                return continents.GetNoise(x, y, z) * 1500.0 * relief + hills.GetNoise(x, y, z) * 500.0 + crater_height(d);
            case Archetype::venus:
            {
                const float mask = smooth(0.2f, 0.55f, highlands.GetNoise(x, y, z));
                const float ridge = clamp01((ridges.GetNoise(x, y, z) + 0.2f) / 1.2f);
                return continents.GetNoise(x, y, z) * 1800.0 * relief + hills.GetNoise(x, y, z) * 400.0 + mask * ridge * 2500.0 * relief;
            }
            case Archetype::earth:
            {
                const float land = continents.GetNoise(x, y, z) - sea;
                if (land <= 0.0f)
                    return 0.0;
                const float m = clamp01(land / 0.6f);
                const double plains = m * 1500.0;
                const float mask = smooth(0.25f, 0.7f, land);
                const float ridge = clamp01((ridges.GetNoise(x, y, z) + 0.2f) / 1.2f);
                return plains + mask * ridge * ridge * 5500.0 * relief + hills.GetNoise(x, y, z) * 500.0 * m;
            }
            case Archetype::mars:
            {
                double h = continents.GetNoise(x, y, z) * 2500.0 * relief + hills.GetNoise(x, y, z) * 1200.0 * relief;
                const float valley = 1.0f - std::fabs(canyons.GetNoise(x, y, z));
                const float equatorial = 1.0f - smooth(0.4f, 0.7f, static_cast<float>(std::fabs(d.y)));
                h -= 3500.0 * relief * smooth(0.975f, 0.997f, valley) * equatorial * smooth(0.0f, 0.4f, highlands.GetNoise(x * 1.7f, y * 1.7f, z * 1.7f));
                h += 9000.0 * relief * smooth(0.45f, 0.7f, highlands.GetNoise(x, y, z));
                return h + crater_height(d);
            }
            default:
                return 0.0;
            }
        }

        void gas_colour(const sl::dvec3 &d, float *rgb) const
        {
            const float x = static_cast<float>(d.x), y = static_cast<float>(d.y), z = static_cast<float>(d.z);
            const float latitude = std::asin(std::max(-1.0f, std::min(1.0f, y)));
            const float turbulence = warp.GetNoise(x, y, z);
            const float band = 0.5f + 0.5f * std::sin(latitude * static_cast<float>(style.bands) * 2.0f + turbulence * 2.2f);
            const float fine = 0.5f + 0.5f * streak.GetNoise(x, y * 6.0f, z);
            const float v = band * 0.65f + fine * 0.35f;
            float dark[3], light[3];
            scaled(style.tint, 1.0f - 0.55f * style.contrast, dark);
            scaled(style.tint, 1.0f + 0.45f * style.contrast, light);
            for (int i = 0; i < 3; ++i)
                light[i] = clamp01(light[i] + 0.06f * style.contrast);
            mix_rgb(dark, light, v, rgb);
            // Storms: oval vortices, warmer and paler than the bands around them.
            const float longitude = std::atan2(z, x);
            for (const Storm &storm : storms)
            {
                float dl = longitude - storm.lon;
                while (dl > 3.14159265f)
                    dl -= 6.2831853f;
                while (dl < -3.14159265f)
                    dl += 6.2831853f;
                const float across = dl * std::cos(latitude) / storm.width, up = (latitude - storm.lat) / storm.height;
                const float e = across * across + up * up;
                if (e < 1.0f)
                {
                    const float swirl = 0.5f + 0.5f * warp.GetNoise(x * 3.0f, y * 3.0f, z * 3.0f);
                    const float strength = (1.0f - e) * (0.55f + 0.3f * swirl) * (0.4f + 0.6f * style.contrast);
                    const float spot[3] = {clamp01(style.tint[0] * 1.15f + 0.08f), clamp01(style.tint[1] * 0.78f), clamp01(style.tint[2] * 0.7f)};
                    float out[3];
                    mix_rgb(rgb, spot, strength, out);
                    std::copy(out, out + 3, rgb);
                }
            }
        }

        void earth_colour(const sl::dvec3 &d, double h, double slope, float *rgb) const
        {
            const float x = static_cast<float>(d.x), y = static_cast<float>(d.y), z = static_cast<float>(d.z);
            const float lat = std::fabs(y);
            const float jitter = 0.05f * albedo.GetNoise(x, y, z);
            const float cold = lat + static_cast<float>(h) / 12000.0f + jitter;
            const float ice_line = 1.05f - 0.7f * style.ice;
            const float snow[3] = {0.93f, 0.95f, 0.98f};
            float out[3];
            if (h <= 0.0)
            {
                const float depth = clamp01((sea - continents.GetNoise(x, y, z)) * 1.6f);
                const float shallow[3] = {0.10f, 0.38f, 0.48f}, deep[3] = {0.02f, 0.08f, 0.24f};
                mix_rgb(shallow, deep, smooth(0.0f, 1.0f, depth), rgb);
                mix_rgb(rgb, snow, smooth(ice_line - 0.04f, ice_line + 0.03f, cold), out);
                std::copy(out, out + 3, rgb);
                return;
            }
            const float temperature = 1.0f - std::pow(lat, 1.3f) - static_cast<float>(h) / 10000.0f;
            const float wet = clamp01(0.5f + 0.7f * moisture.GetNoise(x, y, z) - 0.25f * smooth(0.0f, 0.5f, 1.0f - std::fabs(lat - 0.3f) * 3.0f));
            const float sand[3] = {0.80f, 0.68f, 0.45f}, grass[3] = {0.45f, 0.58f, 0.25f}, forest[3] = {0.12f, 0.33f, 0.14f};
            const float tundra[3] = {0.52f, 0.50f, 0.40f}, rock[3] = {0.45f, 0.42f, 0.40f}, savanna[3] = {0.66f, 0.60f, 0.32f};
            float ground[3], tmp[3];
            mix_rgb(sand, savanna, smooth(0.15f, 0.4f, wet), ground);
            mix_rgb(ground, grass, smooth(0.4f, 0.6f, wet), tmp);
            mix_rgb(tmp, forest, smooth(0.62f, 0.85f, wet), ground);
            mix_rgb(ground, tundra, smooth(0.45f, 0.25f, temperature), tmp);
            mix_rgb(tmp, rock, smooth(1800.0f, 4500.0f, static_cast<float>(h)), ground);
            mix_rgb(ground, rock, smooth(28.0f, 50.0f, static_cast<float>(slope)), tmp);
            const float shade = 1.0f + 0.12f * albedo.GetNoise(x * 8.0f, y * 8.0f, z * 8.0f);
            scaled(tmp, shade, rgb);
            const float snowline = ice_line + 0.02f;
            const float steep = 1.0f - smooth(0.0f, 0.6f, static_cast<float>(slope) / 60.0f);
            const float snowed = std::max(smooth(snowline - 0.05f, snowline + 0.03f, cold), smooth(5200.0f, 6600.0f, static_cast<float>(h)) * steep);
            mix_rgb(rgb, snow, snowed, out);
            std::copy(out, out + 3, rgb);
        }

        void rocky_colour(const sl::dvec3 &d, double h, double slope, float *rgb) const
        {
            const float x = static_cast<float>(d.x), y = static_cast<float>(d.y), z = static_cast<float>(d.z);
            const float tone = albedo.GetNoise(x, y, z);
            const float height_norm = static_cast<float>(h / std::max(1.0, highest));
            float tmp[3];
            switch (style.type)
            {
            case Archetype::mercury:
            {
                const float v = 0.85f + 0.18f * tone + 0.12f * clamp01(height_norm + 0.2f);
                scaled(style.tint, v, rgb);
                if (style.ice > 0.0f)
                {
                    const float ice[3] = {0.88f, 0.90f, 0.93f};
                    mix_rgb(rgb, ice, style.ice * smooth(-0.2f, 0.4f, tone + 0.2f), tmp);
                    std::copy(tmp, tmp + 3, rgb);
                }
                break;
            }
            case Archetype::venus:
                scaled(style.tint, 0.55f * (1.0f + 0.25f * tone + 0.25f * clamp01(height_norm + 0.3f)), rgb);
                break;
            default: // mars
            {
                float dust[3], basalt[3], bright[3];
                scaled(style.tint, 1.0f, dust);
                scaled(style.tint, 0.38f, basalt);
                scaled(style.tint, 1.25f, bright);
                for (int i = 0; i < 3; ++i)
                    bright[i] = clamp01(bright[i] + 0.05f);
                mix_rgb(dust, basalt, smooth(0.2f, 0.7f, tone + 0.15f * std::fabs(height_norm)) * 0.8f, tmp);
                mix_rgb(tmp, bright, smooth(0.35f, 0.7f, albedo.GetNoise(x * 3.1f, y * 3.1f, z * 3.1f)) * 0.7f, rgb);
                mix_rgb(rgb, basalt, smooth(25.0f, 45.0f, static_cast<float>(slope)) * 0.7f, tmp);
                std::copy(tmp, tmp + 3, rgb);
                const float cap = std::sin((84.0f - 45.0f * style.ice + 4.0f * tone) * 0.01745329f);
                const float white[3] = {0.93f, 0.92f, 0.95f};
                mix_rgb(rgb, white, smooth(cap - 0.02f, cap + 0.02f, std::fabs(y)), tmp);
                std::copy(tmp, tmp + 3, rgb);
                break;
            }
            }
        }
    };

    PlanetSurface::PlanetSurface(const PlanetStyle &style, double radius) : style_(style), impl_(std::make_shared<Impl>(style, radius)) {}
    PlanetSurface::~PlanetSurface() = default;

    double PlanetSurface::height(const sl::dvec3 &direction) const { return impl_->height(direction); }
    double PlanetSurface::min_height() const { return impl_->lowest; }
    double PlanetSurface::max_height() const { return impl_->highest; }

    void PlanetSurface::colour(const sl::dvec3 &d, double h, double slope, float rgb[3]) const
    {
        switch (style_.type)
        {
        case Archetype::gas:
            impl_->gas_colour(d, rgb);
            break;
        case Archetype::earth:
            impl_->earth_colour(d, h, slope, rgb);
            break;
        default:
            impl_->rocky_colour(d, h, slope, rgb);
            break;
        }
    }

    namespace
    {
        // The pale cloud tops of a hazy world: a gentle band pattern in the same tint.
        std::shared_ptr<PlanetSurface> haze_layer(const PlanetStyle &style, double radius)
        {
            PlanetStyle haze = style;
            haze.type = Archetype::gas;
            haze.bands = 5;
            haze.contrast = 0.16f;
            return std::make_shared<PlanetSurface>(haze, radius);
        }
    }

    void PlanetSurface::far_colour(const sl::dvec3 &d, double h, float rgb[3]) const
    {
        if (style_.type == Archetype::venus)
            haze_layer(style_, impl_->radius)->colour(d, h, 0.0, rgb);
        else
            colour(d, h, 0.0, rgb);
    }

    sl::Bitmap *PlanetSurface::make_texture(int width, int height) const
    {
        return make_region(0.0, 0.0, 1.0, 1.0, width, height);
    }

    sl::Bitmap *PlanetSurface::make_region(double u0, double v0, double u1, double v1, int width, int height, unsigned max_threads) const
    {
        sl::Bitmap *bitmap = sl::create_bitmap(width, height);
        if (!bitmap)
            return nullptr;
        // A hazy world shows its cloud tops, not the ground; build that layer once rather than per pixel.
        std::shared_ptr<PlanetSurface> haze;
        if (style_.type == Archetype::venus)
            haze = haze_layer(style_, impl_->radius);
        const unsigned threads = std::max(1u, std::min(std::max(1u, max_threads), std::thread::hardware_concurrency()));
        auto run = [&](int rows, const std::function<void(int)> &row)
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
        };
        const bool ground = style_.has_ground() && !haze;
        auto direction_at = [&](double x, double y)
        {
            return texture_direction(u0 + (x + 0.5) / width * (u1 - u0), std::clamp(v0 + (y + 0.5) / height * (v1 - v0), 0.0, 1.0));
        };
        // Heights carry a one pixel border so the curvature shading is continuous across neighbouring tiles.
        const int stride = width + 2;
        std::vector<float> heights;
        if (ground)
        {
            heights.resize(static_cast<std::size_t>(stride) * (height + 2));
            run(height + 2, [&](int y)
            {
                for (int x = 0; x < stride; ++x)
                    heights[static_cast<std::size_t>(y) * stride + x] = static_cast<float>(impl_->height(direction_at(x - 1, y - 1)));
            });
        }
        const float range = static_cast<float>(std::max(1.0, impl_->highest - impl_->lowest));
        // Shading from how the ground bends away from the average of its neighbours: bowls and valleys darken, rims and ridges lighten.
        // It stands in for the relief the lit sphere cannot show; Earth-like oceans stay flat.
        // The curvature is per pixel, so it is scaled to keep the look the same at any resolution of the same world.
        const float relief_gain = style_.type == Archetype::earth ? 1.0f : 2.5f;
        run(height, [&](int y)
        {
            for (int x = 0; x < width; ++x)
            {
                const sl::dvec3 d = direction_at(x, y);
                float rgb[3];
                if (haze)
                    haze->colour(d, 0.0, 0.0, rgb);
                else if (!ground)
                    colour(d, 0.0, 0.0, rgb);
                else
                {
                    const std::size_t at = static_cast<std::size_t>(y + 1) * stride + x + 1;
                    const float h = heights[at];
                    colour(d, h, 0.0, rgb);
                    if (h > 0.0f || style_.type != Archetype::earth)
                    {
                        const float curvature = h - 0.25f * (heights[at - 1] + heights[at + 1] + heights[at - stride] + heights[at + stride]);
                        const float shade = 1.0f + std::max(-0.3f, std::min(0.3f, relief_gain * curvature / range * 6.0f * std::sqrt(std::max(0.0f, 1.0f - static_cast<float>(d.y * d.y)))));
                        for (float &c : rgb)
                            c = clamp01(c * shade);
                    }
                }
                std::uint8_t *pixel = &bitmap->pixels[(static_cast<std::size_t>(y) * width + x) * 4];
                for (int i = 0; i < 3; ++i)
                    pixel[i] = static_cast<std::uint8_t>(clamp01(rgb[i]) * 255.0f + 0.5f);
                pixel[3] = 255;
            }
        });
        bitmap->ram_dirty = true;
        bitmap->smooth = true;
        bitmap->mipmaps = true;
        return bitmap;
    }

    namespace
    {
        class GeneratedSource final : public sl::TerrainSource
        {
        public:
            explicit GeneratedSource(PlanetSurface surface) : surface_(std::move(surface)) {}
            double height(double, double) const override { return 0.0; }
            double height_on_sphere(const sl::dvec3 &direction, double) const override { return surface_.height(direction); }
            double min_height() const override { return surface_.min_height(); }
            double max_height() const override { return surface_.max_height(); }

        private:
            PlanetSurface surface_;
        };
    }

    std::shared_ptr<sl::TerrainSource> PlanetSurface::make_source() const { return std::make_shared<GeneratedSource>(*this); }
}
