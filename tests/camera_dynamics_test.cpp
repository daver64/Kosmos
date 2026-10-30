#include "camera_dynamics.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using kosmos::CameraDynamics;
using kosmos::HeadMotion;

int main()
{
    HeadMotion m;
    m.max_offset = 10.0; // out of the way, to test the linear model

    // A step in acceleration (after the washout has settled) pushes the eye back, then rings and settles.
    CameraDynamics d;
    for (int i = 0; i < 60 * 30; ++i)
        d.step(1.0 / 60.0, sl::dvec3(0.0), m);
    double peak = 0.0;
    for (int i = 0; i < 60; ++i)
    {
        d.step(1.0 / 60.0, sl::dvec3(0.0, 0.0, -10.0), m); // accelerating forward (-Z)
        peak = std::max(peak, d.offset(m).z);
        assert(std::isfinite(peak));
    }
    assert(peak > 0.02 && d.offset(m).z > 0.0); // eye lags backwards
    assert(std::fabs(d.offset(m).x) < 1e-9);

    // Sustained acceleration is washed out: the eye returns to the seat.
    for (int i = 0; i < 60 * 40; ++i)
        d.step(1.0 / 60.0, sl::dvec3(0.0, 0.0, -10.0), m);
    assert(std::fabs(d.offset(m).z) < 0.01);

    // The first sample never kicks: a steady 1 g from the start leaves the eye at the seat.
    CameraDynamics ground;
    for (int i = 0; i < 600; ++i)
        ground.step(1.0 / 60.0, sl::dvec3(0.0, 9.81, 0.0), m);
    assert(std::fabs(ground.offset(m).y) < 1e-6);

    // Stable with long frames, and the soft limit holds under a huge acceleration.
    HeadMotion tight;
    CameraDynamics wild;
    wild.step(0.1, sl::dvec3(0.0), tight);
    for (int i = 0; i < 100; ++i)
    {
        wild.step(0.1, sl::dvec3(1000.0, -1000.0, 1000.0), tight);
        const sl::dvec3 o = wild.offset(tight);
        assert(std::fabs(o.x) <= tight.max_offset + 1e-9 && std::fabs(o.y) <= tight.max_offset + 1e-9 && std::fabs(o.z) <= tight.max_offset + 1e-9);
    }

    // Strength zero holds the eye still; relax() decays the offset.
    HeadMotion off;
    off.strength = 0.0;
    assert(glm::length(wild.offset(off)) < 1e-12);
    for (int i = 0; i < 600; ++i)
        wild.relax(1.0 / 60.0, tight);
    assert(glm::length(wild.offset(tight)) < 0.01);

    std::puts("camera_dynamics test passed");
    return 0;
}
