# Celestia source data

This directory contains original Celestia texture sources, separate from the images installed for Kosmos to load.

- `hires/`, `medres/`, and `lores/` contain Celestia's original map collections. Large `hires/` packs are optional, git-ignored, and not shipped.
- `hires/earth/`, `hires/earth_specular/`, and `hires/earth_normal/` can provide streamed colour, specular, and normal tiles for Earth.
- `hires/<body>/` may contain a high-resolution single image for a body, while `hires/<body>_normal/` may contain its normal map.
- `moons/<planet>/<moon>/` groups moon source data by the planet it orbits. Keep the original moon name and any sibling `<Moon>_normal/` folder, `.ctx` descriptor, tile pyramid, and model files together under that planet. For example, Jupiter's Callisto data belongs in `moons/jupiter/Callisto/`, with its descriptor at `moons/jupiter/Callisto.ctx`. This source-only tree is not scanned at runtime and is excluded from release packages.
- `Sol/` and `stars/` preserve other source content from Celestia collections.

Install runtime defaults with `python3 tools/celestia_import.py`; this copies supported images into `data/textures/`. Use `--out data/textures-hires` for optional overrides. Do not place original source archives or tile pyramids in either runtime output folder.

Source data under `moons/` is organized for humans and tools; Kosmos does not scan those folders at runtime. To install a tiled moon map, stitch it into the runtime texture folder, for example:

```sh
python3 tools/ctx_import.py "data/celestia/moons/jupiter/Callisto.ctx" callisto --out data/textures-hires
```

Each downloaded pack has its own licence. Review its licence before redistributing any source files or installed images.
