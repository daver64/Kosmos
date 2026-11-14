#include "cinematic_camera.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    using namespace kosmos::app;
    const std::vector<CinematicCameraKey> keys{{0.0, 350.0, 0.0, 100.0, 40.0},
                                                {4.0, 370.0, 20.0, 200.0, 60.0}};
    const auto first = cinematic_camera_at(keys, -1.0);
    const auto quarter = cinematic_camera_at(keys, 1.0);
    const auto middle = cinematic_camera_at(keys, 2.0);
    const auto last = cinematic_camera_at(keys, 10.0);
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    if (!near(first.yaw, 350.0) || !near(quarter.yaw, 353.125) ||
        !near(middle.yaw, 360.0) || !near(middle.pitch, 10.0) ||
        !near(middle.distance, 150.0) || !near(middle.fov, 50.0) || !near(last.yaw, 370.0))
    {
        std::fprintf(stderr, "cinematic camera interpolation failed\n");
        return 1;
    }
    std::puts("cinematic_camera_test ok");
    return 0;
}
