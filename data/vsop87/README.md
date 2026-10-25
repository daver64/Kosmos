# VSOP87 Data Files

This directory should contain VSOP87 planetary theory data files.

## Download Instructions

VSOP87 data files are available from the IMCCE (Institut de Mécanique Céleste et de Calcul des Éphémérides):

**Official Source:** ftp://ftp.imcce.fr/pub/ephem/planets/vsop87/

### Required Files

For VSOP87A (recommended - rectangular heliocentric ecliptic J2000):
```
VSOP87A.mercury
VSOP87A.venus
VSOP87A.earth
VSOP87A.mars
VSOP87A.jupiter
VSOP87A.saturn
VSOP87A.uranus
VSOP87A.neptune
```

### Download via wget

```bash
cd data/vsop87/
wget -r -np -nH --cut-dirs=4 -R "index.html*" ftp://ftp.imcce.fr/pub/ephem/planets/vsop87/VSOP87A.*
```

Or download individually:
```bash
cd data/vsop87/
for planet in mercury venus earth mars jupiter saturn uranus neptune; do
    wget ftp://ftp.imcce.fr/pub/ephem/planets/vsop87/VSOP87A.$planet
done
```

### Alternative: curl

```bash
cd data/vsop87/
for planet in mercury venus earth mars jupiter saturn uranus neptune; do
    curl -O ftp://ftp.imcce.fr/pub/ephem/planets/vsop87/VSOP87A.$planet
done
```

## File Format

VSOP87 files are plain text with periodic terms in the format:

```
VSOP87A  iPlanet PlanetName  Coord  iPower
 index      A           B           C
```

Where:
- **iPlanet**: Planet number (1=Mercury, 2=Venus, 3=Earth, etc.)
- **Coord**: Coordinate (X, Y, or Z for rectangular; L, B, R for spherical)
- **iPower**: Power of time variable (0-5)
- **A**: Amplitude
- **B**: Phase
- **C**: Frequency

Each term represents: `A * cos(B + C * T)` where T is time in Julian millennia from J2000.

## VSOP87 Variants

Different variants for different coordinate systems:

| Variant | Coordinates | Reference Frame | Use Case |
|---------|-------------|-----------------|----------|
| **VSOP87A** | Rectangular (X,Y,Z) | Heliocentric ecliptic J2000 | **Recommended** - Standard positions |
| VSOP87B | Spherical (L,B,R) | Heliocentric ecliptic J2000 | Orbital elements |
| VSOP87C | Rectangular (X,Y,Z) | Heliocentric ecliptic of date | Precession included |
| VSOP87D | Spherical (L,B,R) | Heliocentric ecliptic of date | Time-varying reference |
| VSOP87E | Rectangular (X,Y,Z) | Barycentric ecliptic J2000 | Solar system barycenter |

## File Sizes

Total size for VSOP87A: ~21 MB
- Mercury: ~2.5 MB
- Venus: ~3.5 MB
- Earth: ~3.0 MB
- Mars: ~2.5 MB
- Jupiter: ~2.5 MB
- Saturn: ~3.0 MB
- Uranus: ~2.0 MB
- Neptune: ~2.0 MB

## Accuracy

VSOP87 provides sub-arcsecond accuracy for planetary positions over thousands of years:
- **Short term** (±100 years from J2000): < 1" precision
- **Medium term** (±1000 years): < 10" precision
- **Long term** (±4000 years): accuracy degrades but still useful

## Citation

If using VSOP87 data, cite:
> Bretagnon P., Francou G., 1988, "*Planetary theories in rectangular and spherical variables. VSOP87 solutions*", Astronomy and Astrophysics, vol. 202, 309-315

## Implementation Notes

- Our implementation uses VSOP87A (rectangular heliocentric ecliptic J2000)
- Positions are in AU, converted to meters internally
- Time is Julian Date, converted to millennia from J2000 internally
- Coordinate conversion to equatorial system is provided

## Usage in Code

```cpp
#include "Physics/VSOP87.h"

VSOP87 vsop;
if (vsop.LoadData("data/vsop87", 'A')) {
    // Get Earth position on 2025-01-01
    double jd = VSOP87::CalendarToJulianDate(2025, 1, 1);
    glm::dvec3 earthPos = vsop.GetPosition(VSOP87::Planet::Earth, jd);
    
    // Position is in meters, heliocentric ecliptic J2000
    // Convert to equatorial if needed:
    glm::dvec3 earthPosEq = VSOP87::EclipticToEquatorial(earthPos);
}
```

## License

VSOP87 data files are in the public domain and freely available for scientific and educational use.
