cmake_minimum_required(VERSION 3.16)
project(kosmosav CXX)

# The KosmosAV program: a plain GTK 3 program that watches the game over KosmosLink and talks to the KosmosAV server with libcurl (WinHTTP on Windows).
# test_agent is the same logic without a window, played against a fake game.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
if(NOT WIN32)
    find_package(CURL REQUIRED)
endif()

set(KOSMOS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" CACHE PATH "Kosmos SDK folder")
if(NOT TARGET kosmos::link)
    add_subdirectory(${KOSMOS_SDK_DIR} kosmos-sdk EXCLUDE_FROM_ALL)
endif()

set(KOSMOSAV_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin" CACHE PATH "Where the kosmosav program is written")

add_library(kosmosav_core STATIC src/agent.cpp src/http.cpp)
target_compile_features(kosmosav_core PUBLIC cxx_std_17)
target_include_directories(kosmosav_core PUBLIC src)
if(WIN32)
    target_link_libraries(kosmosav_core PUBLIC winhttp)
else()
    target_link_libraries(kosmosav_core PUBLIC CURL::libcurl)
endif()

add_executable(kosmosav src/main.cpp)
target_link_libraries(kosmosav PRIVATE kosmosav_core kosmos::link PkgConfig::GTK3 pthread)
set_target_properties(kosmosav PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${KOSMOSAV_OUTPUT_DIR})

add_executable(test_agent EXCLUDE_FROM_ALL src/test_agent.cpp)
target_link_libraries(test_agent PRIVATE kosmosav_core)
set_target_properties(test_agent PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
