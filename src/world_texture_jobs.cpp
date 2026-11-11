#include "world_texture_jobs.h"

#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace kosmos
{
    namespace
    {
        constexpr std::size_t max_running = 2;
        constexpr int max_attempts = 3;

        std::string own_executable()
        {
#ifdef _WIN32
            char buffer[MAX_PATH];
            const DWORD n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
            return std::string(buffer, n);
#else
            std::error_code ec;
            const std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
            return ec ? std::string{} : p.string();
#endif
        }
    } // namespace

    bool WorldTextureJobs::contains(const Job &job) const
    {
        const auto same = [&](const Job &j) { return j.path == job.path; };
        if (std::any_of(queue_.begin(), queue_.end(), same))
            return true;
        return std::any_of(running_.begin(), running_.end(), [&](const Running &r) { return same(r.job); });
    }

    void WorldTextureJobs::add(const Job &job)
    {
        if (!contains(job))
            queue_.push_back(job);
    }

    bool WorldTextureJobs::launch(const Job &job, long long &handle)
    {
        const std::string exe = own_executable();
        if (exe.empty())
            return false;
        const std::string width = std::to_string(job.width), system = std::to_string(job.system);
        const std::string memory = job.width > 8192 ? "4096" : "2048";
#ifdef _WIN32
        std::string command = "\"" + exe + "\" --system " + system + " --gen-world \"" + job.body + "\" --gen-width " + width +
                              " --gen-threads 2 --gen-memory " + memory;
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr,
                            &startup, &info))
            return false;
        CloseHandle(info.hThread);
        handle = reinterpret_cast<long long>(info.hProcess);
        return true;
#else
        std::vector<std::string> args = {exe, "--system", system, "--gen-world", job.body, "--gen-width", width,
                                         "--gen-threads", "2", "--gen-memory", memory};
        std::vector<char *> argv;
        for (std::string &a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
        pid_t pid = 0;
        const int result = posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (result != 0)
            return false;
        // Nice, so the game itself keeps the cores it needs.
        setpriority(PRIO_PROCESS, static_cast<id_t>(pid), 10);
        handle = pid;
        return true;
#endif
    }

    std::vector<WorldTextureJobs::Job> WorldTextureJobs::poll()
    {
        std::vector<Job> finished;
        for (std::size_t i = 0; i < running_.size();)
        {
            bool done = false;
            bool success = false;
#ifdef _WIN32
            done = WaitForSingleObject(reinterpret_cast<HANDLE>(running_[i].handle), 0) == WAIT_OBJECT_0;
            if (done)
            {
                DWORD code = 1;
                success = GetExitCodeProcess(reinterpret_cast<HANDLE>(running_[i].handle), &code) && code == 0;
                CloseHandle(reinterpret_cast<HANDLE>(running_[i].handle));
            }
#else
            int status = 0;
            const pid_t result = waitpid(static_cast<pid_t>(running_[i].handle), &status, WNOHANG);
            done = result != 0;
            success = result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
            if (!done)
            {
                ++i;
                continue;
            }
            Job job = running_[i].job;
            running_.erase(running_.begin() + static_cast<std::ptrdiff_t>(i));
            if (success)
                finished.push_back(std::move(job));
            else if (job.attempts + 1 < max_attempts)
            {
                ++job.attempts;
                queue_.push_back(std::move(job));
            }
        }
        while (running_.size() < max_running && !queue_.empty())
        {
            Job job = queue_.front();
            queue_.pop_front();
            std::error_code ec;
            if (std::filesystem::exists(job.path, ec))
                continue;
            long long handle = 0;
            if (launch(job, handle))
                running_.push_back({job, handle});
            else if (job.attempts + 1 < max_attempts)
            {
                ++job.attempts;
                queue_.push_back(std::move(job));
            }
        }
        return finished;
    }

    void WorldTextureJobs::cancel()
    {
        queue_.clear();
        for (const Running &r : running_)
        {
#ifdef _WIN32
            TerminateProcess(reinterpret_cast<HANDLE>(r.handle), 1);
            CloseHandle(reinterpret_cast<HANDLE>(r.handle));
#else
            kill(static_cast<pid_t>(r.handle), SIGKILL);
            waitpid(static_cast<pid_t>(r.handle), nullptr, 0);
#endif
        }
        running_.clear();
    }
} // namespace kosmos
