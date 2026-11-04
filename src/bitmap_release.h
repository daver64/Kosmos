#pragma once

#include <mutex>
#include <thread>
#include <vector>

#include "sl.h"

namespace kosmos
{
    /**
     * Frees simlib bitmaps through sl::destroy_bitmap from any thread. destroy_bitmap touches renderer state, so once the main thread has
     * claimed ownership a release from another thread is queued and finished by drain_released_bitmaps() on the main thread.
     * Without a claim (command line tools) the bitmap is destroyed on the calling thread.
     */
    namespace bitmap_release_detail
    {
        struct State
        {
            std::mutex mutex;
            std::thread::id owner;
            bool claimed = false;
            std::vector<sl::Bitmap *> pending;
        };
        inline State &state()
        {
            static State s;
            return s;
        }
    } // namespace bitmap_release_detail

    /** Call once from the thread that owns the renderer. */
    inline void claim_bitmap_thread()
    {
        auto &s = bitmap_release_detail::state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.owner = std::this_thread::get_id();
        s.claimed = true;
    }

    inline void discard_bitmap(sl::Bitmap *bitmap)
    {
        if (!bitmap)
            return;
        auto &s = bitmap_release_detail::state();
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            if (s.claimed && s.owner != std::this_thread::get_id())
            {
                s.pending.push_back(bitmap);
                return;
            }
        }
        sl::destroy_bitmap(bitmap);
    }

    /** Main thread only: destroys the bitmaps other threads released. */
    inline void drain_released_bitmaps()
    {
        auto &s = bitmap_release_detail::state();
        std::vector<sl::Bitmap *> batch;
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            batch.swap(s.pending);
        }
        for (sl::Bitmap *bitmap : batch)
            sl::destroy_bitmap(bitmap);
    }
} // namespace kosmos
