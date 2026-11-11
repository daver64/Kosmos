#pragma once

#include <optional>

namespace kosmos
{
    std::optional<double> process_resident_mib(const char *linux_statm_path = "/proc/self/statm");
}
