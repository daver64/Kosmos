#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#include <glib.h>
#include <filesystem>

inline void kosmos_gtk_schemas(const char *tool)
{
    wchar_t exe[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(*exe)));
    if (!length || length >= sizeof(exe) / sizeof(*exe))
        return;
    namespace fs = std::filesystem;
    fs::path dir = fs::path(exe).parent_path();
    for (int i = 0; i < 6 && !dir.empty(); ++i, dir = dir.parent_path())
        for (const fs::path &candidate : {dir / "share" / "glib-2.0" / "schemas",
                                         dir / "addons" / tool / "share" / "glib-2.0" / "schemas"})
        {
            std::error_code ec;
            if (fs::is_regular_file(candidate / "gschemas.compiled", ec))
            {
                g_setenv("GSETTINGS_SCHEMA_DIR", candidate.u8string().c_str(), FALSE);
                return;
            }
        }
}
#else
inline void kosmos_gtk_schemas(const char *) {}
#endif
