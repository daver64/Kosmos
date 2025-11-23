# Plugin Assets

This directory contains assets for all plugin modules (vessels, bases, etc.)

## Structure

```
plugins/
├── vessels/              # Vessel plugins
│   └── <VesselName>/    # Each vessel has its own folder
│       ├── config/      # JSON configuration files
│       ├── meshes/      # 3D models (.obj, .dae, .gltf)
│       ├── textures/    # Vessel textures
│       └── sounds/      # Audio samples (engine, RCS, etc.)
└── bases/               # Surface bases (future)
```

## Vessel Plugin Assets

Each vessel should have:

### config/
- `vessel.json` - Main vessel configuration (mass, dimensions, fuel tanks, thrusters)
- `animations.json` - Animations (landing gear, doors, etc.)
- `panels.json` - Cockpit panel definitions (if applicable)

### meshes/
- 3D model files in supported formats
- Recommended: OBJ with MTL, or GLTF 2.0
- Include LOD variants for performance
- Naming: `<vessel>_lod0.obj`, `<vessel>_lod1.obj`, etc.

### textures/
- Diffuse/albedo maps
- Normal maps
- Specular/roughness maps
- Emissive maps (for lights)
- Format: PNG, DDS (compressed), or TGA
- Celestia-compatible texture format supported

### sounds/
- Engine sounds (main, hover, RCS)
- Mechanical sounds (gear, doors)
- Alarms and cockpit sounds
- Format: WAV, OGG (compressed)

## Example: DeltaGlider

```
DeltaGlider/
├── config/
│   └── vessel.json
├── meshes/
│   ├── deltaglider_exterior.obj
│   ├── deltaglider_interior.obj
│   └── deltaglider_lod1.obj
├── textures/
│   ├── hull_diffuse.png
│   ├── hull_normal.png
│   └── cockpit_emissive.png
└── sounds/
    ├── main_engine.ogg
    ├── rcs_thrust.ogg
    └── landing_gear.wav
```

## Loading

Plugin assets are loaded relative to: `data/plugins/vessels/<VesselName>/`

C++ code can use: `AssetManager::LoadVesselAsset("DeltaGlider", "meshes/deltaglider_exterior.obj")`
