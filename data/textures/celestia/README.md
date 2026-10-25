# Celestial Body Textures

This directory contains textures for stars, planets, moons, asteroids, and comets.

## Structure

```
celestial/
├── Sol/                    # Solar System
│   ├── planets/           # Planet textures
│   ├── moons/             # Moon textures
│   ├── asteroids/         # Asteroid textures
│   └── comets/            # Comet textures
├── <StarSystem>/          # Other star systems
│   └── ...
└── stars/                 # Star textures
    └── spectral_types/    # By spectral class (O, B, A, F, G, K, M)
```

## Texture Naming Convention

### Planets & Moons
```
<Body>_color.png          # Albedo/color map (required)
<Body>_normal.png         # Normal/bump map (optional)
<Body>_specular.png       # Specular map (optional, for water/ice)
<Body>_emissive.png       # Emissive map (optional, for city lights)
<Body>_clouds.png         # Cloud layer (optional, with alpha)
<Body>_night.png          # Night-side texture (optional, for cities)
```

### Examples
- `Earth_color.png` - Main Earth texture
- `Earth_clouds.png` - Cloud layer with transparency
- `Earth_night.png` - City lights on night side
- `Moon_color.png` - Lunar surface
- `Mars_color.png` - Martian surface

### Stars
```
star_<Class><Subclass>.png   # e.g., star_G2.png for Sun-like star
star_<Class>.png             # Generic for class, e.g., star_M.png
```

Spectral classes:
- `O` - Blue supergiants (very hot)
- `B` - Blue-white giants
- `A` - White stars
- `F` - Yellow-white stars
- `G` - Yellow stars (like our Sun)
- `K` - Orange stars
- `M` - Red dwarfs

### Asteroids & Comets
```
asteroid_<Type>_<Number>.png   # e.g., asteroid_C_001.png
comet_nucleus_<Number>.png     # Comet nucleus texture
```

## Texture Requirements

### Resolution Guidelines
- **Large planets** (Earth, Jupiter): 4K-8K (4096×2048 or 8192×4096)
- **Medium bodies** (Moon, Mars): 2K-4K (2048×1024 or 4096×2048)
- **Small bodies** (asteroids, dwarf planets): 1K-2K (1024×512 or 2048×1024)
- **Stars**: 1K-2K (procedural fallback available)

### Format
- **Preferred**: PNG (lossless) or DDS (compressed, faster loading)
- **Acceptable**: JPEG (for very large textures where compression is needed)
- **Normal maps**: Store in tangent space, OpenGL format

### Projection
- **Equirectangular projection** (lat/lon map, 2:1 aspect ratio)
- 0° longitude at texture center
- 0° latitude at texture center vertically

## Celestia Compatibility

This system supports Celestia texture format conventions:
- Virtual textures (tile-based, level-of-detail)
- Normal maps in tangent space
- Specular maps for water/ice reflection
- Night textures for city lights

### Celestia Virtual Texture Format
```
<Body>/
├── tx_0_0.jpg    # Top-level tile
├── level1/       # Higher detail
│   ├── tx_0_0.jpg
│   ├── tx_0_1.jpg
│   └── ...
└── level2/       # Even higher detail
    └── ...
```

## Sources

Textures can be obtained from:
- NASA Visible Earth
- USGS Astrogeology Science Center
- Solar System Scope
- Celestia Motherlode
- Procedurally generated (for fictional systems)

## Attribution

When using third-party textures, include attribution in:
`data/textures/celestial/CREDITS.txt`
