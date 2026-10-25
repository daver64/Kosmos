# Star Catalogs for Kosmos

This directory contains star catalog data for rendering the stellar background and nearby star systems.

## Required Catalogs

### 1. Hipparcos-2 Catalog (Nearby Stars)
The Hipparcos catalog contains precise positions, magnitudes, and colors for ~118,000 stars.

**Download:**
```bash
cd data/catalogs
# Main Hipparcos-2 catalog (reduced version with nearby stars)
wget https://cdsarc.cds.unistra.fr/ftp/cats/I/311/hip2.dat.gz
gunzip hip2.dat.gz

# Alternative: Full catalog
# wget https://cdsarc.cds.unistra.fr/ftp/cats/I/311/hip_main.dat.gz
```

**File Format:**
- Fixed-width columns
- Columns include: HIP ID, RA, Dec, Parallax, Magnitude, B-V color, Spectral type
- Readme: https://cdsarc.cds.unistra.fr/ftp/cats/I/311/ReadMe

**Size:** ~20 MB (compressed), ~50 MB (uncompressed)

### 2. Gliese Catalog of Nearby Stars (Optional)
Contains detailed data for stars within 25 parsecs (~82 light-years).

**Download:**
```bash
cd data/catalogs
wget https://cdsarc.cds.unistra.fr/ftp/cats/V/70A/catalog.dat
```

**Size:** ~500 KB

### 3. Yale Bright Star Catalog (Optional)
Contains the ~9,000 brightest stars visible to the naked eye.

**Download:**
```bash
cd data/catalogs
wget http://tdc-www.harvard.edu/catalogs/bsc5.dat.gz
gunzip bsc5.dat.gz
```

**Size:** ~2 MB

## Recommended Approach

For Kosmos, we recommend a **tiered approach**:

1. **Tier 1: Nearby Stars (< 50 light-years)** - Render as actual 3D objects
   - Source: Gliese catalog + Hipparcos subset
   - ~2,000 stars
   - Use actual spectral types for colors

2. **Tier 2: Visible Stars (< 1000 light-years)** - Render as billboards
   - Source: Hipparcos full catalog
   - ~118,000 stars
   - Scale brightness by apparent magnitude

3. **Tier 3: Background Milky Way** - Sky dome texture
   - Use ESA Gaia sky maps or Celestia Milky Way texture
   - Pre-rendered 360° panorama

## Gaia DR3 (Alternative Modern Catalog)

ESA's Gaia mission provides the most precise stellar positions.

**Download (subset):**
```bash
cd data/catalogs
# Download bright stars (G < 10 mag, ~3 million stars)
# This requires Python and astroquery:
# pip install astroquery

python3 << 'EOF'
from astroquery.gaia import Gaia
import pandas as pd

# Query bright nearby stars
query = """
SELECT TOP 50000
    source_id, ra, dec, parallax, pmra, pmdec, 
    phot_g_mean_mag, bp_rp, teff_gspphot
FROM gaiadr3.gaia_source
WHERE parallax > 10 AND phot_g_mean_mag < 10
ORDER BY phot_g_mean_mag
"""

job = Gaia.launch_job(query)
results = job.get_results()
results.write('gaia_nearby_bright.csv', format='csv', overwrite=True)
print(f"Downloaded {len(results)} stars")
EOF
```

## Sky Dome / Milky Way Background

For the background starfield and Milky Way, use pre-rendered textures:

### ESA Gaia Sky Maps
High-resolution all-sky images from Gaia mission.

**Download:**
```bash
cd data/textures
mkdir -p skybox

# Gaia DR2 all-sky image (8K resolution)
wget https://www.cosmos.esa.int/documents/29201/1769576/GaiaDR2_FluxRGB_Cartesian_8192x4096.png
mv GaiaDR2_FluxRGB_Cartesian_8192x4096.png skybox/milky_way_gaia_8k.png
```

**Alternative - Celestia Milky Way:**
```bash
cd data/textures/skybox
# Download from Celestia Motherlode
wget https://www.celestiamotherlode.net/creators/granthutchison/textures/milkyway_4k.jpg
```

### Cubemap Format (for OpenGL skybox)
You can convert equirectangular panoramas to cubemap format using tools like:
- `imagemagick`
- `cmft` (CubeMap Filtering Tool)
- Python with `PIL`

**Example conversion:**
```bash
# Install imagemagick
sudo apt-get install imagemagick

# Convert to 6-face cubemap (each face 2048x2048)
convert milky_way_gaia_8k.png -define tiff:tile-geometry=2048x2048 \
    -crop 2048x2048 skybox_face_%d.png
```

## Catalog Parser Implementation

Kosmos will need parsers for these formats. Example structure:

```cpp
// src/Data/CatalogLoader.h
struct Star {
    uint64_t catalogId;
    std::string name;
    glm::dvec3 position;  // Cartesian (parsecs or ly)
    float apparentMagnitude;
    float absoluteMagnitude;
    float colorIndex;     // B-V color
    std::string spectralType;
    float temperature;    // Kelvin
};

class CatalogLoader {
public:
    std::vector<Star> LoadHipparcos(const std::string& filename);
    std::vector<Star> LoadGliese(const std::string& filename);
    std::vector<Star> LoadGaia(const std::string& filename);
};
```

## Coordinates Conversion

Catalogs typically provide:
- **Right Ascension (RA)**: 0-24 hours or 0-360 degrees
- **Declination (Dec)**: -90 to +90 degrees
- **Parallax**: milliarcseconds (convert to distance: d = 1000/parallax parsecs)

Convert to Cartesian coordinates:
```
x = d * cos(dec) * cos(ra)
y = d * cos(dec) * sin(ra)
z = d * sin(dec)
```

## Quick Start

For initial testing, use a minimal catalog:

```bash
cd data/catalogs
# Create a simple test catalog with nearest stars
cat > nearest_stars.txt << 'EOF'
# Name,Distance(ly),RA(deg),Dec(deg),Magnitude,SpectralType
Sol,0.0,0.0,0.0,-26.74,G2V
Proxima Centauri,4.24,217.42894,-62.67953,11.13,M5.5Ve
Alpha Centauri A,4.37,219.90205,-60.83399,-0.01,G2V
Alpha Centauri B,4.37,219.90205,-60.83399,1.34,K1V
Barnard's Star,5.96,269.45207,4.69339,9.53,M4.0V
Wolf 359,7.86,164.12031,7.00444,13.54,M6.0V
Sirius A,8.66,101.28715,-16.71611,-1.46,A1V
Sirius B,8.66,101.28715,-16.71611,8.44,DA2
Luyten 726-8 A,8.73,24.33803,-17.95691,12.61,M5.5V
EOF
```

## References

- **CDS Strasbourg:** https://cdsarc.cds.unistra.fr/
- **VizieR Catalog Service:** https://vizier.u-strasbg.fr/
- **ESA Gaia Archive:** https://gea.esac.esa.int/archive/
- **Celestia Motherlode:** https://www.celestiamotherlode.net/
- **SIMBAD:** http://simbad.u-strasbg.fr/

## File Sizes Summary

| Catalog | Stars | Size | Priority |
|---------|-------|------|----------|
| Nearest stars (test) | ~10 | 1 KB | High |
| Gliese | ~3,800 | 500 KB | High |
| Yale BSC | ~9,000 | 2 MB | Medium |
| Hipparcos-2 | ~118,000 | 50 MB | Medium |
| Gaia DR3 (subset) | ~50,000 | 5 MB | Low |
| Milky Way texture | N/A | 10-50 MB | High |

Start with the nearest stars test file and Milky Way texture for immediate visual results.
