#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace kosmos
{
    enum class PerfSection : std::size_t
    {
        simulation, physics, vessel_lua, mfd_init, mfd_button, mfd_update, mfd_draw,
        instruments, terrain, scene, hud, gui, present, count
    };

    class PerformanceMetrics
    {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr std::size_t sections = static_cast<std::size_t>(PerfSection::count);
        static constexpr std::size_t window = 120;
        struct Frame
        {
            std::array<double, sections> ms{};
            std::array<std::size_t, sections> calls{};
            double elapsed_ms = 0.0;
        };
        struct Summary
        {
            std::array<double, sections> average{}, maximum{};
            std::array<std::size_t, sections> calls{};
            double frame_average = 0.0, frame_maximum = 0.0;
            std::size_t frames = 0;
        };
        struct GpuSummary
        {
            double average = 0.0, maximum = 0.0, latest = 0.0;
            std::size_t samples = 0;
            std::uint64_t frame = 0;
        };

        void set_enabled(bool enabled)
        {
            if (enabled_ != enabled)
            {
                enabled_ = enabled;
                current_ = {};
                count_ = next_ = 0;
                start_ = {};
                pending_ = false;
                gpu_count_ = gpu_next_ = 0;
                gpu_last_frame_ = 0;
            }
        }
        bool enabled() const { return enabled_; }
        void begin_frame()
        {
            if (enabled_)
                begin_frame_at(Clock::now());
        }
        void begin_frame_at(Clock::time_point now)
        {
            if (enabled_)
            {
                if (pending_)
                {
                    current_.elapsed_ms = std::chrono::duration<double, std::milli>(now - start_).count();
                    frames_[next_] = current_;
                    next_ = (next_ + 1) % window;
                    count_ = std::min(count_ + 1, window);
                }
                current_ = {};
                start_ = now;
                pending_ = false;
            }
        }
        void end_frame()
        {
            if (enabled_ && start_ != Clock::time_point{} && !pending_)
            {
                pending_ = true;
            }
        }
        void add(PerfSection section, double ms)
        {
            if (enabled_ && start_ != Clock::time_point{} && !pending_)
            {
                const auto index = static_cast<std::size_t>(section);
                current_.ms[index] += ms;
                ++current_.calls[index];
            }
        }
        Summary summary() const
        {
            Summary result;
            result.frames = count_;
            for (std::size_t i = 0; i < count_; ++i)
            {
                const Frame &frame = frames_[i];
                result.frame_average += frame.elapsed_ms;
                result.frame_maximum = std::max(result.frame_maximum, frame.elapsed_ms);
                for (std::size_t j = 0; j < sections; ++j)
                {
                    result.average[j] += frame.ms[j];
                    result.maximum[j] = std::max(result.maximum[j], frame.ms[j]);
                    result.calls[j] += frame.calls[j];
                }
            }
            if (count_)
            {
                result.frame_average /= count_;
                for (double &value : result.average)
                    value /= count_;
            }
            return result;
        }
        void add_gpu_sample(std::uint64_t frame, double ms)
        {
            if (!enabled_ || (gpu_count_ && frame <= gpu_last_frame_) || !std::isfinite(ms) || ms < 0.0)
                return;
            gpu_samples_[gpu_next_] = ms;
            gpu_next_ = (gpu_next_ + 1) % window;
            gpu_count_ = std::min(gpu_count_ + 1, window);
            gpu_last_frame_ = frame;
        }
        GpuSummary gpu_summary() const
        {
            GpuSummary result;
            result.samples = gpu_count_;
            result.frame = gpu_last_frame_;
            if (!gpu_count_)
                return result;
            result.latest = gpu_samples_[(gpu_next_ + window - 1) % window];
            for (std::size_t i = 0; i < gpu_count_; ++i)
            {
                result.average += gpu_samples_[i];
                result.maximum = std::max(result.maximum, gpu_samples_[i]);
            }
            result.average /= gpu_count_;
            return result;
        }

    private:
        bool enabled_ = false;
        bool pending_ = false;
        Clock::time_point start_{};
        Frame current_{};
        std::array<Frame, window> frames_{};
        std::size_t count_ = 0, next_ = 0;
        std::array<double, window> gpu_samples_{};
        std::size_t gpu_count_ = 0, gpu_next_ = 0;
        std::uint64_t gpu_last_frame_ = 0;
    };

    class PerfScope
    {
    public:
        PerfScope(PerformanceMetrics *metrics, PerfSection section)
            : metrics_(metrics), section_(section), start_(metrics && metrics->enabled() ? PerformanceMetrics::Clock::now() : PerformanceMetrics::Clock::time_point{}) {}
        PerfScope(PerformanceMetrics &metrics, PerfSection section) : PerfScope(&metrics, section) {}
        ~PerfScope()
        {
            if (start_ != PerformanceMetrics::Clock::time_point{})
                metrics_->add(section_, std::chrono::duration<double, std::milli>(PerformanceMetrics::Clock::now() - start_).count());
        }
    private:
        PerformanceMetrics *metrics_;
        PerfSection section_;
        PerformanceMetrics::Clock::time_point start_;
    };
}
