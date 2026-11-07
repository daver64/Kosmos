# Gravity model data

The JSON files contain fully normalized spherical-harmonic coefficients and
their reference radius and gravitational parameter. They are truncated to
the degree and order used by Orbiter's body configurations: 10 for Mercury,
Venus, Earth, Moon, Mars and Vesta, and 70 for Ceres. Degree zero is omitted
because simlib adds point-mass gravity separately.

The source filenames are recorded inside each JSON file. The planetary models
come from NASA's Planetary Data System, including the MESSENGER, Magellan,
Lunar Prospector, Mars Reconnaissance Orbiter and Dawn gravity solutions.
NASA's [PDS gravity archive](https://pds-geosciences.wustl.edu/dataserv/gravity_models.htm)
and [Vesta data set](https://pds.nasa.gov/ds-view/pds/viewProfile.jsp?dsid=DAWN-A-RSS-5-VEGR-V1.0)
describe these products. The Earth model is EGM96, developed by NASA GSFC,
NIMA and Ohio State University; see the [EGM96 report](https://ntrs.nasa.gov/citations/19980218814).
The coefficient files were obtained from the Orbiter distribution's
`GravityModels` directory. Body orientation constants use the
[NAIF PCK](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc)
and existing Kosmos Earth and Mars orientation data.

Kosmos uses its own body ephemerides and an IAU-style rotation model, rather
than Orbiter's complete ephemerides and body-orientation implementation.
Terrain and rendered surfaces can use a different, simplified orientation.
Therefore identical coefficients and degree limits do not guarantee identical
trajectories or registration of gravity anomalies with surface features.
