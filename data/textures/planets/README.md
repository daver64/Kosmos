# Planet Textures

This directory contains textures for celestial bodies in the Kosmos space simulator.
These maps are used when no explicit, hi-res, Celestia-hires or `data/textures/` image exists for the body.
The bundled Venus image here is a radar-derived surface map; the separate `data/textures/venus.jpg` shows cloud tops.
See `data/CREDITS-textures.md` for source and licensing details.

## Recommended Sources

### 1. Celestia Motherlode
- **URL**: https://www.celestiamotherlode.net/catalog/textures.html
- High-quality planet textures in various resolutions
- Compatible formats: JPG, PNG, DDS

### 2. NASA Visible Earth (Blue Marble)
- **URL**: https://visibleearth.nasa.gov/collection/1484/blue-marble
- Official NASA imagery for Earth
- Available in 1k, 2k, 4k, 8k resolutions

### 3. Solar System Scope
- **URL**: https://www.solarsystemscope.com/textures/
- Free high-resolution textures for all planets
- Download link: https://www.solarsystemscope.com/textures/download/2k_earth_daymap.jpg

### 4. JHT's Planetary Pixel Emporium
- **URL**: http://planetpixelemporium.com/
- Excellent quality textures with specular/normal maps
- Free for non-commercial use

## File Naming Convention

Place textures in this directory with the following names:

### Main Textures (Diffuse/Color Maps)
- `earth.jpg` or `earth_4k.jpg` - Earth surface (Blue Marble)
- `moon.jpg` or `moon_4k.jpg` - Lunar surface (LROC)
- `mars.jpg` or `mars_4k.jpg` - Mars surface (Viking/MGS)
- `sun.jpg` or `sun_8k.jpg` - Solar photosphere (SDO)
- `mercury.jpg` - Mercury surface
- `venus.jpg` - Venus atmosphere/surface
- `jupiter.jpg` - Jupiter cloud tops
- `saturn.jpg` - Saturn cloud tops
- `uranus.jpg` - Uranus atmosphere
- `neptune.jpg` - Neptune atmosphere

### Optional Enhancement Maps
- `earth_night.jpg` - Earth city lights
- `earth_clouds.jpg` - Cloud layer (separate from surface)
- `earth_specular.jpg` - Specular map (water reflection)
- `earth_normal.jpg` - Normal/bump map for terrain detail
- `mars_normal.jpg` - Mars terrain bump map

## Texture Specifications

### Recommended Resolutions
- **Close-up planets** (Earth, Moon, Mars): 4096x2048 (4K) or 8192x4096 (8K)
- **Gas giants** (Jupiter, Saturn): 4096x2048 (4K)
- **Distant planets**: 2048x1024 (2K)
- **Sun**: 4096x2048 or higher (for chromosphere detail)

### Format Requirements
- **File format**: JPEG (.jpg) for color maps, PNG (.png) for alpha channels
- **Projection**: Equirectangular (lat/lon mapping)
- **Aspect ratio**: 2:1 (width = 2 × height)
- **Color space**: sRGB

### Texture Coordinates
Textures use spherical UV mapping:
- U (horizontal): 0.0 at Prime Meridian (0° longitude), wraps at 360°
- V (vertical): 0.0 at South Pole (-90° lat), 1.0 at North Pole (+90° lat)

## Quick Start - Minimum Set

To get started quickly, download these essential textures:

1. **Earth**: https://www.solarsystemscope.com/textures/download/2k_earth_daymap.jpg
   - Rename to: `earth.jpg`

2. **Moon**: https://www.solarsystemscope.com/textures/download/2k_moon.jpg
   - Rename to: `moon.jpg`

3. **Mars**: https://www.solarsystemscope.com/textures/download/2k_mars.jpg
   - Rename to: `mars.jpg`

4. **Sun**: https://www.solarsystemscope.com/textures/download/2k_sun.jpg
   - Rename to: `sun.jpg`

## Implementation Notes

### Atmosphere Rendering
For planets with atmospheres (Earth, Mars, Venus, Titan), atmospheric scattering is rendered as a separate pass using Rayleigh scattering approximation.

### Texture Streaming
Large 8K textures may require:
- Mipmapping for LOD management
- Compressed formats (DDS with DXT1/DXT5)
- Streaming from disk for memory management

### Performance
- 2K textures: ~12 MB memory per texture
- 4K textures: ~48 MB memory per texture
- 8K textures: ~192 MB memory per texture

Consider using lower resolutions for distant planets and switching to higher resolutions based on camera distance.

## License Information

Most Celestia textures are derived from public domain NASA imagery or have permissive licenses. Always check the license for each texture:

- **NASA imagery**: Public domain
- **Celestia Motherlode**: Usually free for non-commercial use, check individual licenses
- **Solar System Scope**: CC BY 4.0; attribution required

## Current Status

- [ ] Sun texture
- [ ] Mercury texture
- [ ] Venus texture
- [ ] Earth texture
- [ ] Earth night lights
- [ ] Earth clouds (optional)
- [ ] Moon texture
- [ ] Mars texture
- [ ] Jupiter texture
- [ ] Saturn texture
- [ ] Uranus texture
- [ ] Neptune texture
