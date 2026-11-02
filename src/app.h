#pragma once

// Shared declarations for the Kosmos application (kosmos.cpp and the files split out of it).

#include "sl.h"
#include "app_common.h"
#include "options.h"
#include "props3d.h"
#include "flight.h"
#include "example_fx.h"
#include "sim.h"
#include "lightning.h"
#include "lua_console.h"
#include "joystick.h"
#include "menu.h"
#include "netplay.h"
#include "planet_data.h"
#include "terrain_clutter.h"
#include "planet_bake.h"
#include "blender_edit.h"
#include "usercam.h"
#include "eva.h"
#include "eva_solids.h"
#include "ordnance.h"
#include "settings.h"
#include "engine_sound.h"
#include "procedural.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace kosmos::app
{
    // The part being edited in Blender (the part editor window).
    struct PartEdit
    {
        kosmos::PartSpec *part = nullptr;
        kosmos::PartSpec original;
        bool converted = false;
        std::filesystem::path file;
        int stage = 0, index = 0;
        std::string model;
    };
} // namespace kosmos::app
