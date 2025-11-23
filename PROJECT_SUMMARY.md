# Kosmos Project Structure - Complete Overview

## Created: November 23, 2025

## Project Location
`~/Documents/Projects/Kosmos`

## Directory Structure

```
Kosmos/
├── CMakeLists.txt              # Main CMake build configuration
├── LICENSE                      # MIT License
├── README.md                    # Project overview and build instructions
├── .gitignore                   # Git ignore patterns
├── build.sh                     # Linux build script
│
├── include/Kosmos/              # Public API headers
│   ├── Core.h                   # Core definitions and platform macros
│   └── PluginAPI.h              # Plugin C API for vessel creation
│
├── src/                         # Source code
│   ├── main.cpp                 # Application entry point
│   │
│   ├── Core/                    # Core engine systems
│   │   ├── Application.h/cpp    # Main application and game loop
│   │   ├── Time.h/cpp           # Time system and Julian dates
│   │   ├── Logger.h/cpp         # Logging system
│   │   └── Config.h/cpp         # Configuration management
│   │
│   ├── Math/                    # Mathematical utilities
│   │   ├── Quaternion.cpp       # Rotation math (stub)
│   │   ├── StateVector.cpp      # Position/velocity states (stub)
│   │   ├── CoordinateSystem.cpp # Reference frame transforms (stub)
│   │   ├── OrbitalElements.cpp  # Keplerian elements (stub)
│   │   └── Precision.cpp        # High-precision math (stub)
│   │
│   ├── Physics/                 # Physics simulation
│   │   ├── Universe.cpp         # Multi-system container (stub)
│   │   ├── SolarSystem.cpp      # Star system simulation (stub)
│   │   ├── CelestialBody.cpp    # Planets/moons/stars (stub)
│   │   ├── Vessel.h/cpp         # Spacecraft base class ✓
│   │   ├── FuelTank.h/cpp       # Fuel tank system ✓
│   │   ├── Thruster.h/cpp       # Thruster physics ✓
│   │   ├── ThrusterGroup.h/cpp  # Thruster grouping ✓
│   │   ├── GravityEngine.cpp    # N-body gravity (stub)
│   │   ├── ReferenceFrame.cpp   # Hierarchical frames (stub)
│   │   ├── Integrator.cpp       # RK4/RK8 integration (stub)
│   │   └── VSOP87.cpp           # Sol system ephemeris (stub)
│   │
│   ├── Rendering/               # Graphics subsystem (all stubs)
│   │   ├── Renderer.cpp
│   │   ├── Camera.cpp
│   │   ├── Shader.cpp
│   │   ├── Mesh.cpp
│   │   ├── Texture.cpp
│   │   ├── PlanetRenderer.cpp
│   │   ├── StarField.cpp
│   │   ├── Atmosphere.cpp
│   │   └── HUD.cpp
│   │
│   ├── Plugin/                  # Plugin system (stubs)
│   │   ├── PluginManager.cpp
│   │   ├── VesselPlugin.cpp
│   │   └── PluginAPI.cpp
│   │
│   ├── Input/                   # Input handling (stubs)
│   │   ├── InputManager.cpp
│   │   ├── Keyboard.cpp
│   │   ├── Mouse.cpp
│   │   └── Joystick.cpp
│   │
│   ├── Audio/                   # Audio system (stubs)
│   │   ├── AudioEngine.cpp
│   │   └── SoundSource.cpp
│   │
│   ├── Data/                    # Data loading (stubs)
│   │   ├── CatalogLoader.cpp
│   │   ├── PlanetData.cpp
│   │   ├── AssetManager.cpp
│   │   ├── ScenarioLoader.cpp
│   │   └── VesselConfigLoader.cpp
│   │
│   └── Platform/                # Platform abstraction (stubs)
│       ├── Window.cpp
│       ├── FileSystem.cpp
│       └── DynamicLibrary.cpp
│
├── plugins/                     # Plugin modules
│   ├── CMakeLists.txt
│   └── vessels/
│       ├── CMakeLists.txt
│       └── DeltaGlider.cpp      # Example vessel plugin ✓
│
├── data/                        # Game data
│   ├── catalogs/                # Star catalogs (empty)
│   ├── textures/                # Texture files (empty)
│   ├── models/                  # 3D models (empty)
│   ├── shaders/                 # GLSL shaders
│   │   ├── basic.vert           # Basic vertex shader ✓
│   │   └── basic.frag           # Basic fragment shader ✓
│   ├── scenarios/               # Mission scenarios
│   │   └── iss_orbit.json       # Example scenario ✓
│   └── vsop87/                  # VSOP87 data files (empty)
│
├── docs/                        # Documentation
│   └── Architecture.md          # Architecture overview ✓
│
├── tests/                       # Unit tests (empty)
│
└── external/                    # Third-party libraries (empty)
```

## Implementation Status

### ✓ Fully Implemented
- **Core System**: Application loop, Time management, Logger, Config
- **Propulsion System**: FuelTank, Thruster, ThrusterGroup classes
- **Vessel Base**: Vessel class with full propulsion integration
- **Plugin Example**: DeltaGlider vessel plugin stub
- **Build System**: Complete CMake configuration
- **Shaders**: Basic vertex and fragment shaders
- **Documentation**: README, Architecture docs

### Stub/Placeholder
- Math utilities (need GLM integration)
- Physics (Universe, SolarSystem, CelestialBody, Gravity, VSOP87)
- Rendering system (needs SDL3 and OpenGL)
- Input system (needs SDL3)
- Audio system
- Data loaders
- Platform abstractions

## Key Features Implemented

### 1. Advanced Propulsion System
```cpp
// Multiple fuel types
FuelTank::FuelType:
  - LIQUID_HYDROGEN, LIQUID_OXYGEN
  - RP1, HYDRAZINE, XENON
  - NUCLEAR_THERMAL, ATMOSPHERE

// Realistic thrusters
Thruster::Type:
  - ROCKET, MONOPROP, ION
  - NUCLEAR_THERMAL, AIRBREATHING_JET, SCRAMJET, RCS

// Features:
  - ISP (specific impulse) for vacuum and atmosphere
  - Fuel consumption based on Tsiolkovsky equation
  - Throttling and gimbal support
  - Dynamic center of gravity calculation
```

### 2. Thruster Grouping System
```cpp
ThrusterGroup::GroupType:
  - MAIN, RETRO, HOVER
  - RCS_PITCH_UP/DOWN
  - RCS_YAW_LEFT/RIGHT
  - RCS_ROLL_LEFT/RIGHT
  - RCS_TRANSLATION_FORWARD/BACK/LEFT/RIGHT/UP/DOWN
  - CUSTOM
```

### 3. Plugin Architecture
- C API for cross-compiler compatibility
- DLL/.so loading at runtime
- Vessels can be created independently
- API version checking

## Build Instructions

### Dependencies Required
```bash
# Ubuntu/Debian
sudo apt-get install libsdl3-dev libglm-dev libgl1-mesa-dev cmake g++

# Note: SDL3 may need to be built from source as it's relatively new
```

### Building
```bash
cd ~/Documents/Projects/Kosmos
./build.sh

# Or manually:
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Running
```bash
cd build/bin
./Kosmos
```

## Next Development Steps

### Phase 1: Foundation (Current)
- [x] Project structure
- [x] Build system
- [x] Core application loop
- [x] Propulsion system
- [ ] Integrate SDL3
- [ ] Integrate GLM
- [ ] Basic OpenGL window

### Phase 2: Physics
- [ ] Reference frame system
- [ ] Gravity calculations
- [ ] RK4 integrator
- [ ] Simple solar system (Sun + Earth)

### Phase 3: Rendering
- [ ] Camera system
- [ ] Mesh rendering
- [ ] Basic lighting
- [ ] Planet rendering

### Phase 4: Vessels
- [ ] Complete vessel physics integration
- [ ] Test vessel with thrusters
- [ ] Atmospheric model
- [ ] Orbital mechanics

### Phase 5: VSOP87
- [ ] Load VSOP87 data files
- [ ] Calculate planetary positions
- [ ] Full Sol system

### Phase 6: Multi-System
- [ ] Load star catalogs
- [ ] Spatial partitioning
- [ ] Interstellar navigation

## Design Documents

Detailed design documents are located in `/tmp/`:
- `space_flight_simulator_design.md` - Original complete architecture
- `vessel_propulsion_system_design.md` - Detailed propulsion system

## Notes

- Project uses C++20 standard
- Cross-platform: Linux, Windows, macOS
- Many modules are currently stubs to allow compilation
- Propulsion system is fully designed and implemented
- Focus on implementing SDL3 window and OpenGL next
- GLM will replace temporary vector stubs

## License

MIT License - See LICENSE file

## Contact

This is a comprehensive space flight simulator project inspired by Orbiter and Kerbal Space Program, with support for hundreds of solar systems and realistic physics.
