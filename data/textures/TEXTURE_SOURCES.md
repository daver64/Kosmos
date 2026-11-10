# Texture source and install layout

Keep source data separate from the images Kosmos loads at runtime:

| Source | Location | Installed output |
|---|---|---|
| Celestia texture collections and virtual-tile packs | `import/celestia/` | `data/textures/` for bundled defaults, `data/textures-hires/` for optional single-image overrides |
| Orbiter archives and `Surf.tree` data | `import/orbiter/` | `data/textures-hires/` for optional converted maps |

The repository and release contain low-resolution runtime fallback textures. Large Celestia and Orbiter source packs and high-resolution output overrides are not shipped. The `import/celestia/hires/` source directory, `import/orbiter/` and `data/textures-hires/` are git-ignored; the source-only `import/celestia/moons/` tree is excluded from release packages.

## Celestia

Put the original Celestia texture tree under `import/celestia/`, preserving its `hires/`, `medres/`, and `lores/` directories. To install the best available images for supported moons and planets into the runtime fallback folder:

```sh
python3 tools/celestia_import.py --list
python3 tools/celestia_import.py
```

Use `--out data/textures-hires` to install optional high-resolution overrides instead. The game searches `data/textures-hires/` before Celestia's `import/celestia/hires/<body>/` folder, then falls back to `data/textures/`.

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

These tile packs are optional and are not needed for a clean checkout. Close-up terrain prefers available Orbiter `Surf.tree` imagery, including parent tiles, and uses the Celestia tiles only where no readable Orbiter tile or parent exists; Orbiter `Elev.tree` remains the source of terrain heights when installed. `--no-tiles` forces the ordinary Earth image for the orbital sphere. For other bodies, `tools/ctx_import.py` stitches a Celestia `.ctx` pyramid into one image in `data/textures-hires/`:

```sh
python3 tools/ctx_import.py "import/celestia/hires/path/to/map.ctx" mars
```

## Orbiter

Store Orbiter installations or unpacked archives in `import/orbiter/`. The game reads terrain archives there directly, including flat and signed or unsigned 8- and 16-bit elevation tiles. An `Elev_mod.tree` beside the selected `Elev.tree` supplies flat or masked 8-bit and signed 16-bit modifications to matching tiles; unreadable modifications are reported and the unmodified tile is used. Each archive index is limited to 128 MiB; archives with larger indexes or invalid bounds are reported and not opened. Terrain imagery and elevation tiles are limited to 2 MiB each, both compressed and decompressed; oversized or unreadable tiles are reported and fall back to a readable ancestor.

Individual `<body>/Elev/LL/RRRRRR/CCCCCC.elv` and `<body>/Elev_mod/LL/RRRRRR/CCCCCC.elv` files beside the selected elevation archive override their exact archive tiles. Sparse elevation files can add detail; modifications require a base tile at the same level. Invalid or oversized files are reported and fall back to the archive. Each layer is indexed at startup, with a limit of 200,000 files; changes require reloading the system.

Individual Orbiter BC1/BC2/BC3 surface tiles beside the selected archive, in `<body>/Surf/LL/RRRRRR/CCCCCC.dds`, take precedence over the same archive tile for terrain colour and the experimental relief renderer. DDS files may use DXT1/DXT3/DXT5 or the corresponding DX10 formats. Sparse files may supply higher detail without replacing the archive; unreadable or unsupported overrides fall back to the archive and then its ancestors. Overrides are indexed at startup, with a limit of 200,000 files; changes require reloading the system.

The experimental relief renderer also limits each decoded tile to 1 MiB of RGBA pixels (for example 512 x 512); a larger override falls back to the same-level archive tile, then ancestors.

The default Orbiter relief renderer covers Earth and the current non-Earth sphere-of-influence body when both Orbiter imagery and elevation are available. Coloured terrain meshes are blended into loaded relief tiles at their imagery resolution; meshes smaller than a texel may not appear. It renders near the ground by default (`--no-orbital-relief-ground` restores the previous renderer); the change in sharpness at transitions can be visible. Hard visible-patch, mesh-entry and resident-texture limits default to unlimited; the corresponding `--orbital-relief-*` options set optional positive caps. Near a base, flattened site and runway samples are checked close to the camera. Near a ship, five points around its origin and actual landing-gear contacts are checked; the visible point below a low camera is also checked. If relief is unavailable or differs by more than 1 m at a checked point, Kosmos temporarily falls back to existing terrain and resumes relief after checks pass continuously for one second. These local checks do not certify alignment across an entire base; other areas and distant base props may still be misaligned. Relief uses fixed 24-segment meshes without skirts; six-segment skirt-free coverage has a reproducible mixed-LOD gap. Budgets are per renderer, not aggregate memory limits.

When no archive is available for a layer, individual files may supply that layer on their own. Uncovered elevation is zero; imagery falls back to other available sources. The experimental relief renderer additionally requires readable coverage at its minimum imagery level.

To install an orbital-view surface image without decoding/re-encoding the source tiles:

```sh
python3 tools/orbiter_import.py "import/orbiter/Orbiter 2016 High Res: Earth/Surf.tree" --body earth --level 8
```

The importer writes the optional image into `data/textures-hires/`. Keep `Surf.tree`, elevation data, and other source files under `import/orbiter/`; do not put them in the runtime texture folders.
