#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kosmos
{
    /** True when this build can fetch from the web (WinHTTP on Windows, libcurl elsewhere when it was found at build time). */
    bool http_available();
    /** Fetches a URL into `body`; false on any failure. Blocks, so call it from a worker thread. */
    bool http_get(const std::string &url, std::string &body, int timeout_seconds);

    /**
     * Real METARs for a set of stations, fetched from NOAA's Aviation Weather Center on a worker thread. One request covers
     * every station, and after a successful fetch nothing is requested for an hour: the time is stored with the cache file,
     * so restarting does not fetch again either. A failed fetch is retried after fifteen minutes.
     */
    class LiveWeather
    {
    public:
        ~LiveWeather();

        /** Station ids to ask for and the cache file (its folder is created); loads an existing cache. */
        void configure(std::vector<std::string> ids, const std::string &cache_file);
        /** Starts a fetch in the background when the data is more than an hour old. Cheap to call every frame. */
        void poll(double now_unix);
        /** New observations since the last call (one METAR line each); false when there is nothing new. */
        bool take(std::vector<std::string> &lines);
        /** When the data was fetched (seconds since 1970), or 0. */
        double fetched() const { return fetched_.load(); }
        bool busy() const { return busy_.load(); }
        /** "updated 14:05Z", "fetching...", "unavailable" and so on, for the menu. */
        std::string status(double now_unix) const;

        static constexpr double refresh_seconds = 3600.0;
        static constexpr double retry_seconds = 900.0;

    private:
        void run(double now_unix);
        void load_cache();

        std::vector<std::string> ids_;
        std::string cache_;
        std::thread worker_;
        std::atomic<bool> busy_{false};
        std::atomic<double> fetched_{0.0};
        double last_attempt_ = 0.0;
        bool failed_ = false;
        mutable std::mutex mutex_;
        std::vector<std::string> pending_;
    };

    /** The current UTC time in seconds since 1970. */
    double real_unix_time();
}
