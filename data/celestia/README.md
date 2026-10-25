# Celestia source data

This directory contains original Celestia texture sources, separate from the images installed for Kosmos to load.

- `hires/`, `medres/`, and `lores/` contain Celestia's original map collections. Large `hires/` packs are optional, git-ignored, and not shipped.
- `hires/earth/`, `hires/earth_specular/`, and `hires/earth_normal/` can provide streamed colour, specular, and normal tiles for Earth.
- `hires/<body>/` may contain a high-resolution single image for a body, while `hires/<body>_normal/` may contain its normal map.
- `Sol/` and `stars/` preserve other source content from Celestia collections.

Install runtime defaults with `python3 tools/celestia_import.py`; this copies supported images into `data/textures/`. Use `--out data/textures-hires` for optional overrides. Do not place original source archives or tile pyramids in either runtime output folder.

Each downloaded pack has its own licence. Review its licence before redistributing any source files or installed images.
