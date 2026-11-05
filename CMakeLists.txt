cmake_minimum_required(VERSION 3.28)
project(kosmos LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Ninja otherwise starts (cores + 2) jobs. A job pool caps compiles and links no matter what
# -j says, including a bare `ninja`. Change the cap with -DKOSMOS_BUILD_JOBS=N (the build scripts pass their -j value).
set(KOSMOS_BUILD_JOBS 4 CACHE STRING "Most compile or link jobs run at once (Ninja generator)")
if(KOSMOS_BUILD_JOBS GREATER 4)
    set(KOSMOS_BUILD_JOBS 4) # a hard cap for tooling
endif()
if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS kosmos_jobs=${KOSMOS_BUILD_JOBS})
    set(CMAKE_JOB_POOL_COMPILE kosmos_jobs)
    set(CMAKE_JOB_POOL_LINK kosmos_jobs)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- simlib -------------------------------------------------------------------------------------------------------
# Kosmos is built on simlib (https://github.com/daver64/rsimlib), which must be installed (cmake --install) and found
# through CMAKE_PREFIX_PATH or simlib_DIR; scripts/build-linux.sh and build-windows.sh pass the prefix for you.
# To build against a source checkout instead, pass -DKOSMOS_SIMLIB_DIR=/path/to/rsimlib.
set(KOSMOS_SIMLIB_DIR "" CACHE PATH "Build against this simlib source tree instead of an installed simlib")

if(KOSMOS_SIMLIB_DIR)
    # Keep simlib's static libraries in our build tree rather than inside the source checkout.
    if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    endif()
    include(FetchContent)
    FetchContent_Declare(simlib SOURCE_DIR "${KOSMOS_SIMLIB_DIR}" EXCLUDE_FROM_ALL)
    # Only the library, its dependencies and the shaders are built; simlib's examples and tests are not.
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(simlib)

    # simlib sets these in its own directory scope, so the packaging function is loaded again here.
    set(SIMLIB_PACKAGE_SHADER_DIRS "${simlib_SOURCE_DIR}/shaders;${simlib_BINARY_DIR}/shaders")
    set(SIMLIB_PACKAGE_LICENSE_DIRS "")
    if(WIN32)
        foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
            if(EXISTS "${_prefix}/bin/dxcompiler.dll")
                set(SIMLIB_PACKAGE_DXC_DIR "${_prefix}/bin")
            endif()
        endforeach()
    endif()
    include("${simlib_SOURCE_DIR}/cmake/SimlibPackage.cmake")
    set(KOSMOS_SIMLIB_LIBS simlib imgui lua sol2)
else()
    find_package(simlib CONFIG REQUIRED)
    set(KOSMOS_SIMLIB_LIBS simlib::simlib simlib::imgui simlib::lua sol2)
endif()

# ---- kosmos -------------------------------------------------------------------------------------------------------
add_executable(kosmos
    src/kosmos.cpp
    src/universe.cpp
    src/base.cpp
    src/galaxy.cpp
    src/ship.cpp src/ship_loader.cpp src/ship_placement.cpp src/ship_controls.cpp src/ship_environment.cpp src/ship_publish.cpp
    src/equipment.cpp
    src/ordnance.cpp
    src/camera_dynamics.cpp
    src/weapon_specs.cpp
    src/ship_weapons.cpp
    src/blast_sound.cpp
    src/weapon_sound.cpp
    src/settings.cpp
    src/keybindings.cpp
    src/link/link_server.cpp
    src/link/native_addons.cpp
    src/app_link.cpp
    src/app.cpp
    src/app_start.cpp
    src/app_world_setup.cpp
    src/app_ui_setup.cpp
    src/app_frame.cpp
    src/app_input.cpp
    src/app_camera.cpp
    src/app_draw_world.cpp
    src/app_draw_hud.cpp
    src/app_gui.cpp
    src/app_windows.cpp
    src/app_scene.cpp
    src/app_instruments.cpp
    src/command_line.cpp
    src/multiplayer_ui.cpp
    src/cockpit_panels.cpp
    src/ship_visuals.cpp
    src/body_terrain.cpp
    src/terrain_mesh.cpp
    src/loading_screen.cpp
    src/surface_textures.cpp
    src/reentry.cpp
    src/scenario_start.cpp
    src/procedural.cpp
    src/selftest.cpp
    src/engine_sound.cpp
    src/systems.cpp
    src/part_kit.cpp
    src/blender_edit.cpp
    src/world_texture_jobs.cpp
    src/mfd.cpp
    src/nav.cpp
    src/planet_data.cpp
    src/planet_gen.cpp src/planet_bake.cpp
    src/vsop87.cpp
    src/transfer_plan.cpp
    src/scenario.cpp
    src/sim.cpp
    src/lua_console.cpp
    src/menu.cpp
    src/joystick.cpp
    src/netplay.cpp
    src/weather.cpp
    src/weather_live.cpp
    src/lightning.cpp
    src/landclass.cpp
    src/terrain_clutter.cpp
    src/weather_system.cpp
    src/eva.cpp
    src/eva_solids.cpp
    src/interior.cpp
    src/interior_runtime.cpp
    src/TextEditor.cpp)
target_link_libraries(kosmos PRIVATE ${KOSMOS_SIMLIB_LIBS})
# Live weather downloads: WinHTTP on Windows (always present), libcurl where it is installed, otherwise live weather is off.
find_package(Threads REQUIRED)
target_link_libraries(kosmos PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(kosmos PRIVATE winhttp)
else()
    find_package(CURL QUIET)
    if(CURL_FOUND)
        target_link_libraries(kosmos PRIVATE CURL::libcurl)
        target_compile_definitions(kosmos PRIVATE KOSMOS_HAVE_CURL=1)
    endif()
endif()
if(WIN32)
    set_target_properties(kosmos PROPERTIES WIN32_EXECUTABLE ON) # no console window; see connect_console() in src/kosmos.cpp
endif()
target_include_directories(kosmos PRIVATE src)
# A development build finds its data in the source tree; a packaged build looks beside the executable first.
target_compile_definitions(kosmos PRIVATE
    $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
# VERSION holds major.build: the number the next published build will carry (scripts/publish-web.sh bumps it).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" KOSMOS_VERSION LIMIT_COUNT 1)
target_compile_definitions(kosmos PRIVATE KOSMOS_VERSION="${KOSMOS_VERSION}")
# Shipped in the package so the launcher can tell which build is installed.
file(WRITE "${CMAKE_BINARY_DIR}/VERSION.txt" "${KOSMOS_VERSION}\n")

# Ship systems (power, consumables, pressure vessels) tests: cmake --build build --target systems_test && build/systems_test
add_executable(systems_test EXCLUDE_FROM_ALL tests/systems_test.cpp src/systems.cpp src/ship.cpp src/ship_loader.cpp src/ship_placement.cpp src/ship_controls.cpp src/ship_environment.cpp src/ship_publish.cpp src/reentry.cpp src/interior.cpp src/part_kit.cpp src/universe.cpp
    src/vsop87.cpp src/planet_data.cpp src/planet_gen.cpp src/planet_bake.cpp src/transfer_plan.cpp)
target_link_libraries(systems_test PRIVATE ${KOSMOS_SIMLIB_LIBS})
target_include_directories(systems_test PRIVATE src)
target_compile_definitions(systems_test PRIVATE $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED> KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

# Re-entry heating, hypersonic aerodynamics and parachute drag: cmake --build build --target reentry_test && build/reentry_test
add_executable(reentry_test EXCLUDE_FROM_ALL tests/reentry_test.cpp src/reentry.cpp)
target_link_libraries(reentry_test PRIVATE ${KOSMOS_SIMLIB_LIBS})
target_include_directories(reentry_test PRIVATE src)
target_compile_definitions(reentry_test PRIVATE $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED> KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

# Re-entry plasma look: cmake --build build --target plasma_look_test && build/plasma_look_test
add_executable(plasma_look_test EXCLUDE_FROM_ALL tests/plasma_look_test.cpp)
target_include_directories(plasma_look_test PRIVATE src)

# PNG texture check with libpng, as the game loads them: cmake --build build --target png_check && build/png_check [--fix] [PATH...]
find_package(PNG QUIET)
if(PNG_FOUND)
    add_executable(png_check EXCLUDE_FROM_ALL tests/png_check.cpp)
    target_link_libraries(png_check PRIVATE PNG::PNG ${KOSMOS_SIMLIB_LIBS})
    target_compile_features(png_check PRIVATE cxx_std_17)
endif()

# ShipEdit's document model (no GTK): cmake --build build --target shipedit_model_test && build/shipedit_model_test
add_executable(shipedit_model_test EXCLUDE_FROM_ALL tests/shipedit_model_test.cpp sdk/shipedit/src/model.cpp sdk/shipedit/src/mesh.cpp)
target_include_directories(shipedit_model_test PRIVATE sdk/shipedit/src)
target_link_libraries(shipedit_model_test PRIVATE ${KOSMOS_SIMLIB_LIBS})
target_compile_features(shipedit_model_test PRIVATE cxx_std_17)
target_compile_definitions(shipedit_model_test PRIVATE KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

# The Dynamic Camera's spring model: cmake --build build --target camera_dynamics_test && build/camera_dynamics_test
add_executable(camera_dynamics_test EXCLUDE_FROM_ALL tests/camera_dynamics_test.cpp src/camera_dynamics.cpp)
target_link_libraries(camera_dynamics_test PRIVATE ${KOSMOS_SIMLIB_LIBS})
target_include_directories(camera_dynamics_test PRIVATE src)
target_compile_definitions(camera_dynamics_test PRIVATE $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED> KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(weather_test EXCLUDE_FROM_ALL tests/weather_test.cpp src/weather.cpp)
target_link_libraries(weather_test PRIVATE ${KOSMOS_SIMLIB_LIBS})
target_include_directories(weather_test PRIVATE src)
target_compile_definitions(weather_test PRIVATE $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED> KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

# Equipment loader, blast falloff and the shipped equipment data: cmake --build build --target equipment_test && build/equipment_test
add_executable(equipment_test EXCLUDE_FROM_ALL tests/equipment_test.cpp src/equipment.cpp src/reentry.cpp)
target_link_libraries(equipment_test PRIVATE ${KOSMOS_SIMLIB_LIBS})
target_include_directories(equipment_test PRIVATE src)
target_compile_definitions(equipment_test PRIVATE $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED> KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(joystick_test EXCLUDE_FROM_ALL tests/joystick_test.cpp src/joystick.cpp)
target_link_libraries(joystick_test PRIVATE ${KOSMOS_SIMLIB_LIBS})
target_include_directories(joystick_test PRIVATE src)
target_compile_definitions(joystick_test PRIVATE $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED>)

add_executable(keybindings_test EXCLUDE_FROM_ALL tests/keybindings_test.cpp src/keybindings.cpp)
target_link_libraries(keybindings_test PRIVATE ${KOSMOS_SIMLIB_LIBS})
target_include_directories(keybindings_test PRIVATE src)
target_compile_definitions(keybindings_test PRIVATE $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED>)

# KosmosLink server against the SDK's C client: cmake --build build --target link_test && build/link_test
add_executable(link_test EXCLUDE_FROM_ALL tests/link_test.cpp src/link/link_server.cpp src/link/native_addons.cpp sdk/src/kosmos_link.c)
target_link_libraries(link_test PRIVATE ${KOSMOS_SIMLIB_LIBS} Threads::Threads)
target_include_directories(link_test PRIVATE src src/link sdk/include)
target_compile_definitions(link_test PRIVATE $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED>)

# ---- launcher -----------------------------------------------------------------------------------------------------
# A separate wxWidgets program (no simlib) that picks a scenario and options and starts kosmos; see launcher/main.cpp.
# Needs wxWidgets (Linux: libwxgtk3.2-dev; Windows: vcpkg wxwidgets). Without it the launcher is simply skipped.
option(KOSMOS_BUILD_LAUNCHER "Build the kosmos-launcher front end" ON)
set(KOSMOS_JSON_INCLUDE "")
set(KOSMOS_PACKAGE_EXTRA_TARGETS "")
if(TARGET simlib)
    get_target_property(KOSMOS_JSON_INCLUDE simlib INTERFACE_INCLUDE_DIRECTORIES)
else()
    get_target_property(KOSMOS_JSON_INCLUDE simlib::simlib INTERFACE_INCLUDE_DIRECTORIES)
endif()
add_library(launcher_data STATIC EXCLUDE_FROM_ALL launcher/data.cpp)
target_include_directories(launcher_data PUBLIC launcher PRIVATE ${KOSMOS_JSON_INCLUDE})
add_executable(launcher_test EXCLUDE_FROM_ALL launcher/launcher_test.cpp)
target_link_libraries(launcher_test PRIVATE launcher_data)
target_compile_definitions(launcher_test PRIVATE KOSMOS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(KOSMOS_BUILD_LAUNCHER)
    # wxWidgets 3.3 (vcpkg) ships a CMake config with wx::core targets; the system wxGTK 3.2 is found by FindwxWidgets.
    # The vcpkg find_package wrapper for wxWidgets expects MSVC library names, so a mingw build includes the config directly.
    if(WIN32 AND VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        find_path(KOSMOS_WX_CONFIG_DIR wxWidgetsConfig.cmake PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/wxwidgets" NO_DEFAULT_PATH)
        if(KOSMOS_WX_CONFIG_DIR)
            include("${KOSMOS_WX_CONFIG_DIR}/wxWidgetsConfig.cmake")
        endif()
    else()
        find_package(wxWidgets CONFIG QUIET COMPONENTS core base net)
    endif()
    if(TARGET wx::core)
        set(KOSMOS_WX_LIBS wx::core wx::base wx::net)
    elseif(NOT WIN32)
        find_package(wxWidgets QUIET COMPONENTS core base net)
        if(wxWidgets_FOUND)
            include(${wxWidgets_USE_FILE})
            set(KOSMOS_WX_LIBS ${wxWidgets_LIBRARIES})
        endif()
    endif()
    if(KOSMOS_WX_LIBS)
        add_executable(kosmos-launcher WIN32 launcher/main.cpp launcher/data.cpp $<$<PLATFORM_ID:Windows>:launcher/launcher.rc>)
        target_include_directories(kosmos-launcher PRIVATE launcher ${KOSMOS_JSON_INCLUDE})
        target_link_libraries(kosmos-launcher PRIVATE ${KOSMOS_WX_LIBS})
        if(MSVC)
            target_link_options(kosmos-launcher PRIVATE /MANIFEST:NO)
        elseif(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_link_options(kosmos-launcher PRIVATE -s)
        endif()
        set(KOSMOS_PACKAGE_EXTRA_TARGETS kosmos-launcher)
    else()
        message(STATUS "wxWidgets not found: kosmos-launcher will not be built")
    endif()
endif()

# KosmosMap is a plain GTK 3 program in addons/kosmosmap, built on Linux when GTK 3 and zlib are present.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(KOSMOSMAP_GTK QUIET gtk+-3.0 gdk-pixbuf-2.0)
        find_package(ZLIB QUIET)
        if(KOSMOSMAP_GTK_FOUND AND ZLIB_FOUND)
            set(KOSMOSMAP_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/addons/kosmosmap/bin" CACHE PATH "" FORCE)
            add_subdirectory(sdk/kosmosmap)
            list(APPEND KOSMOS_PACKAGE_EXTRA_TARGETS kosmosmap)
        else()
            message(STATUS "GTK 3 or zlib not found: KosmosMap will not be built")
        endif()
    endif()
endif()
# KosmosCAD's modelling core (no GTK, no GL): cmake --build build --target kosmoscad_test && build/kosmoscad_test
add_executable(kosmoscad_test EXCLUDE_FROM_ALL tests/kosmoscad_test.cpp sdk/kosmoscad/src/mesh.cpp sdk/kosmoscad/src/csg.cpp sdk/kosmoscad/src/scene.cpp
    sdk/kosmoscad/src/equipment.cpp sdk/kosmoscad/src/ship_io.cpp sdk/kosmoscad/src/editor.cpp sdk/kosmoscad/src/image.cpp sdk/kosmoscad/src/mesh_io.cpp)
target_include_directories(kosmoscad_test PRIVATE sdk/kosmoscad/src ${KOSMOS_JSON_INCLUDE})
target_compile_features(kosmoscad_test PRIVATE cxx_std_17)
target_compile_definitions(kosmoscad_test PRIVATE KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
# KosmosMission's planning core (no GTK, no GL): cmake --build build --target missioncore_test && build/missioncore_test
set(KOSMOSMISSION_CORE sdk/kosmosmission/core/orbit.cpp sdk/kosmosmission/core/bodies.cpp sdk/kosmosmission/core/transfer.cpp
    sdk/kosmosmission/core/budget.cpp sdk/kosmosmission/core/payload.cpp sdk/kosmosmission/core/mission.cpp)
add_executable(missioncore_test EXCLUDE_FROM_ALL tests/missioncore_test.cpp ${KOSMOSMISSION_CORE})
target_include_directories(missioncore_test PRIVATE sdk/kosmosmission/core ${KOSMOS_JSON_INCLUDE})
target_compile_features(missioncore_test PRIVATE cxx_std_17)
target_compile_definitions(missioncore_test PRIVATE KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
# ShipEdit, the ship and stack editor (sdk/shipedit), is a GTK 3 program too; its tests need no GTK (shipedit_model_test above).
if(UNIX AND NOT APPLE AND KOSMOSMAP_GTK_FOUND)
    set(SHIPEDIT_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/addons/shipedit/bin" CACHE PATH "" FORCE)
    set(SHIPEDIT_JSON_INCLUDE "${KOSMOS_JSON_INCLUDE}" CACHE PATH "" FORCE)
    add_subdirectory(sdk/shipedit)
    list(APPEND KOSMOS_PACKAGE_EXTRA_TARGETS shipedit)
    # KosmosCAD (sdk/kosmoscad), the 3D modeller ShipEdit launches, needs GTK 3 and OpenGL 3.2.
    set(KOSMOSCAD_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/addons/kosmoscad/bin" CACHE PATH "" FORCE)
    set(KOSMOSCAD_JSON_INCLUDE "${KOSMOS_JSON_INCLUDE}" CACHE PATH "" FORCE)
    add_subdirectory(sdk/kosmoscad)
    list(APPEND KOSMOS_PACKAGE_EXTRA_TARGETS kosmoscad)
    # KosmosMission (sdk/kosmosmission), the mission planner, needs GTK 3 and OpenGL 3.2 too (Linux only for now).
    set(KOSMOSMISSION_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/addons/kosmosmission/bin" CACHE PATH "" FORCE)
    set(KOSMOSMISSION_JSON_INCLUDE "${KOSMOS_JSON_INCLUDE}" CACHE PATH "" FORCE)
    add_subdirectory(sdk/kosmosmission)
    list(APPEND KOSMOS_PACKAGE_EXTRA_TARGETS kosmosmission)
endif()
# KosmosAV, the pilot's program for the KosmosAV freight network (kav/client), is a GTK 3 program too and also needs libcurl (the Windows program is built by scripts/build-win-tools.sh with WinHTTP).
if(UNIX AND NOT APPLE AND KOSMOSMAP_GTK_FOUND)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        set(KOSMOSAV_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/addons/kosmosav/bin" CACHE PATH "" FORCE)
        add_subdirectory(kav/client)
        list(APPEND KOSMOS_PACKAGE_EXTRA_TARGETS kosmosav)
    else()
        message(STATUS "libcurl not found: KosmosAV will not be built")
    endif()
endif()
# planetbake, the world texture baker, sits next to KosmosMap in addons/planetbake/bin (Linux and Windows). It needs zlib, glm and
# FastNoiseLite but not simlib; it gets a preview window where GTK 3 is found (Linux; the Windows one comes from scripts/build-win-tools.sh).
set(PLANETBAKE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/addons/planetbake/bin" CACHE PATH "" FORCE)
# build-windows.sh builds the GTK window versions of both programs first (scripts/build-win-tools.sh) and passes KOSMOS_PREBUILT_WIN_TOOLS,
# so the Windows build neither replaces planetbake.exe with the command line one nor needs GTK in CMake.
option(KOSMOS_PREBUILT_WIN_TOOLS "Windows: ship the planetbake.exe, kosmosmap.exe and kosmosav.exe already built into addons" OFF)
if(NOT (WIN32 AND KOSMOS_PREBUILT_WIN_TOOLS))
    add_subdirectory(sdk/planetbake)
endif()
if(TARGET planetbake)
    list(APPEND KOSMOS_PACKAGE_EXTRA_TARGETS planetbake)
else()
    message(STATUS "planetbake will not be built (needs zlib, glm and FastNoiseLite.h)")
endif()
set(KOSMOS_PACKAGE_EXCLUDE "/data/textures-hires(/|$)" "/import(/|$)"
    "/data/landclass/.*\\.kavlc$" "\\.blend1?$" "/addons/cvel-piper-cub/textures(/|$)")
# Both platforms build into the same source folder, so each package leaves out the other's planetbake.
if(WIN32)
    list(APPEND KOSMOS_PACKAGE_EXCLUDE "/addons/kosmosmap/bin/kosmosmap$" "/addons/kosmosav/bin/kosmosav$" "/addons/shipedit/bin/shipedit$" "/addons/kosmoscad/bin/kosmoscad$" "/addons/kosmosmission/bin/kosmosmission$" "/addons/planetbake/bin/planetbake$")
else()
    list(APPEND KOSMOS_PACKAGE_EXCLUDE "/addons/planetbake/bin/planetbake\\.exe$" "/addons/kosmosmap/bin/kosmosmap\\.exe$" "/addons/kosmosav/bin/kosmosav\\.exe$"
        "/addons/shipedit/bin/shipedit\\.exe$" "/addons/kosmoscad/bin/kosmoscad\\.exe$" "/addons/kosmosmission/bin/kosmosmission\\.exe$" "/addons/(planetbake|kosmosmap|kosmosav|shipedit|kosmoscad|kosmosmission)/share(/|$)")
endif()

# ---- distribution -------------------------------------------------------------------------------------------------
# cmake --build build --target kosmos_package
#   Linux:   build/package/kosmos-beta-<version>-Linux.tar.gz        Windows: build/package/kosmos-beta-<version>-Windows.zip
# Hi-res textures (data/textures-hires, import/celestia/hires) and the 54 GB Orbiter import (import/) are left out; scripts/pack-hires.sh
# builds the separate hi-res pack. The Earth land cover ships only as the gzipped cache (scripts/pack-landclass.sh makes
# it); the raw rasters and the unpacked cache stay out, and scripts/pack-devdata.sh bundles them for developers.
simlib_add_package(TARGET kosmos NAME kosmos-beta-${KOSMOS_VERSION}-${CMAKE_SYSTEM_NAME}
    ASSETS data scenarios addons assets
    EXTRA_FILES README.md "${CMAKE_BINARY_DIR}/VERSION.txt"
    EXTRA_TARGETS ${KOSMOS_PACKAGE_EXTRA_TARGETS}
    EXCLUDE ${KOSMOS_PACKAGE_EXCLUDE})
