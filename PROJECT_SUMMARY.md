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
│   │   ├── SolarSystem.h/cpp    # Star system simulation ✓
│   │   ├── CelestialBody.cpp    # Planets/moons/stars ✓
│   │   ├── Vessel.h/cpp         # Spacecraft base class ✓
│   │   ├── FuelTank.h/cpp       # Fuel tank system ✓
│   │   ├── Thruster.h/cpp       # Thruster physics ✓
│   │   ├── ThrusterGroup.h/cpp  # Thruster grouping ✓
│   │   ├── GravityEngine.cpp    # N-body gravity ✓
│   │   ├── ReferenceFrame.cpp   # Hierarchical frames ✓
│   │   ├── Integrator.cpp       # RK4/RK8 integration ✓
│   │   └── VSOP87.h/cpp         # High-precision planetary positions ✓
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
│   ├── catalogs/                # Star catalogs ✓
│   │   ├── README.md            # Catalog download guide (Hipparcos, Gliese, Gaia)
│   │   └── nearest_stars.txt    # Test catalog (can be auto-generated)
│   ├── textures/                # Texture files ✓
│   │   ├── TEXTURE_SOURCES.md   # Complete texture download guide
│   │   ├── celestial/           # Celestial body textures
│   │   │   ├── Sol/             # Solar system (planets, moons, asteroids, comets)
│   │   │   └── stars/           # Star textures by spectral type
│   │   └── skybox/              # Milky Way panorama textures
│   ├── models/                  # 3D models (empty)
│   ├── plugins/                 # Plugin assets ✓
│   │   └── vessels/             # Vessel plugin data
│   │       └── DeltaGlider/     # Example: meshes, textures, sounds, config
│   ├── shaders/                 # GLSL shaders
│   │   ├── basic.vert           # Basic vertex shader ✓
│   │   └── basic.frag           # Basic fragment shader ✓
│   ├── scenarios/               # Mission scenarios
│   │   └── iss_orbit.json       # Example scenario ✓
│   └── vsop87/                  # VSOP87 planetary data ✓
│       ├── README.md            # Data download instructions
│       └── VSOP87D.*            # 8 planetary coefficient files (5.1 MB)
│
├── docs/                        # Documentation
│   ├── Architecture.md          # Architecture overview ✓
│   ├── VSOP87_IMPLEMENTATION.md # VSOP87 system documentation ✓
│   └── STARFIELD_RENDERING.md   # Skybox and star rendering guide ✓
│
├── tests/                       # Unit tests
│   └── validate_vsop87.py       # VSOP87 accuracy validation ✓
├── download_data.sh             # Quick data download script ✓
│
└── external/                    # Third-party libraries (empty)
```

## Implementation Status

### ✓ Fully Implemented
- **Core System**: Application loop, Time management, Logger, Config
- **Window & Input**: SDL2 window creation, OpenGL context, event handling
- **Rendering System**: Camera, Shader, Mesh (spheres, cubes, planes), PlanetRenderer
- **Math System**: StateVector, OrbitalElements (Keplerian conversion), using GLM
- **Physics Engine**: CelestialBody, GravityEngine (N-body), Integrator (Euler/RK4/RK8)
- **Solar System**: SolarSystem manager with full Sol system (Sun + 8 planets)
- **VSOP87 System**: High-precision planetary positions using VSOP87D analytical theory
- **Propulsion System**: FuelTank, Thruster, ThrusterGroup classes
- **Vessel Base**: Vessel class with full propulsion integration
- **Plugin Example**: DeltaGlider vessel plugin stub
- **Build System**: Complete CMake configuration, outputs to project root
- **Shaders**: Basic vertex and fragment shaders with lighting
- **Documentation**: README, Architecture docs, VSOP87 implementation guide
- **Asset Organization**: Complete folder structure for plugins and celestial textures

### Stub/Placeholder
- Physics simulation integration (vessels in orbit with VSOP87)
- Input system enhancements (camera controls, key bindings)
- Audio system
- Data loaders (scenarios, catalogs, vessel configs)
- Platform abstractions (file system helpers)
- Plugin loading system
- Texture loading (Celestia format support planned)
- Star field background
- Atmospheric rendering
- HUD/UI system

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
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Running
```bash
cd ~/Documents/Projects/Kosmos
./Kosmos
```

Binary outputs to project root, plugins to `lib/` folder.

## Next Development Steps

### Phase 1: Foundation ✅ COMPLETE
- [x] Project structure
- [x] Build system
- [x] Core application loop
- [x] Propulsion system
- [x] Integrate SDL2
- [x] Integrate GLM
- [x] Basic OpenGL window

### Phase 2: Physics ✅ COMPLETE
- [x] Reference frame system
- [x] Gravity calculations (N-body)
- [x] RK4/RK8 integrator
- [x] Simple solar system (Sun + Earth)
- [x] Full solar system (Sun + 8 planets)
- [x] VSOP87 high-precision planetary positions
- [x] Julian Date support
- [x] Hybrid VSOP87/numerical integration mode

### Phase 3: Rendering Enhancement (Next Priority)
- [ ] Fix planet visibility (scale/positioning issues)
- [ ] Camera controls (mouse rotation, zoom)
- [ ] Star field background
- [ ] Simple lighting improvements
- [ ] HUD with basic info (time, camera distance)

### Phase 4: Physics Integration
- [ ] Vessel physics integration with gravity
- [ ] Test vessel in orbit around Earth
- [ ] Atmospheric model (basic)
- [ ] Add Moon to test system

### Phase 5: Textures & Visuals
- [ ] Texture loading system
- [ ] Celestia texture format support
- [ ] Apply textures to planets
- [ ] Atmospheric scattering

### Phase 6: User Interaction (Current Focus)
- [ ] Camera controls (WASD, mouse)
- [ ] Time acceleration controls
- [ ] Planet focus switching
- [ ] Information overlays

### Phase 7: Multi-System
- [ ] Load star catalogs
- [ ] Spatial partitioning
- [ ] Interstellar navigation

## VSOP87 Implementation

### Overview
Kosmos now includes complete VSOP87 (Variations Séculaires des Orbites Planétaires) support for high-precision planetary ephemerides. This analytical solution provides accurate positions for all 8 major planets over thousands of years.

### Features
- **Data Loading**: Reads IMCCE VSOP87 coefficient files (~5.1 MB for variant D)
- **Series Evaluation**: Evaluates thousands of periodic terms: Σ A*cos(B + C*T)
- **Position & Velocity**: Calculates both position (analytical) and velocity (numerical differentiation)
- **Coordinate Systems**: Supports all VSOP87 variants (A-E), currently using D (rectangular heliocentric equatorial)
- **Julian Date**: Proper epoch handling and time conversion
- **Hybrid Mode**: Can switch between VSOP87 and numerical integration

### Accuracy
- Short term (centuries): Sub-arcsecond precision
- Long term (millennia): Arcsecond to arcminute precision
- Position error: ~10^-8 AU (~1500 km) for inner planets

### Files
- `src/Physics/VSOP87.h/cpp` - Core implementation
- `src/Physics/SolarSystem.h/cpp` - Integration with simulation
- `data/vsop87/` - VSOP87D coefficient files (downloaded)
- `docs/VSOP87_IMPLEMENTATION.md` - Complete documentation
- `tests/validate_vsop87.py` - Validation script against JPL Horizons

### Usage
```cpp
auto solarSystem = SolarSystem::CreateSolSystem();
solarSystem->EnableVSOP87("data/vsop87", 'D');  // Load VSOP87D
solarSystem->SetJulianDate(2451545.0);  // Set epoch to J2000.0
solarSystem->Update(deltaTime);  // VSOP87 provides positions
```

See `docs/VSOP87_IMPLEMENTATION.md` for complete details.

## Asset Organization

### Plugin Assets (`data/plugins/`)
Each vessel plugin has its own folder structure:
```
vessels/<VesselName>/
├── config/       # JSON configuration (vessel.json, animations.json)
├── meshes/       # 3D models (OBJ, GLTF)
├── textures/     # Vessel textures (diffuse, normal, specular, emissive)
└── sounds/       # Audio samples (engines, RCS, mechanical sounds)
```

**Example:** DeltaGlider has complete folder structure with documented vessel.json

### Celestial Textures (`data/textures/celestial/`)
Organized by star system and body type:
```
celestial/
├── Sol/
│   ├── planets/      # Earth_color.png, Mars_color.png, etc.
│   ├── moons/        # Moon_color.png, Titan_color.png, etc.
│   ├── asteroids/    # Asteroid textures
│   └── comets/       # Comet nucleus textures
└── stars/
    └── spectral_types/  # O, B, A, F, G, K, M class stars
```

**Texture Format:** Equirectangular projection (2:1 aspect ratio), PNG/DDS
**Celestia Compatible:** Supports virtual texture tiles and standard naming

See `data/plugins/README.md` and `data/textures/celestial/README.md` for details.

## Design Documents

Detailed design documents are located in `/tmp/`:
- `space_flight_simulator_design.md` - Original complete architecture
- `vessel_propulsion_system_design.md` - Detailed propulsion system

## Notes

- Project uses C++20 standard
- Cross-platform: Linux, Windows, macOS
- Executable builds to project root, plugins to `lib/`
- Asset organization supports both plugin and celestial content
- Celestia texture format compatibility planned

## License

MIT License - See LICENSE file

## Contact

This is a comprehensive space flight simulator project inspired by Orbiter and Kerbal Space Program, with support for hundreds of solar systems and realistic physics.
