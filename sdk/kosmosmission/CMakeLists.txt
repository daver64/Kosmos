cmake_minimum_required(VERSION 3.16)
project(kosmosmission CXX)

# KosmosMission is a GTK 3 mission planner whose views are OpenGL (a GtkGLArea). The planning core (core/) has no GTK or GL in it:
# see tests/missioncore_test.cpp. The program talks to the game, optionally, through the SDK's client library.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

set(KOSMOS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Kosmos SDK folder")
if(NOT TARGET kosmos::link)
    add_subdirectory(${KOSMOS_SDK_DIR} kosmos-sdk EXCLUDE_FROM_ALL)
endif()

set(KOSMOSMISSION_JSON_INCLUDE "" CACHE PATH "Folder holding json.hpp (nlohmann)")
set(KOSMOSMISSION_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin" CACHE PATH "Where the kosmosmission program is written")

add_executable(kosmosmission src/main.cpp src/gl_view.cpp core/orbit.cpp core/bodies.cpp core/transfer.cpp core/budget.cpp core/payload.cpp core/mission.cpp)
target_compile_features(kosmosmission PRIVATE cxx_std_17)
target_include_directories(kosmosmission PRIVATE src core ${KOSMOSMISSION_JSON_INCLUDE})
target_link_libraries(kosmosmission PRIVATE kosmos::link PkgConfig::GTK3 PkgConfig::EPOXY)
set_target_properties(kosmosmission PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${KOSMOSMISSION_OUTPUT_DIR})
