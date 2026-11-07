# Texture source and install layout

Keep source data separate from the images Kosmos loads at runtime:

| Source | Location | Installed output |
|---|---|---|
| Celestia texture collections and virtual-tile packs | `import/celestia/` | `data/textures/` for bundled defaults, `data/textures-hires/` for optional single-image overrides |
| Orbiter archives and `Surf.tree` data | `import/orbiter/` | `data/textures-hires/` for optional converted maps |

The repository and release contain runtime fallback textures; Earth, Mars, Jupiter, Europa, Ganymede, Callisto, Io, Phobos and Deimos are NASA images identified in `data/CREDITS-textures.md`. Some NASA mosaics have visible gaps or seams. Large Celestia and Orbiter source packs and high-resolution output overrides are not shipped. Unverified `data/textures/*_normal.jpg` planetary normal maps are excluded from packages, so those surfaces have no normal-map relief unless a user installs a replacement. The `import/celestia/hires/` source directory, `import/orbiter/` and `data/textures-hires/` are git-ignored; the source-only `import/celestia/moons/` tree is excluded from release packages. Other bundled maps still require individual provenance review.

## Celestia

Put the original Celestia texture tree under `import/celestia/`, preserving its `hires/`, `medres/`, and `lores/` directories. To install the best available images for supported moons and planets into the runtime fallback folder:

```sh
python3 tools/celestia_import.py --list
python3 tools/celestia_import.py
```

The default import targets the bundled fallback folder and refuses to replace
existing files unless `--force` is specified. Use `--out data/textures-hires`
to install optional overrides without replacing the bundled maps; create that
output directory first if necessary. Only redistribute imported images when their individual
terms permit it. The game searches `data/textures-hires/` before Celestia's
`import/celestia/hires/<body>/` folder, then `data/textures-generated/`,
then falls back to `data/textures/`.

### Organizing moon sources

Store moons under the planet they orbit, rather than in the planet texture collection:

```text
import/celestia/moons/
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
python3 tools/ctx_import.py "import/celestia/moons/jupiter/Callisto.ctx" callisto --out data/textures-hires
```

Earth can stream Celestia virtual textures directly from:

```text
import/celestia/hires/earth/
import/celestia/hires/earth_specular/
import/celestia/hires/earth_normal/
```

These tile packs are optional and are not needed for a clean checkout. Earth also samples the tiled colour map for close-up terrain where it has finer texels than the available Orbiter `Surf.tree`; Orbiter `Elev.tree` remains the source of terrain heights. `--no-tiles` forces the ordinary Earth image for the orbital sphere. For other bodies, `tools/ctx_import.py` stitches a Celestia `.ctx` pyramid into one image in `data/textures-hires/`:

```sh
python3 tools/ctx_import.py "import/celestia/hires/path/to/map.ctx" mars
```

## Orbiter

Store Orbiter installations or unpacked archives in `import/orbiter/`. The game reads terrain archives there directly. To install an orbital-view surface image without decoding/re-encoding the source tiles:

```sh
python3 tools/orbiter_import.py "import/orbiter/Orbiter 2016 High Res: Earth/Surf.tree" --body earth --level 8
```

The importer writes the optional image into `data/textures-hires/`. Keep `Surf.tree`, elevation data, and other source files under `import/orbiter/`; do not put them in the runtime texture folders.
