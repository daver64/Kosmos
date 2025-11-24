# Kosmos Space Flight Simulator

![Luna3](images/Luna3.jpg)

A cross-platform space flight simulator with realistic physics and high-precision orbital mechanics for exploring our solar system and beyond.

## Features

- **High-Precision Physics**: 64-bit double precision throughout for realistic solar system scale accuracy
- **VSOP87D Ephemerides**: Accurate planetary positions for all 8 planets plus the Moon
- **6DOF Camera System**: Free-flight camera with full 6 degrees of freedom (no gimbal lock)
- **Camera-Relative Rendering**: Precision rendering at astronomical scales without floating-point jitter
- **Realistic Lighting**: Physically-based lighting with proper shadow calculations
- **Milky Way Background**: Beautiful procedural or Gaia-based starfield rendering
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

After building, download essential data files:

### VSOP87 Planetary Data (Required)
```bash
cd data/vsop87
wget -q ftp://ftp.imcce.fr/pub/ephem/planets/vsop87/VSOP87D.{mer,ven,ear,mar,jup,sat,ura,nep}
```

### Star Catalogs (Recommended)
```bash
cd data/catalogs
# Quick start: Create minimal test catalog
cat > nearest_stars.txt << 'EOF'
# Name,Distance(ly),RA(deg),Dec(deg),Magnitude,SpectralType
Sol,0.0,0.0,0.0,-26.74,G2V
Proxima Centauri,4.24,217.42894,-62.67953,11.13,M5.5Ve
Alpha Centauri A,4.37,219.90205,-60.83399,-0.01,G2V
Sirius A,8.66,101.28715,-16.71611,-1.46,A1V
EOF

# Full Hipparcos catalog (~50 MB, ~118K stars)
# wget https://cdsarc.cds.unistra.fr/ftp/cats/I/311/hip2.dat.gz
# gunzip hip2.dat.gz
```

See `data/catalogs/README.md` for complete catalog options.

### Milky Way Skybox (Recommended)
```bash
cd data/textures
mkdir -p skybox
cd skybox

# ESA Gaia DR2 all-sky map (8K resolution)
wget https://www.cosmos.esa.int/documents/29201/1769576/GaiaDR2_FluxRGB_Cartesian_8192x4096.png \
     -O milky_way_gaia_8k.png
```

### Planet Textures (Optional)
```bash
cd data/textures/celestial/Sol/planets

# Earth - NASA Blue Marble
wget https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg \
     -O Earth_color.jpg

# Mars
wget https://www.solarsystemscope.com/textures/download/2k_mars.jpg -O Mars_color.jpg

# See data/textures/TEXTURE_SOURCES.md for Celestia and Orbiter texture packs
```

For complete download instructions:
- **Star Catalogs**: `data/catalogs/README.md`
- **Textures**: `data/textures/TEXTURE_SOURCES.md` (Celestia, Orbiter, NASA sources)

## Controls

### Camera Movement
- **Arrow Keys**: Pitch (up/down) and Yaw (left/right)
- **Q / E**: Roll camera left/right
- **W / A / S / D**: Move camera in free mode (forward/left/back/right)
- **Mouse Wheel**: Zoom in/out in external camera, move forward/back in free mode

### Camera Modes
- **F1**: Cockpit camera (internal view)
- **F2**: External camera (orbiting around focus body)
- **F3**: Track camera (follow mode)
- **F4**: Free camera (unconstrained 6DOF flight)
- **V**: Attach camera to vessel
- **C**: Cycle through cockpit cameras
- **X**: Cycle through external camera positions

### Navigation
- **,** (Comma): Cycle to previous celestial body
- **.** (Period): Cycle to next celestial body
  - Order: Sol → Mercury → Venus → Earth → Moon → Mars → Jupiter → Saturn → Uranus → Neptune

### Time Control
- **1**: Decrease time acceleration
- **2**: Increase time acceleration

### General
- **ESC**: Exit simulation

## Current Solar System Bodies

The simulation includes the following celestial bodies with accurate VSOP87D positions:
- **Sun** (Sol): Self-luminous star at the center
- **8 Planets**: Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune
- **Moon**: Earth's natural satellite with realistic orbital motion

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
- [VSOP87 Implementation](docs/VSOP87_IMPLEMENTATION.md)
- [Starfield Rendering](docs/STARFIELD_RENDERING.md)
- [Plugin Development Guide](docs/PluginDevelopment.md)

## Technical Highlights

- **64-bit Precision**: All calculations use double precision until the final GPU conversion
- **Camera-Relative Rendering**: Objects positioned relative to camera to avoid float precision loss
- **Smooth Orbital Motion**: Phase-increment based orbital calculations for jitter-free movement
- **Depth Management**: Smart depth testing for astronomical scales (skybox vs celestial bodies)
- **Euler Angle Camera**: Accumulation-based rotation system (never recalculated from direction vectors)

## Credits

Inspired by Orbiter Space Flight Simulator and Kerbal Space Program.
