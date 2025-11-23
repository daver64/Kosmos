#pragma once

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define KOSMOS_PLATFORM_WINDOWS
#elif defined(__linux__)
    #define KOSMOS_PLATFORM_LINUX
#elif defined(__APPLE__)
    #define KOSMOS_PLATFORM_MACOS
#endif

// DLL export/import
#ifdef KOSMOS_PLATFORM_WINDOWS
    #define KOSMOS_API __declspec(dllexport)
#else
    #define KOSMOS_API __attribute__((visibility("default")))
#endif

// Plugin API version
#define KOSMOS_API_VERSION 1

// Plugin export macro
#ifdef KOSMOS_PLATFORM_WINDOWS
    #define PLUGIN_EXPORT __declspec(dllexport)
#else
    #define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace Kosmos {
    // Forward declarations
    class Application;
    class Vessel;
    class Universe;
    class SolarSystem;
    class CelestialBody;
}
