#undef NDEBUG
#include "orbiter_zip.h"

#include <wx/init.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    wxInitializer wx;
    assert(wx.IsOk());
    const fs::path root = fs::temp_directory_path() /
                          ("kosmos-zip-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const fs::path zip = root / "fixture.zip";
    {
        wxFileOutputStream output(wxString::FromUTF8(zip.u8string().c_str()));
        wxZipOutputStream archive(output);
        for (const char *name : {"Enceladus/Archive/Surf.tree", "Europa/Archive/Surf.tree",
                                 "Io/Archive/Surf.tree", "Io/Archive/Elev.tree",
                                 "Phobos/Archive/Surf.tree", "Phobos/Archive/Elev.tree",
                                 "Vesta/Archive/Surf.tree", "Vesta/Archive/Elev.tree"})
        {
            assert(archive.PutNextEntry(wxString::FromUTF8(std::string("Textures/") + name)));
            archive.Write(name, std::strlen(name));
            assert(archive.IsOk());
        }
        assert(archive.Close());
    }
    std::string error;
    {
        wxFileInputStream input(wxString::FromUTF8(zip.u8string().c_str()));
        wxZipInputStream archive(input);
        std::unique_ptr<wxZipEntry> entry{archive.GetNextEntry()};
        assert(entry);
        assert(entry->GetName(wxPATH_UNIX) == "Textures/Enceladus/Archive/Surf.tree");
#ifdef _WIN32
        assert(entry->GetName() == "Textures\\Enceladus\\Archive\\Surf.tree");
#endif
    }
    const fs::path target = root / "MinorBodies";
    assert(launcher::extract_minor_bodies(zip, target, error));
    assert(fs::exists(target / "Io/Archive/Elev.tree"));
    {
        std::ifstream contents(target / "Io/Archive/Elev.tree", std::ios::binary);
        const std::string value((std::istreambuf_iterator<char>(contents)), std::istreambuf_iterator<char>());
        assert(value == "Io/Archive/Elev.tree");
    }
    assert(!launcher::extract_minor_bodies(zip, target, error));
    assert(fs::exists(target / "Io/Archive/Elev.tree"));
    {
        wxFileOutputStream output(wxString::FromUTF8(zip.u8string().c_str()));
        wxZipOutputStream archive(output);
        assert(archive.PutNextEntry("Textures/../../escape"));
        archive.Write("bad", 3);
        assert(archive.Close());
    }
    assert(!launcher::extract_minor_bodies(zip, root / "bad", error));
    assert(!fs::exists(root / "bad") && !fs::exists(root / "escape"));
    assert(!fs::exists(root / "bad.installing"));
    {
        wxFileOutputStream output(wxString::FromUTF8(zip.u8string().c_str()));
        wxZipOutputStream archive(output);
        assert(archive.PutNextEntry("Textures/Io/Archive/Surf.tree"));
        archive.Write("one", 3);
        assert(archive.PutNextEntry("Textures/Io/Archive/Surf.tree"));
        archive.Write("two", 3);
        assert(archive.Close());
    }
    assert(!launcher::extract_minor_bodies(zip, root / "duplicate", error));
    assert(error.find("duplicate") != std::string::npos);
    assert(!fs::exists(root / "duplicate") && !fs::exists(root / "duplicate.installing"));
    if (argc > 1)
    {
        assert(launcher::extract_minor_bodies(fs::u8path(argv[1]), root / "real", error));
        assert(fs::file_size(root / "real/Phobos/Archive/Surf.tree") == 56625665);
    }
    fs::remove_all(root);
}
