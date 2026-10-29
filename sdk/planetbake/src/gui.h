#pragma once

#include "bake.h"

/** Opens the preview window with the job's settings as the starting point. Returns the program's exit code (1 if built without GTK). */
int run_gui(const planetbake::Job &initial);
