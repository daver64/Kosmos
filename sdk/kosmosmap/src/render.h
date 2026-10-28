#pragma once

#include "terrain.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace kmap
{
    /** A map view: a plate carree centred on a point, with east scaled by the cosine of the centre latitude so it looks right locally. */
    struct View
    {
        double lat = 0.0, lon = 0.0; // degrees at the centre
        double ppd = 4.0;            // pixels per degree of latitude
        int w = 800, h = 600;
        bool imagery = false;

        double kx() const;
        /** Metres on the ground per pixel at the centre. */
        double metres_per_pixel(double radius) const { return radius * pi / 180.0 / ppd; }
        void to_geo(double px, double py, double &lat_deg, double &lon_deg) const;
        /** Longitudes are taken the short way round from the view centre, so a view across the date line still lines up. */
        void to_pixel(double lat_deg, double lon_deg, double &px, double &py) const;
    };

    /** Renders the ground as ARGB32 pixels (w * h), or returns an empty vector if `generation` stops matching `current` part way. */
    std::vector<std::uint32_t> render(const Ground &ground, const View &view, const std::atomic<int> &current, int generation);

    /** Height in metres for the cursor read-out; NaN when there is no elevation data. */
    double height_at(const Ground &ground, const View &view, double lat_deg, double lon_deg);
}
