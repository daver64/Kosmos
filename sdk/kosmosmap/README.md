# KosmosMap

A moving map for Kosmos, in the spirit of FlightGear's Atlas. It is an ordinary GTK 3 program written in C++ that talks to the game over KosmosLink; it does not use simlib.

- It shows the body whose sphere of influence the flown ship is in, shaded from the same Orbiter elevation data the game flies over (hypsometric colours and a hillshade for Earth, grey or rust for the Moon and Mars, or the planet's imagery). It switches body when the ship changes sphere of influence.
- It draws the ship, its track, other ships, airports with their runways and navaids, a graticule and a scale bar.
- Drag to pan, wheel or +/- to zoom, **F** follows the ship, **C** clears the track. The header bar also switches between relief and imagery.
- Without Kosmos it runs as a plain map browser: `kosmosmap --body Moon --lat 10 --lon 20 --ppd 12`. `--imagery` uses the picture colours and `--shot FILE.png [--size WxH]` renders a PNG without opening a window.

Tick **KosmosMap** in **View > Native addons** to let it read the game (it only needs the read capability). Without the Orbiter data (`data/orbiter`, not in the download) it shows the pictures in `data/textures` unshaded.

Build: it is part of the SDK (`sdk/kosmosmap`), and the game's `cmake --build build` builds it on Linux when GTK 3 and zlib are installed, writing `addons/kosmosmap/bin/kosmosmap`, which the manifest in `addons/kosmosmap/addon.json` launches. Elsewhere, build the SDK folder with CMake and copy `addon.json` and the program into an addon folder. The source in `src/` is a worked example of a native KosmosLink program: `main.cpp` has the link polling and the GTK drawing, `terrain.cpp` reads Kosmos's elevation data, `render.cpp` shades it.
