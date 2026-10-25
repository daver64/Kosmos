Default surface images: equirectangular, in this folder (a fresh git checkout runs on these, from Celestia).
High-resolution replacements go in data/textures-hires/ (git-ignored) with the same names; a file there wins over one here.

Drop equirectangular surface images here, named after the body in lower case (earth.png, earth.jpg, mars.jpg, moon.png ...),
or set "texture": "textures/myfile.png" on the body in data/sol.json.

Sizes up to 16384 x 8192 are supported.
.dds files (BC1/DXT1, BC3/DXT5, BC7) are uploaded to the GPU still compressed, with the mip levels stored in the file:
a 16384 x 8192 BC1 Earth costs about 90 MB of VRAM and almost no system RAM (tools/gen_textures.py writes this format).
Other formats (png/jpg) are decoded to RGBA first: about 1.3 GB of RAM while loading and 0.7 GB of VRAM afterwards.
If the GPU cannot take the size, a DDS drops its largest mips; other images are halved until they fit.
Use --max-texture N to cap the size yourself.

Earth: below 300 km altitude the procedural terrain is drawn (the image is not used);
above 300 km the planet is drawn as a plain sphere with the image. Other bodies scale that distance by radius / 6371 km,
and bodies without terrain always use the image on their sphere.

Celestia textures: tools/celestia_import.py copies the best resolution of each Celestia moon/planet image in textures/celestia
(hires, then medres, then lores) to textures/<body>.jpg. It covers Mercury, the moons of Mars, Jupiter, Saturn, Uranus and Neptune.

Orbiter textures: tools/orbiter_import.py converts an Orbiter Surf.tree archive (Textures/<Planet>/Archive/Surf.tree)
into one of these .dds files without decoding the tiles, keeping the tree's levels as the mip chain:
    python3 tools/orbiter_import.py "C:/Orbiter" --body Earth --level 8      (8 = 16384 x 8192 with 512 px tiles; writes to data/textures-hires/)
Only the surface layer (DXT1/DXT5 tiles) is imported; mask, cloud and elevation layers are ignored.

Planet images: textures/planets/*.jpg are the original 2k downloads; the old procedural 8k ones were moved to textures-hires/procedural/ (not in git; regenerate with tools/gen_textures.py).
