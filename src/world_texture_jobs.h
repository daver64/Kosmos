#pragma once

#include <deque>
#include <string>
#include <vector>

namespace kosmos
{
    // Fills in the far-view images of invented worlds in the background. Each image is made by a child copy of this program
    // (--gen-world), because the renderer's bitmap functions may only be touched from the main thread.
    class WorldTextureJobs
    {
    public:
        struct Job
        {
            int system = 0;
            std::string body;
            int width = 0;
            std::string path; // the cache file the job will write
        };

        ~WorldTextureJobs() { cancel(); }

        // Queued jobs run in the order they were added, at most two at a time. A job already queued or running is ignored.
        void add(const Job &job);

        // Drops everything queued and kills everything running.
        void cancel();

        // Starts queued jobs as slots free up and returns the jobs that finished since the last call. Call it every frame.
        std::vector<Job> poll();

        bool busy() const { return !queue_.empty() || !running_.empty(); }

    private:
        struct Running
        {
            Job job;
            long long handle = 0; // a pid, or a process HANDLE on Windows
        };

        bool launch(const Job &job, long long &handle);
        bool contains(const Job &job) const;

        std::deque<Job> queue_;
        std::vector<Running> running_;
    };
} // namespace kosmos
