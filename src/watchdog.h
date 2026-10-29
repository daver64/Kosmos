#pragma once
// Temporary freeze diagnostic. A background thread logs which frame stage the main loop is stuck in.
// To remove it: delete this file, the WATCHDOG lines in app_frame.cpp, and the include in app_frame.cpp.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace kosmos
{
    class Watchdog
    {
    public:
        // The main loop pays one relaxed atomic store per stage; the thread sleeps between checks.
        static void stage(const char *name) { instance().current.store(name, std::memory_order_relaxed); }

        static void frame_done(double sim_time, int warp)
        {
            Watchdog &w = instance();
            w.sim_time.store(sim_time, std::memory_order_relaxed);
            w.warp.store(warp, std::memory_order_relaxed);
            w.frames.fetch_add(1, std::memory_order_relaxed);
        }

        static void start()
        {
            Watchdog &w = instance();
            if (!w.thread.joinable())
                w.thread = std::thread([&w] { w.watch(); });
        }

    private:
        std::atomic<const char *> current{"startup"};
        std::atomic<unsigned long> frames{0};
        std::atomic<double> sim_time{0.0};
        std::atomic<int> warp{0};
        std::atomic<bool> quit{false};
        std::thread thread;

        static Watchdog &instance()
        {
            static Watchdog w;
            return w;
        }

        ~Watchdog()
        {
            quit = true;
            if (thread.joinable())
                thread.join();
        }

        void watch()
        {
            using namespace std::chrono;
            unsigned long seen = frames.load();
            auto since = steady_clock::now();
            int reported = 0;
            while (!quit)
            {
                std::this_thread::sleep_for(milliseconds(500));
                const unsigned long now_frames = frames.load();
                const auto now = steady_clock::now();
                if (now_frames != seen)
                {
                    if (reported)
                        std::fprintf(stderr, "[watchdog] the main loop recovered after %.1f s\n", duration<double>(now - since).count());
                    seen = now_frames;
                    since = now;
                    reported = 0;
                    continue;
                }
                const double stalled = duration<double>(now - since).count();
                // Report at 5 s, then every 30 s.
                if (stalled >= (reported ? 5.0 + 30.0 * reported : 5.0))
                {
                    std::fprintf(stderr, "[watchdog] no frame for %.0f s; stuck in \"%s\" (frame %lu, sim time %.0f s, warp index %d)\n",
                                 stalled, current.load(), now_frames, sim_time.load(), warp.load());
                    std::fflush(stderr);
                    ++reported;
                }
            }
        }
    };
} // namespace kosmos
