cmake_minimum_required(VERSION 3.16)
project(kosmoscad CXX)

# KosmosCAD is a GTK 3 program whose four views are all OpenGL (a GtkGLArea, libepoxy comes with GTK 3); nothing is drawn with cairo.
# The modelling core (mesh, csg, scene, editor, ship_io) has no GTK or GL in it: see tests/kosmoscad_test.cpp.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

set(KOSMOSCAD_JSON_INCLUDE "" CACHE PATH "Folder holding json.hpp (nlohmann)")
set(KOSMOSCAD_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin" CACHE PATH "Where the kosmoscad program is written")

add_executable(kosmoscad src/main.cpp src/gl_view.cpp src/editor.cpp src/mesh.cpp src/csg.cpp src/scene.cpp src/equipment.cpp src/ship_io.cpp src/image.cpp src/mesh_io.cpp)
target_compile_features(kosmoscad PRIVATE cxx_std_17)
target_include_directories(kosmoscad PRIVATE src ${KOSMOSCAD_JSON_INCLUDE})
target_link_libraries(kosmoscad PRIVATE PkgConfig::GTK3 PkgConfig::EPOXY)
set_target_properties(kosmoscad PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${KOSMOSCAD_OUTPUT_DIR})
