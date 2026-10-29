cmake_minimum_required(VERSION 3.16)
project(planetbake CXX)

# planetbake needs zlib, glm (headers), FastNoiseLite and the game's world generator (planet_gen). It does not need simlib.
# In the SDK the generator is in gen/; in the game's tree it is the game's own src/.
find_package(ZLIB QUIET)
find_package(Threads QUIET)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gen/planet_gen.cpp")
    set(PB_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gen")
else()
    set(PB_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
endif()

find_path(PB_GLM_DIR glm/glm.hpp HINTS ${GLM_INCLUDE_DIR} $ENV{GLM_INCLUDE_DIR} "$ENV{HOME}/opt/include")
find_path(PB_NOISE_DIR FastNoiseLite.h HINTS ${PB_GEN_DIR} "$ENV{HOME}/opt/include" "$ENV{HOME}/Documents/Projects/rsimlib/include" ${FASTNOISE_DIR})
if(NOT ZLIB_FOUND OR NOT PB_GLM_DIR OR NOT PB_NOISE_DIR OR NOT EXISTS "${PB_GEN_DIR}/planet_gen.cpp")
    set(PB_MESSAGE "planetbake needs zlib, glm (glm/glm.hpp) and FastNoiseLite.h; set GLM_INCLUDE_DIR / FASTNOISE_DIR")
    if(PROJECT_IS_TOP_LEVEL)
        message(FATAL_ERROR "${PB_MESSAGE}")
    endif()
    message(STATUS "planetbake skipped: ${PB_MESSAGE}")
    return()
endif()

set(PB_VERSION dev)
foreach(candidate ../VERSION ../../VERSION)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${candidate}")
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${candidate}" PB_VERSION LIMIT_COUNT 1)
        break()
    endif()
endforeach()

set(PLANETBAKE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin" CACHE PATH "Where the planetbake program is written")

add_executable(planetbake src/main.cpp src/bake.cpp src/layers.cpp src/encode.cpp src/imageio.cpp src/pack.cpp src/settings.cpp src/fill.cpp src/edit.cpp src/gui.cpp ${PB_GEN_DIR}/planet_gen.cpp)
target_compile_features(planetbake PRIVATE cxx_std_17)
# The compat headers stand in for the few simlib types planet_gen uses; they must come before anything else.
target_include_directories(planetbake PRIVATE compat src ${PB_GEN_DIR})
# Never add the host's /usr/include by hand: it breaks cross builds (a compiler already searches its own system folders).
foreach(dir ${PB_GLM_DIR} ${PB_NOISE_DIR})
    if(NOT dir MATCHES "^/usr(/local)?/include$")
        target_include_directories(planetbake SYSTEM PRIVATE ${dir})
    endif()
endforeach()
target_compile_definitions(planetbake PRIVATE PLANETBAKE_VERSION="${PB_VERSION}")
target_link_libraries(planetbake PRIVATE ZLIB::ZLIB Threads::Threads)

# planetbake builds on Windows too (command line only there); the preview window is optional: without GTK 3 the program is command line only.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT PLANETBAKE_NO_GUI AND UNIX AND NOT CMAKE_CROSSCOMPILING)
    pkg_check_modules(PB_GTK QUIET IMPORTED_TARGET gtk+-3.0)
    if(PB_GTK_FOUND)
        target_compile_definitions(planetbake PRIVATE PLANETBAKE_GTK=1)
        target_link_libraries(planetbake PRIVATE PkgConfig::PB_GTK)
    endif()
endif()
set_target_properties(planetbake PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PLANETBAKE_OUTPUT_DIR})
