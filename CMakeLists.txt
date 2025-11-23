cmake_minimum_required(VERSION 3.20)
project(Kosmos VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(KOSMOS_BUILD_SHARED "Build shared libraries" ON)
option(KOSMOS_BUILD_TESTS "Build tests" ON)
option(KOSMOS_BUILD_PLUGINS "Build example plugins" ON)
option(KOSMOS_ENABLE_PROFILING "Enable profiling support" OFF)

# Platform-specific settings
if(WIN32)
    add_definitions(-DKOSMOS_PLATFORM_WINDOWS)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
elseif(UNIX AND NOT APPLE)
    add_definitions(-DKOSMOS_PLATFORM_LINUX)
elseif(APPLE)
    add_definitions(-DKOSMOS_PLATFORM_MACOS)
endif()

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# SDL2
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

# GLM (header-only)
find_package(glm QUIET)
if(NOT glm_FOUND)
    message(STATUS "GLM not found, will need to be installed manually")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Source files
set(KOSMOS_CORE_SOURCES
    src/Core/Application.cpp
    src/Core/Time.cpp
    src/Core/Config.cpp
    src/Core/Logger.cpp
)

set(KOSMOS_MATH_SOURCES
    src/Math/Quaternion.cpp
    src/Math/StateVector.cpp
    src/Math/CoordinateSystem.cpp
    src/Math/OrbitalElements.cpp
    src/Math/Precision.cpp
)

set(KOSMOS_PHYSICS_SOURCES
    src/Physics/Universe.cpp
    src/Physics/SolarSystem.cpp
    src/Physics/CelestialBody.cpp
    src/Physics/Vessel.cpp
    src/Physics/FuelTank.cpp
    src/Physics/Thruster.cpp
    src/Physics/ThrusterGroup.cpp
    src/Physics/GravityEngine.cpp
    src/Physics/ReferenceFrame.cpp
    src/Physics/Integrator.cpp
    src/Physics/VSOP87.cpp
)

set(KOSMOS_RENDERING_SOURCES
    src/Rendering/Renderer.cpp
    src/Rendering/Camera.cpp
    src/Rendering/Shader.cpp
    src/Rendering/Mesh.cpp
    src/Rendering/Texture.cpp
    src/Rendering/PlanetRenderer.cpp
    src/Rendering/StarField.cpp
    src/Rendering/Atmosphere.cpp
    src/Rendering/HUD.cpp
)

set(KOSMOS_PLUGIN_SOURCES
    src/Plugin/PluginManager.cpp
    src/Plugin/VesselPlugin.cpp
    src/Plugin/PluginAPI.cpp
)

set(KOSMOS_INPUT_SOURCES
    src/Input/InputManager.cpp
    src/Input/Keyboard.cpp
    src/Input/Mouse.cpp
    src/Input/Joystick.cpp
)

set(KOSMOS_AUDIO_SOURCES
    src/Audio/AudioEngine.cpp
    src/Audio/SoundSource.cpp
)

set(KOSMOS_DATA_SOURCES
    src/Data/CatalogLoader.cpp
    src/Data/PlanetData.cpp
    src/Data/AssetManager.cpp
    src/Data/ScenarioLoader.cpp
    src/Data/VesselConfigLoader.cpp
)

set(KOSMOS_PLATFORM_SOURCES
    src/Platform/Window.cpp
    src/Platform/FileSystem.cpp
    src/Platform/DynamicLibrary.cpp
)

# Main executable
add_executable(Kosmos
    src/main.cpp
    ${KOSMOS_CORE_SOURCES}
    ${KOSMOS_MATH_SOURCES}
    ${KOSMOS_PHYSICS_SOURCES}
    ${KOSMOS_RENDERING_SOURCES}
    ${KOSMOS_PLUGIN_SOURCES}
    ${KOSMOS_INPUT_SOURCES}
    ${KOSMOS_AUDIO_SOURCES}
    ${KOSMOS_DATA_SOURCES}
    ${KOSMOS_PLATFORM_SOURCES}
)

target_include_directories(Kosmos PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(Kosmos
    OpenGL::GL
    GLEW::GLEW
    ${SDL2_LIBRARIES}
    ${CMAKE_DL_LIBS}  # For dynamic library loading
)

if(glm_FOUND)
    target_link_libraries(Kosmos glm::glm)
endif()

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(Kosmos pthread)
elseif(WIN32)
    target_link_libraries(Kosmos winmm)
endif()

# Install targets
install(TARGETS Kosmos
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY data/
    DESTINATION share/kosmos/data
)

# Subdirectories
if(KOSMOS_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

if(KOSMOS_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
