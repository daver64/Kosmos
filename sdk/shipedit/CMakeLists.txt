cmake_minimum_required(VERSION 3.16)
project(shipedit CXX)

# ShipEdit is a plain GTK 3 program (cairo draws the preview's overlay). It reads and writes ship.json files and does not talk to the running game.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
# The ship's surfaces are drawn in a GtkGLArea (libepoxy comes with GTK 3); without OpenGL at run time the cairo drawing is used.
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

set(SHIPEDIT_JSON_INCLUDE "" CACHE PATH "Folder holding json.hpp (nlohmann)")
set(SHIPEDIT_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin" CACHE PATH "Where the shipedit program is written")

add_executable(shipedit src/main.cpp src/model.cpp src/mesh.cpp src/preview.cpp src/gl_preview.cpp)
target_compile_features(shipedit PRIVATE cxx_std_17)
target_include_directories(shipedit PRIVATE src ${SHIPEDIT_JSON_INCLUDE})
target_link_libraries(shipedit PRIVATE PkgConfig::GTK3 PkgConfig::EPOXY)
set_target_properties(shipedit PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${SHIPEDIT_OUTPUT_DIR})
