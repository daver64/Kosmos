# Celestial Body Textures for Kosmos

This directory contains textures for planets, moons, stars, and other celestial objects.

## Supported Texture Formats

Kosmos aims to support textures from multiple sources:
- **Celestia** - Popular space simulation software
- **Orbiter** - Space flight simulator
- **Custom textures** - Any equirectangular or DDS format

## Directory Structure

```
textures/
├── celestial/          # Celestial body textures
│   ├── Sol/           # Solar system objects
│   │   ├── planets/   # Planet textures
│   │   ├── moons/     # Moon textures
│   │   ├── asteroids/ # Asteroid textures
│   │   └── comets/    # Comet textures
│   └── stars/         # Star textures by spectral class
├── skybox/            # Background sky/milky way
└── vessels/           # Spacecraft textures (in plugins/)
```

## Texture Naming Conventions

### Celestia Format
Celestia uses standardized naming:
```
<ObjectName>_<MapType>.<ext>

Map Types:
- (no suffix) or _color: Diffuse/albedo map
- _bump: Normal/bump map
- _night: Night-side lights
- _spec: Specular/gloss map
- _emissive: Emissive/glow map
- _clouds: Cloud layer
```

Examples:
```
Earth_color.png         # Earth day-side texture
Earth_night.png         # City lights at night
Earth_clouds.png        # Cloud layer
Earth_bump.png          # Elevation/normal map
Earth_spec.png          # Specular map (water = shiny)
```

### Orbiter Format
Orbiter typically uses:
```
<ObjectName>.<ext>      # Base texture
<ObjectName>_lmask.png  # Light mask (oceans for specularity)
<ObjectName>_night.png  # Night lights
<ObjectName>_cloud.dds  # Cloud layer
```

## Downloading Textures

### 1. Celestia Textures (Recommended)

**High-Quality Earth Textures:**
```bash
cd data/textures/celestial/Sol/planets

# Earth 8K from NASA Blue Marble
wget https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg
mv world.topo.bathy.200412.3x5400x2700.jpg Earth_color_8k.jpg

# Or use Celestia Motherlode
wget https://www.celestiamotherlode.net/creators/jh/textures/Earth_8k.jpg -O Earth_color.jpg
```

**Full Celestia Texture Pack:**
```bash
# Download from Celestia official site or Motherlode
# Option 1: Direct download (if available)
cd ~/Downloads
wget https://celestia.space/download/celestia-textures-1.7.0.zip
unzip celestia-textures-1.7.0.zip
cp -r celestia-textures/* ~/Documents/Projects/Kosmos/data/textures/celestial/

# Option 2: Clone from repository
git clone https://github.com/CelestiaProject/CelestiaContent.git
cp -r CelestiaContent/textures/* ~/Documents/Projects/Kosmos/data/textures/celestial/
```

**Individual Planet Textures from Celestia Motherlode:**
```bash
cd data/textures/celestial/Sol/planets

# Mercury
wget https://www.celestiamotherlode.net/catalog/images/textures/medres/mercury.jpg

# Venus
wget https://www.celestiamotherlode.net/catalog/images/textures/medres/venus.jpg

# Mars
wget https://www.celestiamotherlode.net/catalog/images/textures/medres/mars.jpg

# Jupiter
wget https://www.celestiamotherlode.net/catalog/images/textures/medres/jupiter.jpg

# Saturn
wget https://www.celestiamotherlode.net/catalog/images/textures/medres/saturn.jpg

# Uranus
wget https://www.celestiamotherlode.net/catalog/images/textures/medres/uranus.jpg

# Neptune
wget https://www.celestiamotherlode.net/catalog/images/textures/medres/neptune.jpg
```

### 2. NASA/ESA Official Textures

**NASA Solar System Exploration:**
```bash
cd data/textures/celestial/Sol/planets

# Earth - Blue Marble
wget https://eoimages.gsfc.nasa.gov/images/imagerecords/74000/74218/world.200412.3x5400x2700.jpg -O Earth_8k.jpg

# Mars - Viking MDIM
wget https://planetarymaps.usgs.gov/mosaic/Mars_Viking_MDIM21_ClrMosaic_global_232m.tif

# Moon
wget https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_4k.jpg -O Moon_4k.jpg
```

**JPL Photojournal:**
Visit https://photojournal.jpl.nasa.gov/ and search for planetary texture maps.

### 3. Solar System Scope Textures (Free for Non-Commercial)

High-quality 2K/4K/8K textures:
```bash
cd data/textures/celestial/Sol/planets

# Visit https://www.solarsystemscope.com/textures/
# Download available textures (requires registration)
# Free for non-commercial use
```

### 4. Orbiter Textures

If you have Orbiter installed, copy textures from:
```bash
# Orbiter installation path (example)
ORBITER_PATH="/path/to/Orbiter"

cp $ORBITER_PATH/Textures/*.dds data/textures/celestial/Sol/planets/
cp $ORBITER_PATH/Textures2/*.dds data/textures/celestial/Sol/planets/

# Convert DDS to PNG if needed
for f in *.dds; do
    convert "$f" "${f%.dds}.png"
done
```

**Orbiter Add-ons:**
Many high-quality textures available at:
- https://www.orbithangar.com/
- Look for "Texture Packs" category

### 5. Procedural Textures

For missing objects, use procedural generation tools:
- **Celestia's `texgen`**
- **Blender** with planet texture add-ons
- **Space Engine** exported textures

## Moon Textures

```bash
cd data/textures/celestial/Sol/moons

# Earth's Moon
wget https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_8k.jpg -O Moon_8k.jpg

# Jupiter's moons
wget https://planetarymaps.usgs.gov/mosaic/Io_Voyager_ClrMosaic_Global_250m.tif -O Io.tif
wget https://planetarymaps.usgs.gov/mosaic/Europa_Voyager_GalileoSSI_global_mosaic_500m.tif -O Europa.tif
wget https://planetarymaps.usgs.gov/mosaic/Ganymede_Voyager_GalileoSSI_Global_ClrMosaic_1435m.tif -O Ganymede.tif
wget https://planetarymaps.usgs.gov/mosaic/Callisto_Voyager_GalileoSSI_global_mosaic_1435m.tif -O Callisto.tif

# Saturn's Titan
wget https://planetarymaps.usgs.gov/mosaic/Titan_ISS_P19658_Mosaic_Global_4km.tif -O Titan.tif
```

## Star Textures

For rendering nearby stars, create textures for different spectral types:

```bash
cd data/textures/celestial/stars

# Create or download basic star corona textures
# Spectral types: O, B, A, F, G, K, M
# Colors: Blue, Blue-white, White, Yellow-white, Yellow, Orange, Red

# Example: Use procedural textures or download from:
wget https://www.celestiamotherlode.net/creators/oort/textures/star_O.jpg
wget https://www.celestiamotherlode.net/creators/oort/textures/star_B.jpg
wget https://www.celestiamotherlode.net/creators/oort/textures/star_A.jpg
wget https://www.celestiamotherlode.net/creators/oort/textures/star_F.jpg
wget https://www.celestiamotherlode.net/creators/oort/textures/star_G.jpg  # Sun-like
wget https://www.celestiamotherlode.net/creators/oort/textures/star_K.jpg
wget https://www.celestiamotherlode.net/creators/oort/textures/star_M.jpg
```

## Quick Start - Essential Textures

For immediate functionality, download these minimal textures:

```bash
#!/bin/bash
# Quick start texture download script

cd data/textures/celestial/Sol/planets

# Sun (use yellow/orange gradient)
wget https://www.solarsystemscope.com/textures/download/2k_sun.jpg -O Sol_color.jpg

# Earth (Blue Marble)
wget https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg -O Earth_color.jpg

# Moon
wget https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_4k.jpg -O Moon_color.jpg

# Mars
wget https://www.solarsystemscope.com/textures/download/2k_mars.jpg -O Mars_color.jpg

# Jupiter
wget https://www.solarsystemscope.com/textures/download/2k_jupiter.jpg -O Jupiter_color.jpg
```

## Texture Format Support

Kosmos will support:
- ✅ **PNG** - Preferred for quality
- ✅ **JPG** - Good for photos
- ✅ **DDS** - DirectDraw Surface (Orbiter format, with compression)
- ⏳ **TGA** - Targa format
- ⏳ **Virtual Textures** - Tiled formats for huge textures

## Texture Resolution Guidelines

| Object Type | Recommended Resolution | Max Size |
|-------------|----------------------|----------|
| Sun | 2K-4K | 8K |
| Terrestrial Planets | 4K-8K | 16K |
| Gas Giants | 2K-4K | 8K |
| Major Moons | 2K-4K | 8K |
| Minor Moons | 1K-2K | 4K |
| Asteroids | 512-1K | 2K |
| Stars (nearby) | 2K | 4K |
| Skybox | 8K equirect | 16K |

## Compatibility Notes

### Celestia → Kosmos
- Direct compatibility for PNG/JPG textures
- May need to rename files to match conventions
- DDS textures work if OpenGL DDS support enabled

### Orbiter → Kosmos
- Need DDS texture loader or conversion to PNG
- Light masks map to specular maps
- Cloud layers may need alpha channel adjustments

### Conversion Tools
```bash
# DDS to PNG
sudo apt-get install imagemagick
convert texture.dds texture.png

# Resize textures
convert input.jpg -resize 4096x2048 output.jpg

# Create mipmaps
convert input.png -define dds:mipmaps=10 output.dds
```

## Legal / Licensing

### Public Domain / NASA
- NASA imagery is generally public domain
- Check specific mission restrictions
- Credit requested but not required

### Celestia
- Most Celestia textures are open source
- Check individual texture licenses
- Many use Creative Commons licenses

### Orbiter
- Community add-ons have various licenses
- Check each texture pack's license
- Generally free for non-commercial use

### Commercial Sources
- Solar System Scope: Free for non-commercial
- Turbosquid, CGTrader: Paid licenses

## Creating Your Own Textures

For procedural or custom textures, tools:
- **Blender** + Planetary shader nodes
- **Substance Designer**
- **World Machine** (terrain heightmaps)
- **Gimp/Photoshop** with planet plugins

## References

- **Celestia Motherlode:** https://www.celestiamotherlode.net/
- **NASA Images:** https://images.nasa.gov/
- **USGS Astrogeology:** https://astrogeology.usgs.gov/
- **Solar System Scope:** https://www.solarsystemscope.com/textures/
- **Orbit Hangar:** https://www.orbithangar.com/
- **JPL Photojournal:** https://photojournal.jpl.nasa.gov/

## Next Steps

1. Download essential textures (Sun, Earth, Moon, Mars)
2. Implement texture loader in `src/Rendering/Texture.cpp`
3. Add texture mapping to `PlanetRenderer`
4. Test with nearest planets first
5. Add LOD (Level of Detail) system for performance
