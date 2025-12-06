# Kosmos Space Flight Simulator

![Luna3](images/Luna3.jpg)

A cross-platform space flight simulator with realistic physics and high-precision orbital mechanics for exploring our solar system and beyond.

## Features

- **High-Precision Physics**: 64-bit double precision throughout for realistic solar system scale accuracy
- **VSOP87D Ephemerides**: Accurate planetary positions for all 8 planets plus the Moon
- **Orbiter-Style Camera System**: Multiple camera modes (cockpit, external, track, free) with smooth transitions
- **Camera-Relative Rendering**: Precision rendering at astronomical scales without floating-point jitter
- **Planet Textures**: Support for Celestia-format equirectangular texture maps
- **Atmospheric Rendering**: Physically-based atmosphere with Fresnel effects, density falloff, and specular highlights
- **Realistic Lighting**: Physically-based lighting with proper calculations
- **Milky Way Background**: Procedural 4K starfield rendering
- **Plugin Architecture**: Extensible vessel system via DLL/.so plugins
- **Dynamic Clipping Planes**: Separate near/far planes for vessels and planets for optimal depth precision
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
sudo apt-get install libsdl2-dev libglm-dev libglew-dev libgl1-mesa-dev
```

### Fedora

```bash
sudo dnf install SDL2-devel glm-devel glew-devel mesa-libGL-devel
```

### macOS (Homebrew)

```bash
brew install sdl2 glm glew
```

### Windows

Download and install:
- SDL2: https://www.libsdl.org/
- GLM: https://github.com/g-truc/glm
- GLEW: http://glew.sourceforge.net/

## Downloading Data Files

Most essential data files are **already included in the repository**. Optional high-resolution alternatives can be downloaded as described below.

### VSOP87 Planetary Data (✅ Included)
**Already in repo**: All VSOP87D files for 8 planets are included in `data/vsop87/`.

**Source**: IMCCE (Institut de Mécanique Céleste et de Calcul des Éphémérides)
- ftp://ftp.imcce.fr/pub/ephem/planets/vsop87/

The included VSOP87D files provide heliocentric spherical coordinates with sub-arcsecond accuracy for planetary positions.

### Planet Textures (✅ Included)
**Already in repo**: 2K resolution textures for all planets in `data/textures/planets/`:
- Sun, Mercury, Venus, Earth, Moon, Mars, Jupiter, Saturn, Uranus, Neptune

**Source**: Solar System Scope (https://www.solarsystemscope.com/textures/)
- License: Free for personal and educational use
- Format: 2048×1024 JPG (equirectangular, 2:1 aspect ratio)

**Optional**: Higher resolution 4K/8K textures can be downloaded using the script:
```bash
cd data/textures/planets
./download_textures.sh
```

See `data/textures/planets/README.md` for alternative sources (Celestia Motherlode, NASA Blue Marble, JHT's Planetary Pixel Emporium).

### Milky Way Skybox (✅ Included)
**Already in repo**: 4K procedural Milky Way background in `data/textures/skybox/milkyway_procedural_4k.jpg`

**Optional**: ESA Gaia DR2 all-sky map (8K resolution, ~50 MB):
```bash
cd data/textures/skybox
wget https://www.cosmos.esa.int/documents/29201/1769576/GaiaDR2_FluxRGB_Cartesian_8192x4096.png \
     -O milky_way_gaia_8k.png
```

### Star Catalogs (✅ Minimal Included)
**Already in repo**: Basic test catalog with nearest stars in `data/catalogs/nearest_stars.txt`

**Optional**: Full Hipparcos catalog (~50 MB, ~118K stars):
```bash
cd data/catalogs
wget https://cdsarc.cds.unistra.fr/ftp/cats/I/311/hip2.dat.gz
gunzip hip2.dat.gz
```

See `data/catalogs/README.md` for complete catalog options and formats.

## Quick Start

The simulator is ready to run with included assets! Simply build and launch:

```bash
cd build
cmake ..
make -j$(nproc)
cd ..
./Kosmos
```

All essential files (VSOP87 data, planet textures, skybox) are already included.

## Controls

### Camera Modes (Orbiter-Style)
- **F1**: Cockpit camera (internal view from vessel)
- **F2**: External camera (third-person orbiting around vessel)
- **F3**: Track camera (chase/follow mode)
- **F4**: Free camera (unconstrained 6DOF flight, detached from vessel)

**Note for ASUS TUF Gaming Laptops**: If F-keys trigger multimedia functions, press **Fn + F11** to toggle Fn Lock and enable function keys.

### Camera Control
- **Arrow Keys**: Pitch (up/down) and Yaw (left/right)
- **Q / E**: Roll camera left/right
- **W / A / S / D**: Move camera in free mode (forward/left/back/right)
- **Space**: Move up (free camera)
- **Left Ctrl**: Move down (free camera)
- **Page Up / Page Down**: Zoom in/out in external camera

### Vessel & Camera Selection
- **V**: Attach/detach camera to vessel
- **C**: Cycle through cockpit cameras (if vessel has multiple)
- **X**: Cycle through external camera positions

### Navigation & Focus
- **,** (Comma): Cycle to previous celestial body
- **.** (Period): Cycle to next celestial body
  - Order: Sol → Mercury → Venus → Earth → Moon → Mars → Jupiter → Saturn → Uranus → Neptune

### Time Control
- **1**: Decrease time acceleration
- **2**: Increase time acceleration

### General
- **ESC**: Exit simulation

## Current Implementation

### Vessels
- **LunaE3 Probe**: Orbital space probe with basic propulsion system (plugin-based)
  - Full 3D mesh with DDS textures
  - Plugin architecture allows dynamic loading without recompilation

### Solar System Bodies
The simulation includes the following celestial bodies with accurate VSOP87D positions:
- **Sun** (Sol): Self-luminous star at the center (696,000 km radius)
- **8 Planets**: Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune
  - Accurate sizes and orbital positions
  - Texture-mapped surfaces (2K equirectangular)
  - Atmospheric rendering for Earth, Mars, Venus, and gas giants
- **Moon**: Earth's natural satellite with realistic orbital motion (1,737 km radius)

All bodies are rendered at their actual physical sizes with proper lighting and can be viewed from any perspective.

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

Vessels are implemented as dynamically-loaded plugins (.so on Linux, .dll on Windows). See `docs/PluginDevelopment.md` for complete details.

Example minimal vessel plugin (see `plugins/vessels/LunaE3.cpp`):

```cpp
#include "Kosmos/PluginAPI.h"

extern "C" {
    PLUGIN_EXPORT const char* GetPluginName() {
        return "LunaE3";
    }
    
    PLUGIN_EXPORT int GetAPIVersion() {
        return KOSMOS_API_VERSION;
    }
    
    PLUGIN_EXPORT void* CreateVessel(void* engineInterface) {
        // Create and configure vessel
        LunaE3Data* vessel = new LunaE3Data();
        vessel->fuel = 1.0;
        vessel->fuelCapacity = 500.0;  // 500 kg
        return vessel;
    }
    
    PLUGIN_EXPORT void DestroyVessel(void* vessel) {
        delete static_cast<LunaE3Data*>(vessel);
    }
    
    PLUGIN_EXPORT void Vessel_Update(void* vessel, double deltaTime) {
        // Update vessel state (fuel consumption, etc.)
    }
}
```

Plugins are loaded from `lib/` directory and mesh data from `data/plugins/vessels/`.

## License

MIT License - See LICENSE file for details

## Contributing

Contributions welcome! Please read CONTRIBUTING.md for guidelines.

## Documentation

- [Architecture Overview](docs/Architecture.md)
- [VSOP87 Implementation](docs/VSOP87_IMPLEMENTATION.md)
- [Starfield Rendering](docs/STARFIELD_RENDERING.md)
- [Plugin Development Guide](docs/PluginDevelopment.md)

## Technical Highlights

- **64-bit Precision**: All calculations use double precision until the final GPU conversion
- **Camera-Relative Rendering**: Objects positioned relative to camera to avoid float precision loss at astronomical scales
- **Dynamic Clipping Planes**: Separate near/far planes for vessels (1m-100km) and planets (dynamic based on distance)
- **Rotation-Only View Matrix**: Camera view matrix built without position data to prevent precision loss at 146M+ km scales
- **Smooth Orbital Motion**: VSOP87D-based planetary positions with interpolation for jitter-free movement
- **Depth Management**: Smart depth testing for astronomical scales (vessels vs celestial bodies vs skybox)
- **LOD System**: Planet mesh detail adjusts based on viewing distance (128×64 segments close, 64×32 segments far)
- **Atmospheric Scattering**: Fresnel effects with exponential density falloff, subsurface scattering, and specular highlights

## Credits

Inspired by Orbiter Space Flight Simulator and Kerbal Space Program.
