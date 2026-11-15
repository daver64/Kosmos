Default surface images: equirectangular, in this folder or planets/ (see CREDITS-textures.md).
High-resolution replacements go in data/textures-hires/ (git-ignored) with the same names; a file there wins over one here.
Named files in this folder take precedence over planets/; optional Celestia hires imagery takes precedence over both.
Venus's default sphere shows a 4096 x 2048 cloud-top image; planets/venus.jpg is a radar-derived surface map.

Drop equirectangular surface images here, named after the body in lower case (earth.png, earth.jpg, mars.jpg, moon.png ...),
or set "texture": "textures/myfile.png" on the body in data/sol.json.

Sizes up to 16384 x 8192 are supported.
.dds files (BC1/DXT1, BC3/DXT5, BC7) are uploaded to the GPU still compressed, with the mip levels stored in the file:
a 16384 x 8192 BC1 Earth costs about 90 MB of VRAM and almost no system RAM (tools/gen_textures.py writes this format).
Other formats (png/jpg) are decoded to RGBA first: about 1.3 GB of RAM while loading and 0.7 GB of VRAM afterwards.
If the GPU cannot take the size, a DDS drops its largest mips; other images are halved until they fit.
Use --max-texture N to cap the size yourself.

The surface renderer depends on altitude, settings and installed data. Orbiter surface and elevation trees can provide terrain imagery and relief; when unavailable, Kosmos falls back to Celestia imagery or procedural terrain. The orbital relief renderer also requires suitable Orbiter imagery and elevation and falls back when they are unavailable.

Celestia source textures live in import/celestia (hires, then medres, then lores).
tools/celestia_import.py installs the best resolution of each supported moon/planet image here as <body>.jpg.

Orbiter terrain is read from the launcher-managed Archive layout:
    import/orbiter/<Body>/Archive/Surf.tree
    import/orbiter/<Body>/Archive/Elev.tree
Minor-body trees are under import/orbiter/MinorBodies/<Body>/Archive/.
The launcher also installs standard Orbiter scenery under data/orbiter-defaults/.
Its trees are in data/orbiter-defaults/earth/Earth/Archive/ and
data/orbiter-defaults/planets/<Body>/Archive/.
Legacy source folders and arbitrary Orbiter installations are not scanned at runtime.

Optional: tools/orbiter_import.py converts an Orbiter Surf.tree archive into one
of these .dds files without decoding the tiles, keeping the tree's levels as
the mip chain:
    python3 tools/orbiter_import.py "C:/Orbiter" --body Earth --level 8      (8 = 16384 x 8192 with 512 px tiles; writes to data/textures-hires/)
This conversion is only for a whole-planet image; runtime terrain reads the
launcher-managed Surf.tree and Elev.tree directly.

Planet images: textures/planets/*.jpg are the original 2k downloads; the old procedural 8k ones were moved to textures-hires/procedural/ (not in git; regenerate with tools/gen_textures.py).
