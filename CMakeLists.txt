cmake_minimum_required(VERSION 3.28)
project(kosmos LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
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
    src/ship.cpp
    src/mfd.cpp
    src/nav.cpp
    src/planet_data.cpp
    src/vsop87.cpp
    src/scenario.cpp
    src/sim.cpp
    src/lua_console.cpp
    src/menu.cpp
    src/netplay.cpp
    src/TextEditor.cpp)
target_link_libraries(kosmos PRIVATE ${KOSMOS_SIMLIB_LIBS})
target_include_directories(kosmos PRIVATE src)
# A development build finds its data in the source tree; a packaged build looks beside the executable first.
target_compile_definitions(kosmos PRIVATE $<$<PLATFORM_ID:Windows>:SDL_MAIN_HANDLED> KOSMOS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
# VERSION holds major.build: the number the next published build will carry (scripts/publish-web.sh bumps it).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" KOSMOS_VERSION LIMIT_COUNT 1)
target_compile_definitions(kosmos PRIVATE KOSMOS_VERSION="${KOSMOS_VERSION}")

# ---- distribution -------------------------------------------------------------------------------------------------
# cmake --build build --target kosmos_package
#   Linux:   build/package/kosmos-beta-Linux.tar.gz        Windows: build/package/kosmos-beta-Windows.zip
# data/orbiter (the user's own Orbiter high-resolution tiles) and data/textures-hires (optional high-resolution maps, not in the repository)
# are never shipped, and only the monospace font of assets/.
simlib_add_package(TARGET kosmos NAME kosmos-beta-${CMAKE_SYSTEM_NAME}
    ASSETS data addons assets
    EXTRA_FILES README.md
    EXCLUDE "/data/orbiter(/|$)" "/data/textures-hires(/|$)")
