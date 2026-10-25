# Texture source and install layout

Keep source data separate from the images Kosmos loads at runtime:

| Source | Location | Installed output |
|---|---|---|
| Celestia texture collections and virtual-tile packs | `data/celestia/` | `data/textures/` for bundled defaults, `data/textures-hires/` for optional single-image overrides |
| Orbiter archives and `Surf.tree` data | `data/orbiter/` | `data/textures-hires/` for optional converted maps |

The repository and release contain low-resolution runtime fallback textures. Large Celestia and Orbiter source packs and high-resolution output overrides are not shipped. The `data/celestia/hires/` source directory, `data/orbiter/` and `data/textures-hires/` are git-ignored; the source-only `data/celestia/moons/` tree is excluded from release packages.

## Celestia

Put the original Celestia texture tree under `data/celestia/`, preserving its `hires/`, `medres/`, and `lores/` directories. To install the best available images for supported moons and planets into the runtime fallback folder:

```sh
python3 tools/celestia_import.py --list
python3 tools/celestia_import.py
```

Use `--out data/textures-hires` to install optional high-resolution overrides instead. The game searches `data/textures-hires/` before Celestia's `data/celestia/hires/<body>/` folder, then falls back to `data/textures/`.

### Organizing moon sources

Store moons under the planet they orbit, rather than in the planet texture collection:

```text
data/celestia/moons/
  jupiter/
    Amalthea/
    Callisto/
    Callisto.ctx
    Callisto_normal/
    Europa/
    Ganymede/
  saturn/
  uranus/
  neptune/
```

Keep the original moon spelling and its related `.ctx`, `levelN` tile folders, normal maps, and model assets together under the parent planet. The source tree is not scanned by Kosmos at runtime. Install a desired tiled moon map to the game's runtime textures with `ctx_import.py`, for example:

```sh
python3 tools/ctx_import.py "data/celestia/moons/jupiter/Callisto.ctx" callisto --out data/textures-hires
```

Earth can stream Celestia virtual textures directly from:

```text
data/celestia/hires/earth/
data/celestia/hires/earth_specular/
data/celestia/hires/earth_normal/
```

These tile packs are optional and are not needed for a clean checkout. `--no-tiles` forces the ordinary Earth image. For other bodies, `tools/ctx_import.py` stitches a Celestia `.ctx` pyramid into one image in `data/textures-hires/`:

```sh
python3 tools/ctx_import.py "data/celestia/hires/path/to/map.ctx" mars
```

## Orbiter

Store Orbiter installations or unpacked archives in `data/orbiter/`. The game reads terrain archives there directly. To install an orbital-view surface image without decoding/re-encoding the source tiles:

```sh
python3 tools/orbiter_import.py "data/orbiter/Orbiter 2016 High Res: Earth/Surf.tree" --body earth --level 8
```

The importer writes the optional image into `data/textures-hires/`. Keep `Surf.tree`, elevation data, and other source files under `data/orbiter/`; do not put them in the runtime texture folders.
