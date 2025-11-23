# Camera System Documentation

## Overview

Kosmos features a flexible camera system that supports multiple camera modes and can be attached to vessels or positioned anywhere in the simulation. The system consists of two main components:

- **Camera**: Low-level camera class handling view/projection matrices
- **CameraController**: High-level controller managing camera modes, attachments, and input

## Camera Modes

### 1. Free Camera (`CameraMode::FREE`)
- Unattached camera that can move freely in 3D space
- **Controls**:
  - `W/S` - Move forward/backward
  - `A/D` - Strafe left/right
  - `Space/Ctrl` - Move up/down
  - `Arrow Keys` - Rotate camera
  - `Page Up/Down` - Zoom

**Use cases**: Exploring the simulation, cinematic views, debugging

### 2. Cockpit Camera (`CameraMode::COCKPIT`)
- First-person view from inside the vessel
- Camera position and orientation locked to vessel
- Supports multiple cockpit camera definitions per vessel

**Controls**:
- `C` - Cycle through available cockpit cameras

**Features**:
- Defined per vessel with position, direction, up vector, and FOV
- Transforms with vessel orientation
- Ideal for piloting and instrument viewing

### 3. External Camera (`CameraMode::EXTERNAL`)
- Third-person view orbiting around the vessel
- Camera tracks vessel from a specified distance and angle
- Supports multiple predefined external views

**Controls**:
- `X` - Cycle through external camera presets
- `Arrow Keys` - Rotate around vessel
- `Page Up/Down` - Adjust distance

**Features**:
- Spherical coordinates (yaw, pitch, distance)
- Can track vessel or look in fixed direction
- Default views: rear, front, top

### 4. Track Camera (`CameraMode::TRACK`)
- Camera positioned at a fixed point in space, always looking at vessel
- Useful for observing vessel from specific locations

### 5. Ground Camera (`CameraMode::GROUND`)
- Camera positioned on celestial body surface
- *(Not yet fully implemented)*

### 6. Target Camera (`CameraMode::TARGET`)
- Camera at fixed position looking at a specific target
- *(Not yet fully implemented)*

### 7. Follow Camera (`CameraMode::FOLLOW`)
- Camera follows object at a specified offset
- *(Not yet fully implemented)*

## API Usage

### Basic Setup

```cpp
#include "Rendering/Camera.h"
#include "Rendering/CameraController.h"

// Create camera
auto camera = std::make_unique<Camera>();
camera->SetPerspective(60.0f, aspectRatio, 0.1f, 10000.0f);

// Create controller
auto cameraController = std::make_unique<CameraController>(camera.get());

// Attach to vessel
cameraController->AttachToVessel(myVessel);
cameraController->SetMode(CameraMode::COCKPIT);
```

### Defining Custom Cockpit Cameras

```cpp
// Add multiple cockpit camera views
cameraController->AddCockpitCamera(CockpitCamera(
    "Forward View",                    // Name
    glm::dvec3(0.0, 1.5, 2.0),        // Position (forward in cockpit)
    glm::dvec3(0.0, 0.0, -1.0),       // Direction (looking forward)
    glm::dvec3(0.0, 1.0, 0.0),        // Up vector
    60.0f                              // Field of view
));

cameraController->AddCockpitCamera(CockpitCamera(
    "Rear View",
    glm::dvec3(0.0, 1.5, 2.0),
    glm::dvec3(0.0, 0.0, 1.0),         // Looking backward
    glm::dvec3(0.0, 1.0, 0.0),
    90.0f                              // Wider FOV for rear view
));

// Cycle between cameras at runtime
cameraController->NextCockpitCamera();
```

### Defining Custom External Cameras

```cpp
// Add external camera presets
cameraController->AddExternalCamera(ExternalCamera(
    "Chase Camera",                    // Name
    glm::dvec3(0.0, 2.0, 0.0),        // Offset from vessel center
    15.0,                              // Distance in meters
    glm::radians(180.0),               // Yaw (behind vessel)
    glm::radians(10.0),                // Pitch (slightly above)
    true                               // Track vessel
));

cameraController->AddExternalCamera(ExternalCamera(
    "Side View",
    glm::dvec3(0.0, 0.0, 0.0),
    20.0,
    glm::radians(90.0),                // 90° to the side
    glm::radians(0.0),                 // Level with vessel
    true
));
```

### Manual Camera Control

```cpp
// Free camera movement
cameraController->SetMode(CameraMode::FREE);
cameraController->MoveFree(glm::dvec3(dx, dy, dz));
cameraController->RotateFree(deltaYaw, deltaPitch);

// External camera adjustment
cameraController->SetMode(CameraMode::EXTERNAL);
cameraController->RotateExternal(deltaYaw, deltaPitch);
cameraController->AdjustExternalDistance(deltaDistance);

// Get camera state
glm::dvec3 worldPos = cameraController->GetWorldPosition();
glm::dvec3 lookAt = cameraController->GetWorldTarget();
```

### Update Loop

```cpp
void Update(double deltaTime) {
    // Update camera controller each frame
    cameraController->Update(deltaTime);
    
    // Camera view/projection matrices are automatically updated
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectionMatrix();
}
```

## Default Controls

### Global
- `F1` - Switch to Cockpit mode
- `F2` - Switch to External mode
- `F3` - Switch to Track mode
- `F4` - Switch to Free mode
- `V` - Attach/detach camera to vessel
- `ESC` - Exit

### Mode-Specific

**Free Camera:**
- `W/A/S/D` - Move forward/left/back/right
- `Space` - Move up
- `Left Ctrl` - Move down
- `Arrow Keys` - Rotate view

**External Camera:**
- `X` - Cycle external camera presets
- `Arrow Keys` - Rotate around vessel
- `Page Up/Down` - Zoom in/out

**Cockpit Camera:**
- `C` - Cycle cockpit camera views

## Architecture

### Camera Class (`Camera.h/cpp`)
Low-level camera providing:
- Position, target, and up vectors
- View matrix calculation
- Projection matrix (perspective/orthographic)
- Basic movement methods

### CameraController Class (`CameraController.h/cpp`)
High-level controller providing:
- Camera mode management
- Vessel/body attachment
- Multiple camera definitions per mode
- Input-agnostic camera control
- Automatic updates based on attached object

### Integration Points

**Vessel Integration:**
```cpp
// Vessel must provide:
glm::dvec3 GetPosition() const;        // World position
glm::dmat3 GetOrientation() const;     // Rotation matrix (TODO)
```

**Application Integration:**
```cpp
// In Application class:
std::unique_ptr<CameraController> m_cameraController;

// Initialization:
m_cameraController = std::make_unique<CameraController>(m_camera.get());

// Update loop:
m_cameraController->Update(deltaTime);

// Input handling:
if (key == F1) m_cameraController->SetMode(CameraMode::COCKPIT);
```

## Future Enhancements

### Planned Features
- [ ] Camera shake effects for thruster firing
- [ ] Smooth camera transitions between modes
- [ ] Camera animation system (cutscenes, tours)
- [ ] Custom camera paths (splines)
- [ ] VR support with head tracking
- [ ] Picture-in-picture for multiple simultaneous views
- [ ] Replay camera system
- [ ] Vessel orientation integration (roll/pitch/yaw)
- [ ] Ground camera with terrain collision
- [ ] Docking camera presets
- [ ] Camera configuration file format

### API Improvements
- [ ] Camera state serialization/deserialization
- [ ] Camera preset saving/loading
- [ ] Event-based camera notifications
- [ ] Camera interpolation utilities
- [ ] FOV animation support

## Technical Notes

### Coordinate System
- Right-handed coordinate system
- Y-up orientation
- Vessel forward direction: -Z axis (TODO: verify)
- Camera positions are in simulation world space (meters)

### Precision
- Uses `glm::dvec3` (double precision) for positions
- Handles astronomical scales (meters to AU)
- View matrix converted to `glm::mat4` (float) for rendering
- Near/far planes configurable for different scales

### Performance
- Camera updates once per frame
- Minimal overhead for unattached cameras
- Vessel attachment uses direct position queries
- No physics simulation for camera movement

## Examples

### Spacecraft Cockpit Setup
```cpp
// Forward pilot view
controller->AddCockpitCamera(CockpitCamera(
    "Pilot", glm::dvec3(0, 1.6, 3.5), 
    glm::dvec3(0, 0, -1), glm::dvec3(0, 1, 0), 60.0f
));

// Overhead instruments view
controller->AddCockpitCamera(CockpitCamera(
    "Overhead Panel", glm::dvec3(0, 1.4, 2.5),
    glm::dvec3(0, 1, -0.2), glm::dvec3(0, 0, -1), 70.0f
));
```

### Landing Camera Setup
```cpp
// Bottom camera for landing
controller->AddExternalCamera(ExternalCamera(
    "Landing View", glm::dvec3(0, -2, 0),
    5.0, 0.0, glm::radians(-80.0), true
));
```

### Cinematic Camera
```cpp
controller->SetMode(CameraMode::FREE);
controller->SetFreePosition(glm::dvec3(1000, 500, 0));
controller->SetFreeTarget(vesselPosition);
```

## See Also
- `src/Rendering/Camera.h` - Base camera class
- `src/Rendering/CameraController.h` - Camera controller
- `src/Physics/Vessel.h` - Vessel integration
- `docs/CONTROLS.md` - Full control reference (TODO)
