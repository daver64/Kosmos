# planetbake

Bakes the generated worlds of Kosmos into image files: colour, height, normal, specular, night-lights and cloud maps, as PNG or DDS, as one image or as a tile pyramid. It is a tool of its own, not part of the game, so a very large job (65536 pixels wide is 4 GB of PNG per layer) does not run inside Kosmos.

It uses the game's own world generator (`gen/planet_gen.*`), so a world baked here is the world you see in Kosmos, given the same type, seed and parameters. It does not need simlib.

**It never uses more than 4 threads**, however many cores the machine has. `--threads` can lower that, not raise it. Only one region is rendered at a time, so a preview and an export running together still stay within 4.

## Running it

`planetbake` with no options opens the window (when it was built with GTK 3): a 1024 x 512 preview of the world, buttons for the layer shown (Colour, Height, Normal, Specular, Night, Clouds), controls for the world (type, seed, ocean, ice, relief, tint, bands and contrast for gas giants, radius) and an Export panel (width, layers, PNG or DDS, tile pyramid, folder, name). The panel shows the disk size before you start, and refuses a job that would not fit the memory limit. Export runs in the background with a progress bar and can be cancelled; a tile export that was stopped carries on where it left off when started again.

With options it runs in the terminal:

    planetbake --type earth --seed 7 --width 8192 --out maps --name earth7
    planetbake --type mars --seed 3 --width 16384 --format dds --dds bc1 --layers colour,normal,specular --out mars
    planetbake --type earth --seed 7 --width 65536 --tiles --format dds --out earth-tiles    # a tile pyramid
    planetbake --type earth --seed 7 --width 65536 --tiles --estimate                       # only print the size
    planetbake --type venus --seed 2 --preview venus.png --layer colour                     # 1024 x 512 preview

| Option | Meaning |
|---|---|
| `--type NAME` | `mercury`, `venus`, `earth`, `mars` or `gas` (default `earth`) |
| `--seed N`, `--ocean F`, `--ice F`, `--relief F`, `--tint R,G,B`, `--bands N`, `--contrast F` | The generator's parameters (see `kosmos --bake-planet`, which prints the full line for a world in the game) |
| `--radius-km F` | Planet radius, default 6371. It sets the real size of heights and slopes. |
| `--out DIR`, `--name NAME` | Where the files go, and the file name stem for single images |
| `--width N` | Pixels round the equator. Single images 256 to 16384; tile pyramids go to 65536. |
| `--layers LIST` | Any of `colour,height,normal,specular,night,clouds`, or `all` (default). A single layer can be baked on its own (`--layers clouds`) into an existing folder or pack: the other layers are left alone and `info.json` keeps listing them. Layers that mean nothing for the world are skipped (a gas giant has only colour; night lights are for earth-like worlds; clouds are for worlds with an atmosphere: earth, venus and mars). |
| `--format png\|dds` | Default `png`. Height is always a 16 bit PNG, because DDS here is 8 bit. |
| `--dds bc1\|bc3\|rgba` | `bc1` is 4 bits a pixel, `bc3` 8, `rgba` uncompressed. Every DDS has a full mip chain. |
| `--tiles` | A tile pyramid per layer instead of single images |
| `--detail F` | Fine land colour detail (default 1, 0 for the generator's own smooth colour). The generator's colour only varies over roughly 100 km and more, so without this a 64k map is a very smooth image. The detail is added at scales from 40 km down to 40 m, and only the scales the pixels can resolve, so previews and small images look as before. |
| `--detail F` | Fine land colour detail (default 1; 0 gives the generator's own smooth colour). The generator's colour only varies over about 100 km and more, so without this a 64k map is a very smooth image. Detail is added from 40 km down to 40 m, and only at scales the pixels can resolve, so previews and small images look as before. |
| `--normal-strength F` | Default 8. A whole planet's real slopes are tiny, so a normal map exaggerates them; 1 is the true slope. |
| `--memory MB` | Refuse a job that needs more than this (default 4096) |
| `--threads N` | 1 to 4 |
| `--estimate` | Print the size of the job and stop |
| `--preview FILE`, `--layer NAME` | Write a 1024 x 512 PNG of one layer and stop |

## Working on an existing pack

planetbake can open any folder of planet textures: tile pyramids (`LAYER/levelN/tx_C_R.png|dds`) or single images (`NAME_colour.png`, ...). `planetbake --open DIR` opens the window on it (the window's **Open pack...** button does the same). The layer buttons then show the pack, with missing tiles grey. Click the map to pick a tile at the level set by the slider (it is outlined in red), then:

- **Edit tile** copies the tile to a working PNG (`~/.cache/planetbake/edit/`), backs the original up to `PACK/.originals/` and opens it in your graphics program (default `krita`; change it in the window's entry or with `--set-editor "gimp"`, kept in `~/.config/planetbake/settings.json`). When you save, the tile is taken back automatically (or press **Import edit**), written in the tile's own format (an edited DDS tile is edited as PNG) and locked. The picture must keep its size. Other levels are not regenerated from an edited tile.
- **Restore original** puts the backup back and unlocks the tile.
- **Lock/unlock tile, layer or pack** writes `.locked` marker files. baking and filling skip locked tiles and layers and refuse a locked pack. Kosmos itself never writes into `data/textures-hires/`, so a pack a body names with `"tiles"` is not overwritten by the game's procedural generation.
- **Fill missing...** fills what the pack lacks (missing tiles, levels or layers), in this order: a source picture or folder you give, the pack's own coarser level scaled up, then the generator (using the pack's `info.json`). Existing and locked tiles are never touched. Heights are converted into the pack's height range; the fill's own notes are written to `planetbake-fill.json`, not your `info.json`.

On the command line:

    planetbake --open DIR --info                                  # layers, levels, locks
    planetbake --open DIR --fill --dry-run                        # count what would be filled
    planetbake --open DIR --fill --source ~/celestia/textures     # a folder: files are matched to layers by name
    planetbake --open DIR --fill --source colour=earth.png --source gebco
    planetbake --open DIR --lock layer:height                     # or: pack, tile:colour:3:5:2
    planetbake --open DIR --edit-tile colour:3:5:2                # NAME:LEVEL:COLUMN:ROW; --no-launch to only make the copy
    planetbake --open DIR --import-tile colour:3:5:2 [--from FILE]
    planetbake --open DIR --restore-tile colour:3:5:2
    planetbake --list-sources                                     # public datasets planetbake can download

`--source` takes `[LAYER=]PATH` (a picture, or a folder of pictures) or a catalogue name (`bluemarble`, `bluemarble-small`, `gebco`, `blackmarble`, plus any in `~/.config/planetbake/sources.json`), which is downloaded with curl into `~/.cache/planetbake/downloads` (NASA imagery, free to use with credit). `--no-generate` stops the fill at sources and coarser levels. Source pictures must be equirectangular (longitude -180 to 180, north up) and are not reprojected; JPEG and TIFF are converted with ImageMagick when it is installed. The GEBCO height range is approximate. The Windows program has the same window.

## What the layers are

| Layer | Contents |
|---|---|
| `colour` | Surface colour with the generator's relief shading |
| `height` | 16 bit grey, linear from the world's lowest point (0) to its highest (65535). The heights in metres are in the info file. |
| `normal` | Tangent space normals: R east, G north, B up, as 0.5 + 0.5 x component |
| `specular` | Gloss, white is shiny: seas and ice on earth-like worlds, ice on others |
| `night` | City lights for the dark side (earth-like worlds), black elsewhere |
| `clouds` | White cloud cover in the alpha channel (RGBA PNG, or BC3 DDS; BC1 is raised to BC3). Earth: equatorial convection, clear subtropics, swirled mid-latitude storm belts. Venus: an unbroken, streaky yellowish deck. Mars: thin wisps and a polar hood. Mercury and gas giants have none |

Single images are written as `NAME_LAYER.png` (or `.dds`) and `NAME_info.json`, with the parameters, size and height range. Images are written a strip at a time, so a 16384 wide PNG needs a few hundred MB of memory. A DDS layer is kept whole to build its mip chain and needs about 1.3 x width x height x 4 bytes.

## Tile pyramids

`--tiles` writes `OUT/LAYER/levelN/tx_COLUMN_ROW.png` (or `.dds`) and `OUT/LAYER/tiles.ctx`, the same layout Kosmos bakes with `--bake-planet`: 512 pixel tiles, level N having 2^(N+1) columns and 2^N rows, at least 3 levels (4096 wide) and up to 7 (65536 wide). The width is rounded up to a whole number of levels. Tiles already on disk are skipped, so an interrupted run resumes. Disk use grows four-fold with each doubling of width; `--estimate` prints it.

To use a colour pyramid in the game, copy `OUT/colour` into the world's folder in the tile cache (`kosmos --bake-planet` prints the folder). Kosmos reads PNG, JPEG and DDS tiles.

## Building

The game's own build (`scripts/build-linux.sh`, `build-windows.sh`, `release.sh`) builds it into `addons/planetbake/bin/` and puts it in the game packages (the game's Windows cross build makes the command line version; the SDK's Windows program has the window). To build it alone:

From the SDK folder (needs a C++17 compiler, CMake, zlib, glm and `FastNoiseLite.h`; GTK 3 adds the window):

    cmake -S planetbake -B build-pb
    cmake --build build-pb --parallel 4
    build-pb/bin/planetbake --help

`gen/` holds the generator, `FastNoiseLite.h` is looked for in `gen/`, `~/opt/include`, or where `-DFASTNOISE_DIR=` and `-DGLM_INCLUDE_DIR=` point. `-DPLANETBAKE_NO_GUI=ON` makes a command line only build. Build with at most 4 jobs (the Windows GTK build is made by `scripts/build-sdk.sh`, which needs GTK 3 installed in vcpkg's `x64-mingw-posix` tree). The SDK download also has ready-made programs in `bin/`: `planetbake-linux-x86_64` (needs GTK 3 for the window) and `planetbake-windows-x86_64.exe` (with the window; it needs its `share/` folder next to `bin/`).

## Source

| File | Contents |
|---|---|
| `src/layers.*` | The layers: samples the generator once per region and derives colour, height, normal, specular, night and clouds from it |
| `src/encode.*` | PNG (streaming, zlib only) and DDS (BC1, BC3, RGBA, mip chains) writers |
| `src/bake.*` | Jobs: single images and tile pyramids, size estimates |
| `src/main.cpp`, `src/gui.cpp` | The command line and the GTK 3 window |
| `compat/` | Stand-ins for the few simlib types the generator uses |
| `gen/` | The game's `planet_gen` and `FastNoiseLite.h` (copied in when the SDK is packaged; in the game's tree the build uses `src/planet_gen.*`) |
