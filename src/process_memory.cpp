#include "process_memory.h"

#include <cstdio>

#if defined(_WIN32)
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace kosmos
{
    std::optional<double> process_resident_mib(const char *linux_statm_path)
    {
#if defined(_WIN32)
        (void)linux_statm_path;
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            return std::nullopt;
        return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
#elif defined(__linux__)
        std::FILE *file = std::fopen(linux_statm_path, "r");
        if (!file)
            return std::nullopt;
        unsigned long pages = 0, resident = 0;
        const int parsed = std::fscanf(file, "%lu %lu", &pages, &resident);
        std::fclose(file);
        const long page_size = sysconf(_SC_PAGESIZE);
        if (parsed != 2 || page_size <= 0)
            return std::nullopt;
        return static_cast<double>(resident) * static_cast<double>(page_size) / (1024.0 * 1024.0);
#else
        (void)linux_statm_path;
        return std::nullopt;
#endif
    }
}
