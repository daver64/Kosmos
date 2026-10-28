#include "render.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

namespace kmap
{
    namespace
    {
        constexpr double rad = pi / 180.0;

        double wrap_lon(double lon)
        {
            lon = std::fmod(lon + 180.0, 360.0);
            if (lon < 0.0)
                lon += 360.0;
            return lon - 180.0;
        }

        struct Rgb
        {
            float r, g, b;
        };

        Rgb mix(const Rgb &a, const Rgb &b, float t)
        {
            return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
        }

        struct Stop
        {
            double height;
            Rgb colour;
        };

        Rgb ramp(const Stop *stops, int count, double h)
        {
            if (h <= stops[0].height)
                return stops[0].colour;
            for (int i = 1; i < count; ++i)
                if (h <= stops[i].height)
                    return mix(stops[i - 1].colour, stops[i].colour, static_cast<float>((h - stops[i - 1].height) / (stops[i].height - stops[i - 1].height)));
            return stops[count - 1].colour;
        }

        // Earth: the elevation data is flat at zero over the oceans, so the sea is one colour; land runs from lowland green through brown to snow.
        Rgb earth_colour(double h)
        {
            static const Stop land[] = {{0.0, {52, 128, 66}}, {250.0, {96, 160, 76}}, {700.0, {176, 184, 96}}, {1400.0, {196, 160, 92}},
                                        {2400.0, {160, 118, 84}}, {3600.0, {136, 120, 112}}, {5000.0, {200, 198, 200}}, {6500.0, {248, 248, 252}}};
            return h < 1.0 ? Rgb{44, 100, 172} : ramp(land, 8, h);
        }

        // Other bodies: a tint by height across the range seen on the whole body.
        Rgb world_colour(const Ground &g, double h)
        {
            const double t = std::clamp((h + 6000.0) / 12000.0, 0.0, 1.0);
            const bool red = g.name == "Mars";
            const Rgb low = red ? Rgb{110, 46, 30} : Rgb{58, 58, 64}, high = red ? Rgb{236, 178, 130} : Rgb{222, 218, 208};
            return mix(low, high, static_cast<float>(t));
        }

    }

    double View::kx() const
    {
        return std::max(0.05, std::cos(std::min(std::fabs(lat), 78.0) * rad));
    }

    void View::to_geo(double px, double py, double &lat_deg, double &lon_deg) const
    {
        lat_deg = lat - (py - h * 0.5) / ppd;
        lon_deg = wrap_lon(lon + (px - w * 0.5) / (ppd * kx()));
    }

    void View::to_pixel(double lat_deg, double lon_deg, double &px, double &py) const
    {
        double d = wrap_lon(lon_deg - lon);
        px = w * 0.5 + d * ppd * kx();
        py = h * 0.5 - (lat_deg - lat) * ppd;
    }

    std::vector<std::uint32_t> render(const Ground &ground, const View &view, const std::atomic<int> &current, int generation)
    {
        const int w = view.w, h = view.h;
        const int gw = w + 2, gh = h + 2;
        const double mpp = std::max(0.01, view.metres_per_pixel(ground.radius));
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::vector<float> heights(static_cast<std::size_t>(gw) * gh, nan);
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w) * h, 0xFF141820u);

        const int threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
        auto each_row = [&](int rows, const auto &work) {
            std::vector<std::thread> pool;
            for (int t = 0; t < threads; ++t)
                pool.emplace_back([&, t] {
                    for (int j = t; j < rows; j += threads)
                    {
                        if (current.load() != generation)
                            return;
                        work(j);
                    }
                });
            for (std::thread &th : pool)
                th.join();
        };

        // Heights on a grid with a one pixel border, so slopes at the edge are right too.
        if (ground.has_elevation)
            each_row(gh, [&](int j) {
                Elevation::Cursor cursor;
                double lat, lon;
                for (int i = 0; i < gw; ++i)
                {
                    view.to_geo(i - 1 + 0.5, j - 1 + 0.5, lat, lon);
                    if (std::fabs(lat) <= 90.0)
                        heights[static_cast<std::size_t>(j) * gw + i] = static_cast<float>(ground.elevation.height(lat * rad, lon * rad, mpp, ground.radius, cursor));
                }
            });
        if (current.load() != generation)
            return {};

        // Slopes steepen with zoom-out so relief still reads when a pixel spans kilometres.
        const double exaggeration = std::clamp(mpp / 60.0, 1.0, 30.0);
        const double lx = -0.5, ly = 0.5, lz = 0.7, ll = std::sqrt(lx * lx + ly * ly + lz * lz);
        const bool use_picture = view.imagery && ground.picture.ok();
        each_row(h, [&](int j) {
            double lat, lon;
            for (int i = 0; i < w; ++i)
            {
                view.to_geo(i + 0.5, j + 0.5, lat, lon);
                if (std::fabs(lat) > 90.0)
                    continue;
                const std::size_t g = static_cast<std::size_t>(j + 1) * gw + (i + 1);
                const float here = heights[g];
                Rgb c{128, 128, 128};
                bool shaded = !std::isnan(here);
                if (use_picture)
                {
                    float rgb[3];
                    ground.picture.colour(lat * rad, lon * rad, rgb);
                    c = {rgb[0] * 255.0f, rgb[1] * 255.0f, rgb[2] * 255.0f};
                }
                else if (!std::isnan(here))
                    c = ground.home ? earth_colour(here) : world_colour(ground, here);
                else if (ground.picture.ok())
                {
                    float rgb[3];
                    ground.picture.colour(lat * rad, lon * rad, rgb);
                    c = {rgb[0] * 255.0f, rgb[1] * 255.0f, rgb[2] * 255.0f};
                }
                if (shaded && !(ground.home && here <= 0.0f))
                {
                    const double dzdx = (heights[g + 1] - heights[g - 1]) / (2.0 * mpp);
                    const double dzdn = (heights[g - gw] - heights[g + gw]) / (2.0 * mpp);
                    const double nx = -dzdx * exaggeration, ny = -dzdn * exaggeration;
                    const double dot = (nx * lx + ny * ly + lz) / (std::sqrt(nx * nx + ny * ny + 1.0) * ll);
                    const float f = static_cast<float>(std::clamp(0.30 + 0.95 * dot, 0.25, 1.35));
                    c = {c.r * f, c.g * f, c.b * f};
                }
                auto byte = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 255.0f)); };
                pixels[static_cast<std::size_t>(j) * w + i] = 0xFF000000u | (byte(c.r) << 16) | (byte(c.g) << 8) | byte(c.b);
            }
        });
        if (current.load() != generation)
            return {};
        return pixels;
    }

    double height_at(const Ground &ground, const View &view, double lat_deg, double lon_deg)
    {
        if (!ground.has_elevation)
            return std::numeric_limits<double>::quiet_NaN();
        Elevation::Cursor cursor;
        return ground.elevation.height(lat_deg * rad, lon_deg * rad, view.metres_per_pixel(ground.radius), ground.radius, cursor);
    }
}
