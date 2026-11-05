#include "native_addons.h"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace kosmos::link
{
    namespace fs = std::filesystem;

    namespace
    {
        const char *platform_key()
        {
#ifdef _WIN32
            return "windows";
#else
            return "linux";
#endif
        }

        std::string random_token()
        {
            std::random_device rd;
            std::ostringstream out;
            for (int n = 0; n < 4; ++n)
                out << std::hex << rd();
            return out.str();
        }
    } // namespace

    std::uint32_t capability_from_name(const std::string &name)
    {
        if (name == "read")
            return cap_read;
        if (name == "controls")
            return cap_controls;
        if (name == "write")
            return cap_write;
        if (name == "spawn")
            return cap_spawn;
        if (name == "draw")
            return cap_draw;
        if (name == "keys")
            return cap_keys;
        if (name == "world")
            return cap_world;
        if (name == "ui")
            return cap_ui;
        if (name == "vessel")
            return cap_vessel;
        if (name == "draw3d")
            return cap_draw3d;
        return 0;
    }

    std::string capability_names(std::uint32_t caps)
    {
        std::string out;
        for (const char *name : {"read", "controls", "write", "spawn", "draw", "keys", "world", "ui", "vessel", "draw3d"})
            if (caps & capability_from_name(name))
                out += (out.empty() ? "" : ", ") + std::string(name);
        return out.empty() ? "none" : out;
    }

    NativeAddons::~NativeAddons() { shutdown(); }

    NativeEntry *NativeAddons::find(const std::string &id)
    {
        for (NativeEntry &e : entries_)
            if (e.id == id)
                return &e;
        return nullptr;
    }

    // The tools that ship with Kosmos are trusted from the start; any other addon still needs the player's approval.
    static bool is_bundled_tool(const std::string &id)
    {
        for (const char *tool : {"kosmosmission", "kosmosmap", "kosmoscad", "kosmosav", "shipedit"})
            if (id == tool)
                return true;
        return false;
    }

    void NativeAddons::scan(const std::vector<AddonRef> &addons)
    {
        for (const AddonRef &addon : addons)
        {
            std::ifstream in(fs::path(addon.directory) / "addon.json");
            if (!in)
                continue;
            nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_discarded() || !j.contains("native") || !j["native"].is_object())
                continue;
            const nlohmann::json &n = j["native"];
            NativeEntry e;
            e.id = addon.id;
            e.name = addon.name.empty() ? addon.id : addon.name;
            e.directory = addon.directory;
            e.autostart = n.value("autostart", true);
            e.restart = n.value("restart", false);
            e.allowed = is_bundled_tool(e.id);
            for (const nlohmann::json &cap : n.value("capabilities", nlohmann::json::array()))
                if (cap.is_string())
                    e.caps |= capability_from_name(cap.get<std::string>());
            if (n.contains("run") && n["run"].is_object())
                e.program = n["run"].value(platform_key(), "");
            if (e.program.empty())
                e.status = std::string("no program for ") + platform_key();
            if (!find(e.id))
                entries_.push_back(std::move(e));
        }
    }

    void NativeAddons::load()
    {
        std::ifstream in(state_file_);
        if (state_file_.empty() || !in)
            return;
        const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.contains("allowed"))
            return;
        for (const nlohmann::json &id : j["allowed"])
        {
            if (!id.is_string())
                continue;
            if (NativeEntry *e = find(id.get<std::string>()))
                e->allowed = true;
            else
            {
                NativeEntry e2;
                e2.id = id.get<std::string>();
                e2.name = e2.id;
                e2.allowed = true;
                e2.status = "not connected";
                entries_.push_back(std::move(e2));
            }
        }
    }

    void NativeAddons::save() const
    {
        if (state_file_.empty())
            return;
        nlohmann::json j;
        j["allowed"] = nlohmann::json::array();
        for (const NativeEntry &e : entries_)
            if (e.allowed)
                j["allowed"].push_back(e.id);
        std::error_code ec;
        fs::create_directories(fs::path(state_file_).parent_path(), ec);
        std::ofstream out(state_file_);
        if (out)
            out << j.dump(2);
    }

    std::uint32_t NativeAddons::granted(const std::string &addon, const std::string &, std::uint32_t requested)
    {
        const NativeEntry *e = find(addon);
        if (!e || !e->allowed)
            return 0;
        // An addon with a manifest gets what the manifest declares; one started by hand gets what it asked for.
        return e->caps ? (requested & e->caps) : requested;
    }

    void NativeAddons::seen(const std::string &addon, const std::string &name, std::uint32_t requested)
    {
        NativeEntry *e = find(addon);
        if (!e)
        {
            NativeEntry n;
            n.id = addon;
            n.name = name;
            n.caps = 0;
            entries_.push_back(std::move(n));
            e = &entries_.back();
        }
        if (e->program.empty())
        {
            e->caps = e->allowed ? e->caps : requested;
            e->status = "connected";
        }
    }

    bool NativeAddons::start(NativeEntry &e)
    {
        e.attempted = true;
        const fs::path dir = fs::weakly_canonical(fs::path(e.directory));
        const fs::path exe = fs::weakly_canonical(dir / e.program);
        const std::string rel = exe.lexically_relative(dir).string();
        if (rel.empty() || rel.rfind("..", 0) == 0)
        {
            e.status = "the program is outside the addon folder";
            return false;
        }
        std::error_code ec;
        if (!fs::is_regular_file(exe, ec))
        {
            e.status = "program not found: " + e.program;
            return false;
        }
        e.token = random_token();
        const std::string endpoint = "127.0.0.1:" + std::to_string(port_);
#ifdef _WIN32
        SetEnvironmentVariableA("KOSMOS_LINK", endpoint.c_str());
        SetEnvironmentVariableA("KOSMOS_LINK_TOKEN", e.token.c_str());
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring command = L"\"" + exe.wstring() + L"\"";
        const BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.wstring().c_str(), &si, &pi);
        SetEnvironmentVariableA("KOSMOS_LINK", nullptr);
        SetEnvironmentVariableA("KOSMOS_LINK_TOKEN", nullptr);
        if (!ok)
        {
            e.status = "could not start (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        CloseHandle(pi.hThread);
        e.process = reinterpret_cast<long long>(pi.hProcess);
#else
        const std::string exe_path = exe.string(), dir_path = dir.string();
        const pid_t pid = fork();
        if (pid < 0)
        {
            e.status = "could not start (fork failed)";
            return false;
        }
        if (pid == 0)
        {
            if (chdir(dir_path.c_str()) != 0)
                _exit(126);
            setenv("KOSMOS_LINK", endpoint.c_str(), 1);
            setenv("KOSMOS_LINK_TOKEN", e.token.c_str(), 1);
            execl(exe_path.c_str(), exe_path.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        e.process = pid;
#endif
        e.running = true;
        e.status = "running";
        return true;
    }

    void NativeAddons::stop(NativeEntry &e)
    {
        if (!e.running)
            return;
#ifdef _WIN32
        HANDLE h = reinterpret_cast<HANDLE>(e.process);
        TerminateProcess(h, 0);
        WaitForSingleObject(h, 1000);
        CloseHandle(h);
#else
        const pid_t pid = static_cast<pid_t>(e.process);
        kill(pid, SIGTERM);
        for (int n = 0; n < 20; ++n)
        {
            int status = 0;
            if (waitpid(pid, &status, WNOHANG) == pid)
                break;
            usleep(50000);
            if (n == 19)
            {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
        }
#endif
        e.running = false;
        e.process = 0;
        e.status = "stopped";
    }

    void NativeAddons::update()
    {
        for (NativeEntry &e : entries_)
        {
            if (e.running)
            {
                bool exited = false;
                std::string how;
#ifdef _WIN32
                DWORD code = 0;
                if (GetExitCodeProcess(reinterpret_cast<HANDLE>(e.process), &code) && code != STILL_ACTIVE)
                {
                    exited = true;
                    how = "exit code " + std::to_string(code);
                    CloseHandle(reinterpret_cast<HANDLE>(e.process));
                }
#else
                int status = 0;
                if (waitpid(static_cast<pid_t>(e.process), &status, WNOHANG) == static_cast<pid_t>(e.process))
                {
                    exited = true;
                    how = WIFEXITED(status) ? "exit code " + std::to_string(WEXITSTATUS(status)) : "killed by signal " + std::to_string(WTERMSIG(status));
                }
#endif
                if (exited)
                {
                    e.running = false;
                    e.process = 0;
                    e.status = "stopped (" + how + ")";
                    const bool failed = how != "exit code 0";
                    if (e.restart && failed && e.restarts < 3)
                    {
                        ++e.restarts;
                        start(e);
                    }
                }
            }
            else if (e.allowed && !e.program.empty() && e.autostart && !e.attempted)
                start(e);
        }
    }

    void NativeAddons::shutdown()
    {
        for (NativeEntry &e : entries_)
            stop(e);
    }

    void NativeAddons::set_allowed(const std::string &id, bool allowed)
    {
        NativeEntry *e = find(id);
        if (!e)
            return;
        e->allowed = allowed;
        if (!allowed)
            stop(*e);
        e->attempted = false;
        e->restarts = 0;
        save();
    }

    void NativeAddons::restart(const std::string &id)
    {
        NativeEntry *e = find(id);
        if (!e || e->program.empty())
            return;
        stop(*e);
        e->restarts = 0;
        e->status.clear();
        e->attempted = false;
        if (e->allowed)
            start(*e);
    }
} // namespace kosmos::link
