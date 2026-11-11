#include "display.h"
#include "system.h"

#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    if (!sl::configure_graphics_backend_from_args(argc, argv) ||
        !sl::set_gfx_mode(sl::GFX_AUTODETECT_WINDOWED, 128, 128))
    {
        std::fprintf(stderr, "Unable to initialize renderer\n");
        return 1;
    }
    if (sl::get_gpu_diagnostics().timing_valid)
        return 2;
    sl::set_gpu_diagnostics_enabled(true);
    bool received = false;
    std::uint64_t last = 0;
    for (std::uint64_t frame = 1; frame <= 100; ++frame)
    {
        sl::gpu_diagnostics_begin_frame(frame);
        sl::clear_to_colour(40, 50, 60, 255);
        sl::show_video_bitmap();
        sl::end_frame();
        const auto diagnostics = sl::get_gpu_diagnostics();
        if (diagnostics.timing_valid)
        {
            if (!std::isfinite(diagnostics.elapsed_ms) || diagnostics.elapsed_ms < 0 ||
                diagnostics.frame_number > frame || (received && diagnostics.frame_number < last))
                return 3;
            received = true;
            last = diagnostics.frame_number;
        }
    }
    const auto sample = sl::get_gpu_diagnostics();
    if (sample.timing_supported && !received)
    {
        std::fprintf(stderr, "No completed GPU sample\n");
        return 4;
    }
    std::printf("%s GPU timing %s, latest frame %llu, %.3f ms\n",
                sl::graphics_backend_name().c_str(), received ? "available" : "unsupported",
                static_cast<unsigned long long>(last), sample.elapsed_ms);
    if (sample.memory_valid || sample.memory_budget_valid || sample.memory_total_valid || sample.memory_free_valid)
        std::printf("GPU memory usage=%llu (%s), budget=%llu (%s), total=%llu (%s), free=%llu (%s) bytes; usage scope=%s\n",
                    static_cast<unsigned long long>(sample.memory_usage_bytes),
                    sample.memory_valid ? "valid" : "unavailable",
                    static_cast<unsigned long long>(sample.memory_budget_bytes),
                    sample.memory_budget_valid ? "valid" : "unavailable",
                    static_cast<unsigned long long>(sample.memory_total_bytes),
                    sample.memory_total_valid ? "valid" : "unavailable",
                    static_cast<unsigned long long>(sample.memory_free_bytes),
                    sample.memory_free_valid ? "valid" : "unavailable",
                    sample.memory_valid ? (sample.memory_process_scope ? "process" : "adapter") : "unavailable");
    else
        std::puts("GPU memory unavailable");
    sl::set_gpu_diagnostics_enabled(false);
    if (sl::get_gpu_diagnostics().timing_valid)
        return 5;
    sl::set_gpu_diagnostics_enabled(true);
    if (sl::get_gpu_diagnostics().timing_valid)
        return 6;
    for (std::uint64_t frame = 1001; frame <= 1020; ++frame)
    {
        sl::gpu_diagnostics_begin_frame(frame);
        sl::clear_to_colour(30, 20, 10, 255);
        sl::show_video_bitmap();
        sl::end_frame();
        const auto diagnostics = sl::get_gpu_diagnostics();
        if (diagnostics.timing_valid && diagnostics.frame_number < 1001)
            return 7;
    }
    sl::set_gpu_diagnostics_enabled(false);
    sl::shutdown();
    return 0;
}
