# Kosmos Architecture Documentation

## System Overview

Kosmos is a cross-platform space flight simulator designed with modularity and extensibility in mind.

### Core Modules

1. **Core** - Application lifecycle, time system, configuration, logging
2. **Math** - Coordinate systems, quaternions, orbital elements
3. **Physics** - Universe simulation, gravity, vessels, propulsion
4. **Rendering** - OpenGL rendering, cameras, shaders, planets
5. **Plugin** - Dynamic vessel loading system
6. **Input** - Keyboard, mouse, joystick handling
7. **Audio** - 3D positional audio
8. **Data** - Asset loading, catalogs, scenarios
9. **Platform** - OS abstraction layer

### Propulsion System

The vessel propulsion system consists of three main components:

#### FuelTank
- Stores fuel/oxidizer
- Tracks mass and consumption
- Affects center of gravity dynamically

#### Thruster
- Models rocket engines, jets, ion drives, etc.
- Realistic ISP and thrust calculations
- Atmospheric effects on performance
- Gimbal capability for thrust vectoring

#### ThrusterGroup
- Organizes thrusters into functional groups
- Supports RCS (6 DOF control)
- Main engines, retro, hover modes
- Weighted thrust distribution

### Plugin System

Vessels are implemented as plugins (DLL/SO) that can be loaded at runtime:

```cpp
extern "C" {
    PLUGIN_EXPORT const char* GetPluginName();
    PLUGIN_EXPORT void* CreateVessel(void* engineInterface);
    // ... more functions
}
```

This allows community-created vessels without recompiling the engine.

## Next Steps

1. Implement SDL3 windowing and input
2. Add OpenGL renderer with basic mesh display
3. Implement reference frame system
4. Add gravity calculations
5. Implement VSOP87 for Sol system
6. Create first functional vessel
7. Add atmospheric model
8. Implement camera system
9. Add HUD and UI
10. Load star catalogs for nearby systems

## Development Guidelines

- Use C++20 features where beneficial
- Maintain cross-platform compatibility
- Document all public APIs
- Write tests for physics calculations
- Profile performance regularly
- Keep modules loosely coupled
