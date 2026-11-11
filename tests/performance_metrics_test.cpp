#include "performance_metrics.h"
#include "process_memory.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool passed, const char *description)
{
    if (!passed)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        std::exit(1);
    }
}

int main()
{
    using namespace kosmos;
    using namespace std::chrono;
    PerformanceMetrics metrics;
    const auto origin = PerformanceMetrics::Clock::time_point(seconds(1));
    metrics.begin_frame_at(origin);
    metrics.add(PerfSection::physics, 5);
    metrics.end_frame();
    check(metrics.summary().frames == 0, "disabled metrics");

    metrics.set_enabled(true);
    for (int i = 0; i < 125; ++i)
    {
        metrics.begin_frame_at(origin + milliseconds(i * 20));
        metrics.add(PerfSection::simulation, 4);
        metrics.add(PerfSection::physics, 1);
        metrics.add(PerfSection::physics, 2);
        metrics.end_frame();
        metrics.add(PerfSection::physics, 100);
    }
    const auto s = metrics.summary();
    const auto physics = static_cast<std::size_t>(PerfSection::physics);
    const auto simulation = static_cast<std::size_t>(PerfSection::simulation);
    check(s.frames == 120, "bounded window");
    check(std::abs(s.frame_average - 20) < 1e-9, "frame average");
    check(std::abs(s.frame_maximum - 20) < 1e-9, "frame maximum");
    check(std::abs(s.average[physics] - 3) < 1e-9, "accumulated physics average");
    check(std::abs(s.maximum[physics] - 3) < 1e-9, "accumulated physics maximum");
    check(s.calls[physics] == 240, "substep count");
    check(std::abs(s.average[simulation] - 4) < 1e-9, "nested simulation measurement");
    metrics.set_enabled(false);
    check(metrics.summary().frames == 0, "disable resets samples");
    metrics.set_enabled(true);
    metrics.begin_frame_at(origin);
    metrics.end_frame();
    metrics.begin_frame_at(origin + milliseconds(25));
    check(metrics.summary().frames == 1, "reenable starts new window");
    check(std::abs(metrics.summary().frame_average - 25) < 1e-9, "reenabled frame timing");
    metrics.add_gpu_sample(7, 5);
    metrics.add_gpu_sample(7, 20);
    metrics.add_gpu_sample(6, 20);
    metrics.add_gpu_sample(8, std::numeric_limits<double>::quiet_NaN());
    metrics.add_gpu_sample(9, 3);
    const auto gpu = metrics.gpu_summary();
    check(gpu.samples == 2 && gpu.frame == 9, "GPU samples deduplicated and ordered");
    check(gpu.average == 4 && gpu.maximum == 5 && gpu.latest == 3, "GPU aggregation");
    metrics.set_enabled(false);
    check(metrics.gpu_summary().samples == 0, "GPU samples reset");
    metrics.set_enabled(true);
    metrics.begin_frame_at(origin);
    metrics.end_frame();
    metrics.begin_frame_at(origin + milliseconds(500));
    metrics.end_frame();
    for (int i = 1; i <= 120; ++i)
    {
        metrics.begin_frame_at(origin + milliseconds(500 + i * 10));
        metrics.end_frame();
    }
    check(metrics.summary().frames == 120, "bounded window after pause");
    check(std::abs(metrics.summary().frame_maximum - 10) < 1e-9, "old pause expires from worst frame");
#if defined(__linux__)
    const auto memory = process_resident_mib();
    check(memory && *memory > 0, "Linux resident memory available");
    check(!process_resident_mib("/dev/null"), "invalid resident memory data unavailable");
    check(!process_resident_mib("/path/that/does/not/exist/statm"), "missing resident memory file unavailable");
#elif defined(_WIN32)
    const auto memory = process_resident_mib();
    check(memory && *memory > 0, "Windows working set available");
#endif
    std::puts("performance metrics tests passed");
}
