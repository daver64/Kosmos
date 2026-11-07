# CVEL Atlas III source and attribution

Atlas III meshes and model-package textures are by Alex Hohlov; the original
notice credits Dave Rowbotham for the Sat1 mesh and Atlas III programming.
The mesh notice specifies the Artistic Licence; its text is included here.
The original model package contains the Anim8or source and the six bitmap
textures. The five Orbiter meshes used by Kosmos are supplied alongside it.

On 2026-11-07 Kosmos converted the five Orbiter meshes to OBJ/MTL, reflected
the Z axis, converted the corresponding DDS textures to PNG, and removed
coincident faces. The six DDS images have the same decoded pixels as the
bitmap images in the original model package. See
`tools/import_cvel_atlas.py` and `tools/msh_import.py` in the Kosmos source.
The converted models are distributed with Kosmos under the accompanying
Artistic Licence; this notice does not apply the separate LGPL code licence
to the meshes.
