cmake_minimum_required(VERSION 3.16)
project(kosmosmap CXX)

# KosmosMap is a plain GTK 3 program: it needs GTK 3 and zlib, not simlib. It talks to the game through the SDK's client library.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0 gdk-pixbuf-2.0)
find_package(ZLIB REQUIRED)

set(KOSMOS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Kosmos SDK folder")
if(NOT TARGET kosmos::link)
    add_subdirectory(${KOSMOS_SDK_DIR} kosmos-sdk EXCLUDE_FROM_ALL)
endif()

# Inside the game's tree the program goes to addons/kosmosmap/bin, next to the manifest the game reads.
set(KOSMOSMAP_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin" CACHE PATH "Where the kosmosmap program is written")

add_executable(kosmosmap src/main.cpp src/render.cpp src/terrain.cpp)
target_compile_features(kosmosmap PRIVATE cxx_std_17)
target_link_libraries(kosmosmap PRIVATE kosmos::link PkgConfig::GTK3 ZLIB::ZLIB pthread)
set_target_properties(kosmosmap PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${KOSMOSMAP_OUTPUT_DIR})
