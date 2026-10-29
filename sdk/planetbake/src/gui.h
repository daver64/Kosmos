#pragma once

#include "bake.h"

/** Opens the preview window with the job's settings as the starting point, and a pack to work on when `pack_dir` is not empty. Returns the program's exit code (1 if built without GTK). */
int run_gui(const planetbake::Job &initial, const std::string &pack_dir = std::string());
