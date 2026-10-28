#pragma once

#include "app.h"

namespace kosmos::app
{
    // A packaged build keeps data/ beside the executable; a development build falls back to the source tree.
    std::string default_root(const char *argv0);

    // Reads kosmos's own options into `options`; anything else is left in `backend_args` for simlib.
    // Returns keep_going to carry on, or the exit code when the run is finished (--version, an error, a developer tool).
    int parse_command_line(int argc, char **argv, Options &options, std::vector<char *> &backend_args);
} // namespace kosmos::app
