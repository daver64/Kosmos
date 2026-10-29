#include "settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace planetbake
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string env(const char *name)
        {
            const char *v = std::getenv(name);
            return v ? v : "";
        }

        // Anything that could be taken by a shell as more than a file name is refused.
        bool plain_path(const std::string &s)
        {
            for (char c : s)
                if (c == '"' || c == '$' || c == '`' || c == '\\' || c == '\n')
                    return false;
            return true;
        }
    }

    std::string settings_dir()
    {
#ifdef _WIN32
        return (fs::path(env("APPDATA")) / "planetbake").string();
#else
        const std::string xdg = env("XDG_CONFIG_HOME");
        return (xdg.empty() ? fs::path(env("HOME")) / ".config" : fs::path(xdg)) .string() + "/planetbake";
#endif
    }

    std::string cache_dir()
    {
#ifdef _WIN32
        return (fs::path(env("LOCALAPPDATA")) / "planetbake").string();
#else
        const std::string xdg = env("XDG_CACHE_HOME");
        return (xdg.empty() ? fs::path(env("HOME")) / ".cache" : fs::path(xdg)).string() + "/planetbake";
#endif
    }

    Settings load_settings()
    {
        Settings s;
        std::ifstream f(fs::path(settings_dir()) / "settings.json");
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const std::size_t at = text.find("\"editor\"");
        if (at != std::string::npos)
        {
            const std::size_t open = text.find('"', text.find(':', at) + 1);
            std::string value;
            for (std::size_t i = open + 1; open != std::string::npos && i < text.size() && text[i] != '"'; ++i)
            {
                if (text[i] == '\\' && i + 1 < text.size())
                    ++i;
                value += text[i];
            }
            if (!value.empty())
                s.editor = value;
        }
        return s;
    }

    bool save_settings(const Settings &settings, std::string &error)
    {
        std::error_code ec;
        fs::create_directories(settings_dir(), ec);
        std::ofstream f(fs::path(settings_dir()) / "settings.json");
        std::string escaped;
        for (char c : settings.editor)
        {
            if (c == '"' || c == '\\')
                escaped += '\\';
            escaped += c;
        }
        f << "{\n  \"editor\": \"" << escaped << "\"\n}\n";
        if (!f)
        {
            error = "cannot write " + (fs::path(settings_dir()) / "settings.json").string();
            return false;
        }
        return true;
    }

    bool launch_editor(const std::string &editor, const std::string &file, std::string &error)
    {
        if (editor.empty() || !plain_path(file) || !plain_path(editor))
        {
            error = "the editor command or the file name has characters that are not allowed";
            return false;
        }
#ifdef _WIN32
        const std::string command = "start \"\" " + editor + " \"" + file + "\"";
        if (std::system(command.c_str()) != 0)
        {
            error = "cannot start " + editor;
            return false;
        }
        return true;
#else
        const pid_t pid = fork();
        if (pid < 0)
        {
            error = "cannot start " + editor;
            return false;
        }
        if (pid == 0)
        {
            // Detach so the editor outlives planetbake and leaves no zombie.
            if (fork() != 0)
                _exit(0);
            setsid();
            const std::string command = editor + " \"$1\"";
            execlp("sh", "sh", "-c", command.c_str(), "planetbake", file.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return true;
#endif
    }
}
