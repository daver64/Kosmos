#pragma once

#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>

namespace launcher
{
    inline bool extract_minor_bodies(const std::filesystem::path &archive, const std::filesystem::path &target,
                                     std::string &error, const std::function<void()> &progress = {})
    {
        namespace fs = std::filesystem;
        error.clear();
        const std::set<std::string> expected = {
            "Enceladus/Archive/Surf.tree", "Europa/Archive/Surf.tree",
            "Io/Archive/Surf.tree", "Io/Archive/Elev.tree",
            "Phobos/Archive/Surf.tree", "Phobos/Archive/Elev.tree",
            "Vesta/Archive/Surf.tree", "Vesta/Archive/Elev.tree"};
        const fs::path staging = target.parent_path() / (target.filename().string() + ".installing");
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        if (ec || fs::exists(target, ec) || ec || !fs::create_directory(staging, ec))
        {
            error = "Cannot create installation directory or target already exists" + (ec ? ": " + ec.message() : ".");
            return false;
        }
        std::set<std::string> seen;
        wxFileInputStream input(wxString::FromUTF8(archive.u8string().c_str()));
        wxZipInputStream zip(input);
        while (std::unique_ptr<wxZipEntry> entry{zip.GetNextEntry()})
        {
            if (entry->IsDir())
                continue;
            const std::string name(entry->GetName(wxPATH_UNIX).ToUTF8());
            if (name.rfind("Textures/", 0) != 0)
            {
                error = "Unexpected file in minor-body archive: " + name;
                break;
            }
            const std::string relative = name.substr(9);
            if (!expected.count(relative) || !seen.insert(relative).second)
            {
                error = "Unexpected or duplicate file in minor-body archive: " + name;
                break;
            }
            const fs::path dest = staging / fs::u8path(relative);
            fs::create_directories(dest.parent_path(), ec);
            std::ofstream out(dest, std::ios::binary);
            if (ec || !out)
            {
                error = "Cannot create " + dest.string();
                break;
            }
            char buffer[1024 * 1024];
            while (zip.CanRead())
            {
                zip.Read(buffer, sizeof buffer);
                const size_t count = zip.LastRead();
                if (count)
                    out.write(buffer, static_cast<std::streamsize>(count));
                if (!out)
                {
                    error = "Cannot write " + dest.string();
                    break;
                }
                if (progress)
                    progress();
            }
            if (!error.empty())
                break;
            const auto written = out.tellp();
            out.close();
            if (!out || static_cast<wxFileOffset>(written) != entry->GetSize())
            {
                error = "Incomplete file in minor-body archive: " + name;
                break;
            }
        }
        if (error.empty() && seen != expected)
            error = "Minor-body archive is incomplete.";
        if (error.empty())
        {
            fs::rename(staging, target, ec);
            if (ec)
                error = "Cannot install minor-body scenery: " + ec.message();
        }
        if (!error.empty())
        {
            fs::remove_all(staging, ec);
            if (ec)
                error += "; cannot remove temporary files: " + ec.message();
            return false;
        }
        return true;
    }
}
