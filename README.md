# Kosmos Space Flight Simulator

A cross-platform space flight simulator supporting realistic physics across hundreds of solar systems.

## Features

- **Realistic Physics**: N-body gravity simulation with VSOP87 for Sol system
- **Multi-System Support**: Explore hundreds of nearby star systems
- **Advanced Propulsion**: Detailed fuel tank and thruster system modeling
- **Plugin Architecture**: Extensible vessel system via DLL/.so plugins
- **Cross-Platform**: Windows, Linux, and macOS support

## Building

### Prerequisites

- CMake 3.20 or higher
- C++20 compatible compiler (GCC 10+, Clang 11+, MSVC 2019+)
- SDL3
- OpenGL 4.x
- GLM (OpenGL Mathematics)

### Linux

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Windows

```bash
mkdir build
cd build
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release
```

### macOS

```bash
mkdir build
cd build
cmake -G "Xcode" ..
cmake --build . --config Release
```

## Dependencies Installation

### Ubuntu/Debian

```bash
sudo apt-get install libsdl3-dev libglm-dev libgl1-mesa-dev
```

### Fedora

```bash
sudo dnf install SDL3-devel glm-devel mesa-libGL-devel
```

### macOS (Homebrew)

```bash
brew install sdl3 glm
```

### Windows

Download and install:
- SDL3: https://github.com/libsdl-org/SDL
- GLM: https://github.com/g-truc/glm

## Project Structure

```
Kosmos/
├── src/                    # Source files
│   ├── Core/              # Core engine systems
│   ├── Math/              # Mathematical utilities
│   ├── Physics/           # Physics simulation
│   ├── Rendering/         # Graphics subsystem
│   ├── Plugin/            # Plugin system
│   ├── Input/             # Input handling
│   ├── Audio/             # Audio system
│   ├── Data/              # Data loading
│   └── Platform/          # Platform abstraction
├── include/               # Public headers
├── plugins/               # Plugin modules
│   └── vessels/          # Vessel plugins
├── data/                  # Game data
│   ├── catalogs/         # Star catalogs
│   ├── textures/         # Texture files
│   ├── models/           # 3D models
│   ├── shaders/          # GLSL shaders
│   ├── scenarios/        # Mission scenarios
│   └── vsop87/           # VSOP87 data files
├── docs/                  # Documentation
├── tests/                 # Unit tests
└── external/              # Third-party libraries
```

## Creating Vessel Plugins

See `docs/PluginDevelopment.md` for details on creating custom vessels.

Example minimal vessel plugin:

```cpp
#include "Kosmos/PluginAPI.h"

extern "C" {
    PLUGIN_EXPORT const char* GetPluginName() {
        return "My Vessel";
    }
    
    PLUGIN_EXPORT int GetAPIVersion() {
        return KOSMOS_API_VERSION;
    }
    
    PLUGIN_EXPORT void* CreateVessel(void* engineInterface) {
        // Create and configure vessel
        return vessel;
    }
}
```

## License

MIT License - See LICENSE file for details

## Contributing

Contributions welcome! Please read CONTRIBUTING.md for guidelines.

## Documentation

- [Architecture Overview](docs/Architecture.md)
- [Plugin Development Guide](docs/PluginDevelopment.md)
- [Physics System](docs/Physics.md)
- [Rendering System](docs/Rendering.md)

## Credits

Inspired by Orbiter Space Flight Simulator and Kerbal Space Program.
